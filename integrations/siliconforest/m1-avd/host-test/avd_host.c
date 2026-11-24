// SPDX-License-Identifier: MIT
/*
 * sfavd-host: runs a libsfavd recording through the driver core (sf_avd_core.c and the
 * Linux codec files) against the host stand-in for the decoder, and writes every word
 * the driver sends to the decoder: the instruction stream of each picture.
 *
 *   avd_host <recording> [<instruction log>]      exit 0 when every decode succeeded
 *   avd_host --selftest                           the checks that need no recording
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/sf_avd_core.h"
#include "../user/sfavd_rec.h"
#include "host_os.h"

static uint8_t g_fw[0xc000];

static struct sf_avd_device *
device(void)
{
	static const char tail[] = "CODECS:h264,h265,vp9";

	memset(g_fw, 0, sizeof(g_fw));
	memcpy(g_fw + sizeof(g_fw) - 0x40, tail, sizeof(tail));
	return sf_avd_device_create("apple,t8103-avd", host_code, host_mbox, host_ctrl, g_fw, sizeof(g_fw));
}

/* Model capture replacement, not a real GPU/DMA test. Exercise a late
 * invalid entry before commit, verify all old addresses survive refusal, then
 * transfer ownership and let normal decoding/destruction use the new set. */
static int import_capture_test(struct sf_avd_session *session,unsigned count,size_t bytes)
{
    void *cpu[SFAVD_MAX_BUFFERS]={0},*cookies[SFAVD_MAX_BUFFERS]={0},*old[SFAVD_MAX_BUFFERS]={0};
    void *before[SFAVD_MAX_BUFFERS]={0};
    uint64_t iova[SFAVD_MAX_BUFFERS]={0};
    for(unsigned i=0;i<count;i++) {
        size_t length=0;void *cookie=NULL;
        if(sf_avd_session_buffer(session,i,&before[i],&length,&cookie)||length!=bytes)return -1;
        cpu[i]=sf_avd_os_dma_alloc(bytes,&iova[i],&cookies[i]);
        if(!cpu[i])return -1;
    }
    void *last=cpu[count-1];cpu[count-1]=NULL;
    if(sf_avd_session_import(session,count,cpu,iova,cookies,old)!=-EINVAL)return -1;
    for(unsigned i=0;i<count;i++) {
        size_t length=0;void *address=NULL,*cookie=NULL;
        if(sf_avd_session_buffer(session,i,&address,&length,&cookie)||address!=before[i]||old[i])return -1;
    }
    cpu[count-1]=last;
    if(sf_avd_session_import(session,count,cpu,iova,cookies,old))return -1;
    for(unsigned i=0;i<count;i++) {
        size_t length=0;void *address=NULL,*cookie=NULL;
        if(sf_avd_session_buffer(session,i,&address,&length,&cookie)||address!=cpu[i]||cookie!=cookies[i])return -1;
        sf_avd_os_dma_free(old[i]);
    }
    if(sf_avd_session_import(session,count,cpu,iova,cookies,old)!=-EBUSY)return -1;
    return 0;
}

static int
replay(const char *path, const char *logpath)
{
	FILE *f = fopen(path, "rb"), *log = logpath ? fopen(logpath, "w") : NULL;
	struct sf_avd_device *dev = device();
	struct sf_avd_session *sessions[16] = { 0 };
	struct sfavd_format formats[16];
	struct sfavd_rec r = { 0 };
	unsigned decodes = 0, pictures = 0, failed = 0;
	int ret;

	if (f == NULL || dev == NULL || (logpath && log == NULL)) {
		fprintf(stderr, "cannot open %s\n", path);
		return 2;
	}
	host_set_log(log, getenv("AVD_HOST_VERBOSE") ? atoi(getenv("AVD_HOST_VERBOSE")) : 1);
	while ((ret = sfavd_rec_read(f, &r)) == 1) {
		if (r.type == SFAVD_REC_CREATE) {
			uint32_t c = r.create.ctx;

			if (c >= 16 || sessions[c] != NULL)
				return 2;
			if (log)
				fprintf(log, "create ctx %u codec %.4s %ux%u buffers %u\n", c, (const char *)&r.create.codec,
				    r.create.width, r.create.height, r.create.buffers);
			ret = sf_avd_session_create(dev, &sessions[c]);
			if (ret == 0)
				ret = sf_avd_session_set_format(sessions[c], r.create.codec, r.create.width, r.create.height,
				    r.create.format_ctrl.id, r.format_ctrl, r.create.format_ctrl.size, &formats[c]);
			if (ret == 0)
				ret = sf_avd_session_alloc(sessions[c], r.create.buffers);
            if (ret==0 && getenv("AVD_HOST_IMPORT"))
                ret=import_capture_test(sessions[c],r.create.buffers,formats[c].sizeimage);
			if (ret != 0) {
				fprintf(stderr, "create failed: %d\n", ret);
				return 1;
			}
			if (log)
				fprintf(log, "format %.4s %ux%u stride %u uv %u size %u\n", (const char *)&formats[c].pixelformat,
				    formats[c].width, formats[c].height, formats[c].bytesperline, formats[c].uv_offset,
				    formats[c].sizeimage);
		} else if (r.type == SFAVD_REC_DECODE) {
			struct sf_avd_session *s = r.decode.ctx < 16 ? sessions[r.decode.ctx] : NULL;
			uint32_t status = 0;
			void *coded;

			if (s == NULL)
				return 2;
			decodes++;
			ret = 0;
			for (uint32_t i = 0; i < r.decode.nctrls && ret == 0; i++)
				ret = sf_avd_session_set_ctrl(s, r.decode.ctrls[i].id, r.ctrl[i], r.decode.ctrls[i].size);
			coded = ret == 0 ? sf_avd_session_coded(s, r.decode.size) : NULL;
			if (coded != NULL)
				memcpy(coded, r.data, r.decode.size);
			else if (ret == 0)
				ret = -ENOMEM;
			if (log)
				fprintf(log, "decode ts %" PRIu64 " dst %u flags %u size %u\n", r.decode.timestamp, r.decode.dst,
				    r.decode.flags, r.decode.size);
			if (ret == 0)
				ret = sf_avd_session_decode(s, r.decode.size, r.decode.dst, r.decode.timestamp, r.decode.flags,
				    &status);
            if(ret==0) {
                void *empty[SFAVD_MAX_BUFFERS]={0};uint64_t zero[SFAVD_MAX_BUFFERS]={0};
                /* No import may enter after any successful decode/HOLD. */
                unsigned count=0;void *address,*cookie;size_t length;
                while(count<SFAVD_MAX_BUFFERS && !sf_avd_session_buffer(s,count,&address,&length,&cookie))count++;
                if(sf_avd_session_import(s,count,empty,zero,empty,empty)!=-EBUSY)return 1;
            }
			if (log)
				fprintf(log, "result %d status %x\n", ret, status);
			if (ret != 0) {
				failed++;
				fprintf(stderr, "decode %u (ts %" PRIu64 ") failed: %d\n", decodes, r.decode.timestamp, ret);
			} else if (!(r.decode.flags & SFAVD_DECODE_HOLD)) {
				pictures++;
			}
		} else {
			if (r.destroy.ctx >= 16 || sessions[r.destroy.ctx] == NULL)
				return 2;
			sf_avd_session_destroy(sessions[r.destroy.ctx]);
			sessions[r.destroy.ctx] = NULL;
			if (log)
				fprintf(log, "destroy ctx %u\n", r.destroy.ctx);
		}
	}
	if (ret != 0) {
		fprintf(stderr, "malformed recording\n");
		return 2;
	}
	for (unsigned c = 0; c < 16; c++)
		if (sessions[c] != NULL)
			sf_avd_session_destroy(sessions[c]);
	sf_avd_device_destroy(dev);
	printf("%s: %u decode calls, %u pictures, %u failed, %u allocations left, peak device memory %zu KiB\n", path,
	    decodes, pictures, failed, host_live_allocs(), host_peak_bytes() / 1024);
	if (log)
		fclose(log);
	fclose(f);
	free(r.body);
	return failed || host_live_allocs() ? 1 : 0;
}

int
main(int argc, char **argv)
{
	if (argc >= 2 && strcmp(argv[1], "--selftest") != 0)
		return replay(argv[1], argc > 2 ? argv[2] : NULL);
	fprintf(stderr, "usage: avd_host <recording> [<instruction log>]\n");
	return 2;
}
