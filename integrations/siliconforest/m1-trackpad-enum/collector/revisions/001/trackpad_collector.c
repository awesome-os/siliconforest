/* Original finite IORegistry diagnostic collector; no device opens or commands.
 * Framing/flow reused from our reviewed DART collector revision002.
 */
#include "trackpad_collector.h"
#include "observation_layout.h"
#include <string.h>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/sha.h>
#endif
static const char PROVIDER_KEY[]="provider";
static const char INSTANCE_KEY[]="instance";
static uint32_t u32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0]|(unsigned)p[1]<<8); }
static int zero(const uint8_t *p,size_t n)
{ for(size_t i=0;i<n;i++) if(p[i])return 0; return 1; }
int tpec_nonce_valid(const char *p)
{
 if(!p||strlen(p)!=48)return 0;
 for(unsigned i=0;i<48;i++)if(!((p[i]>='0'&&p[i]<='9')||(p[i]>='a'&&p[i]<='f')))return 0;
 return 1;
}
/* Validate only exact producer state/data consistency. A stored descriptor is
 * evidence of acquisition, not proof of valid HID semantics or pointer input.
 */
int tpec_validate(const uint8_t *b,size_t n,struct tpec_summary *out)
{
 if(!b||!out||n!=TPEC_DIAG_BYTES||u32(b)!=M1TP_OBSERVATION_VERSION)return -1;
 const int32_t result=(int32_t)u32(b+4);
 const uint32_t stage=u32(b+8), mask=u32(b+12), m=u32(b+16), rx=u32(b+20), stored=u32(b+24);
 const uint8_t *header=b+28,*desc=b+44;
 if(stage>M1TP_OBS_COMPLETE || (mask&~31u) || m>2038 || rx>2038 || stored>512)return -1;
 if(!zero(desc+stored,512-stored))return -1;
 /* Producer preserves a truncated numeric-header prefix on short replies. */
 if(m<16 && !zero(header+m,16-m))return -1;
 const int valid_meta=m>=16 && header[1]==2 && u16(header+6)>=1 && u16(header+6)<=2038;
 if(stage==M1TP_OBS_BEFORE_IO) {
  if(result!=M1TP_OBS_NOT_ATTEMPTED || !(mask==0||mask==1||mask==3||mask==7) ||
     m||rx||stored||!zero(header,16))return -1;
 } else {
  if(mask!=(stage<=M1TP_OBS_INFO_RECEIVED?15u:31u))return -1;
  switch(stage) {
  case M1TP_OBS_INFO_REQUEST:
   if(result!=M1TP_OBS_INFO_REQUEST_FAILED || m||rx||stored||!zero(header,16))return -1;
   break;
  case M1TP_OBS_INFO_RECEIVED:
   if(result!=M1TP_OBS_INFO_INVALID || valid_meta||rx||stored)return -1;
   break;
  case M1TP_OBS_DESC_REQUEST:
   if(result!=M1TP_OBS_DESC_REQUEST_FAILED || !valid_meta||rx||stored)return -1;
   break;
  case M1TP_OBS_DESC_RECEIVED:
   if(result!=M1TP_OBS_DESC_INVALID || !valid_meta||stored||(rx>=1&&rx<=512))return -1;
   break;
  case M1TP_OBS_COMPLETE:
   if(result!=M1TP_OBS_ENUM_OK || !valid_meta||!stored||rx!=stored)return -1;
   break;
  default:return -1;
  }
 }
 out->stage=stage;out->result=result;out->request_mask=mask;
 out->metadata_payload_bytes=m;out->descriptor_payload_bytes=rx;out->descriptor_bytes=stored;
 return 0;
}
static void emit_raw(FILE *f, const char *nonce, const char *key, const uint8_t *b, size_t n)
{
    static const char hex[] = "0123456789abcdef";
    unsigned char digest[32];
    char line[129], digest_hex[65];
#ifdef __APPLE__
    CC_SHA256(b, (CC_LONG)n, digest);
#else
    SHA256(b, n, digest);
#endif
    for (unsigned i = 0; i < 32; i++) { digest_hex[2 * i] = hex[digest[i] >> 4]; digest_hex[2 * i + 1] = hex[digest[i] & 15]; }
    digest_hex[64] = 0;
    fprintf(f, "DL14_TP_RECORD_BEGIN %s %s bytes=%zu chunk_bytes=64\n", nonce, key, n);
    size_t chunks = 0;
    for (size_t at = 0; at < n; at += 64) {
        size_t c = n - at < 64 ? n - at : 64;
        for (size_t i = 0; i < c; i++) { line[2 * i] = hex[b[at + i] >> 4]; line[2 * i + 1] = hex[b[at + i] & 15]; }
        line[2 * c] = 0;
        fprintf(f, "DL14_TP_RECORD_HEX %s %s index=%zu %s\n", nonce, key, chunks++, line);
    }
    fprintf(f, "DL14_TP_RECORD_END %s %s bytes=%zu chunks=%zu sha256=%s\n", nonce, key, n, chunks, digest_hex);
}

static const char *get_status(int g)
{
    return g == TPEC_GET_NO_ENTRY ? "no_entry" : g == TPEC_GET_NO_PROPERTY ? "no_property" : "error";
}

int tpec_run(int argc, char **argv, tpec_getter get, void *ctx, FILE *out)
{
    static uint8_t provider[TPEC_MAX_RAW], instance[TPEC_MAX_RAW];
    size_t plen = 0, ilen = 0;
    int32_t pdetail = 0, idetail = 0;
    struct tpec_summary sum;
    if (argc != 3 || strcmp(argv[1], "--nonce") || !tpec_nonce_valid(argv[2]) || !get || !out) {
        fputs("usage: darwin-trackpad-enum --nonce <48 lowercase hex>\n", stderr);
        return TPEC_EXIT_USAGE;
    }
    const char *nonce = argv[2];
    int pg = get(ctx, TPEC_PROVIDER, provider, sizeof(provider), &plen, &pdetail);
    if (pg == TPEC_GET_OK && plen > sizeof(provider)) pg = TPEC_GET_ERROR;
    if (pg == TPEC_GET_OK) emit_raw(out, nonce, PROVIDER_KEY, provider, plen);
    else fprintf(out, "DL14_TP_COPY %s %s status=%s detail=%d\n", nonce, PROVIDER_KEY, get_status(pg), (int)pdetail);
    const char *verdict, *inst = "not_read";
    int code = TPEC_EXIT_FAIL, wellformed = pg == TPEC_GET_OK && tpec_validate(provider, plen, &sum) == 0;
    if (pg != TPEC_GET_OK) verdict = pg == TPEC_GET_ERROR ? "MIG_ERROR" : "MISSING";
    else if (!wellformed) verdict = "MALFORMED";
    else {
        int ig = get(ctx, TPEC_INSTANCE, instance, sizeof(instance), &ilen, &idetail);
        if (ig == TPEC_GET_OK && ilen > sizeof(instance)) ig = TPEC_GET_ERROR;
        if (ig == TPEC_GET_OK) emit_raw(out, nonce, INSTANCE_KEY, instance, ilen);
        else if (ig != TPEC_GET_NO_ENTRY)
            fprintf(out, "DL14_TP_COPY %s %s status=%s detail=%d\n", nonce, INSTANCE_KEY, get_status(ig), (int)idetail);
        if (ig == TPEC_GET_NO_ENTRY) inst = "absent";
        else if (ig == TPEC_GET_OK && ilen == plen && !memcmp(instance, provider, plen)) inst = "equal";
        else inst = ig == TPEC_GET_OK ? "mismatch" : get_status(ig);
        if (strcmp(inst, "absent") && strcmp(inst, "equal")) verdict = "INSTANCE_MISMATCH";
        else if (sum.result == 0) { verdict = "ENUMERATION_CAPTURED"; code = TPEC_EXIT_CAPTURED; }
        else { verdict = "REFUSED"; code = TPEC_EXIT_REFUSED; }
    }
    if (wellformed)
        fprintf(out, "DL14_TP_VERDICT %s verdict=%s result=%d stage=%u keys=%u metadata_payload=%u descriptor_payload=%u descriptor_bytes=%u instance=%s input_proven=false feature_writes=0\n",
                nonce, verdict, (int)sum.result, sum.stage, sum.request_mask, sum.metadata_payload_bytes, sum.descriptor_payload_bytes, sum.descriptor_bytes, inst);
    else
        fprintf(out, "DL14_TP_VERDICT %s verdict=%s instance=%s input_proven=false feature_writes=0\n", nonce, verdict, inst);
    if (fflush(out) || ferror(out)) return TPEC_EXIT_FAIL;
    return code;
}
