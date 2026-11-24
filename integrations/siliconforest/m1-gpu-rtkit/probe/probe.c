// SPDX-License-Identifier: GPL-2.0
/*
 * probe.c - SFRtkitProbe mode 2, LinuxKPI side: the calls the Asahi GPU driver
 * makes into the RTKit library before it touches a GPU endpoint, and nothing
 * after them.
 *
 *   Linux (sources/gpu-plan/linux-aurora)                    here
 *   rust/kernel/soc/apple/rtkit.rs  RtKit::new()             apple_rtkit_init(dev, cookie, NULL, 0, &ops)
 *   drivers/gpu/drm/asahi/gpu.rs    GpuManager::init()       apple_rtkit_boot(rtk)
 *     rtk.start_endpoint(EP_FIRMWARE / EP_DOORBELL)          NOT done
 *     rtk.send_message(EP_FIRMWARE, MSG_INIT | initdata)     NOT done
 *
 * The ops are the ones rtkit.rs builds for the G13 GpuManager: flags 0,
 * crashed, recv_message, recv_message_early, shmem_setup, shmem_destroy. On
 * Linux shmem_setup allocates a GEM object and maps it into the firmware's
 * kernel address space through the UAT page tables (gpu.rs shmem_alloc). There
 * is no UAT here, so shmem_setup refuses: the firmware's buffer requests are
 * logged and left unanswered rather than answered with an address the
 * firmware cannot use.
 */
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/soc/apple/rtkit.h>

#include "sf_rtkit_compat.h"
#include "sf_rtkitprobe.h"

#define plog(fmt, ...)	sf_rtkit_printk(NULL, fmt, ##__VA_ARGS__)

struct probe_state {
	unsigned int buffer_requests;
	unsigned int app_messages;
	unsigned int crashes;
};

static struct probe_state probe_state;

static void probe_crashed(void *cookie, const void *crashlog, size_t crashlog_size)
{
	struct probe_state *st = cookie;

	st->crashes++;
	plog("ops.crashed: the coprocessor reported a crash (crashlog %zu bytes)", crashlog_size);
}

static void probe_recv_message(void *cookie, u8 endpoint, u64 message)
{
	struct probe_state *st = cookie;

	st->app_messages++;
	plog("ops.recv_message: endpoint 0x%02x message 0x%016llx (not expected: no application endpoint was started)",
	     endpoint, (unsigned long long)message);
}

/* rtkit.rs installs this for every client; its default returns false */
static bool probe_recv_message_early(void *cookie, u8 endpoint, u64 message)
{
	return false;
}

static int probe_shmem_setup(void *cookie, struct apple_rtkit_shmem *bfr)
{
	struct probe_state *st = cookie;

	st->buffer_requests++;
	plog("ops.shmem_setup: firmware asks for a 0x%zx byte buffer (iova 0x%llx): refused, no UAT mapping to give it",
	     bfr->size, (unsigned long long)bfr->iova);
	return -ENOMEM;
}

static void probe_shmem_destroy(void *cookie, struct apple_rtkit_shmem *bfr)
{
}

static const struct apple_rtkit_ops probe_ops = {
	.flags = 0,
	.crashed = probe_crashed,
	.recv_message = probe_recv_message,
	.recv_message_early = probe_recv_message_early,
	.shmem_setup = probe_shmem_setup,
	.shmem_destroy = probe_shmem_destroy,
};

int sf_rtkitprobe_rtkit_boot(void)
{
	struct apple_rtkit *rtk;
	struct device *dev;
	char eps[160];
	size_t n = 0;
	int ep, ret;

	sf_rtkit_enter();
	/* dev_dbg() of mailbox.c and rtkit.c: HELLO versions, endpoint map, power states */
	sf_rtkit_debug = 1;

	/* stands in for Linux's gpu@206400000 platform device; its mboxes = <&agx_mbox> */
	dev = sf_rtkit_device_create("gpu", NULL);
	if (!dev)
		return -ENOMEM;
	ret = sf_apple_mbox_bind(dev, 0, "/arm-io/gfx-asc");
	if (ret) {
		plog("step 4: could not bind the mailbox: %d", ret);
		return ret;
	}

	plog("step 4: apple_rtkit_init(dev, cookie, NULL, 0, ops), as rtkit.rs RtKit::new: mailbox interrupts on, receive armed");
	rtk = apple_rtkit_init(dev, &probe_state, NULL, 0, &probe_ops);
	if (IS_ERR(rtk)) {
		ret = PTR_ERR(rtk);
		plog("step 4: apple_rtkit_init failed: %d", ret);
		return ret;
	}
	plog("step 4: done");

	plog("step 5: apple_rtkit_boot(), as gpu.rs init(): wait 1 s each for the endpoint map and the IOP power ack, then set AP power ON");
	ret = apple_rtkit_boot(rtk);
	plog("step 5: apple_rtkit_boot returned %d; is_running %d, is_crashed %d", ret,
	     apple_rtkit_is_running(rtk), apple_rtkit_is_crashed(rtk));

	eps[0] = '\0';
	for (ep = 0; ep < 0x100 && n + 6 < sizeof(eps); ep++)
		if (apple_rtkit_has_endpoint(rtk, ep))
			n += snprintf(eps + n, sizeof(eps) - n, " 0x%02x", ep);
	plog("endpoints known (0x00 is always set; the rest come from the firmware's map):%s", eps);
	plog("buffer requests refused %u, application messages %u, crash reports %u",
	     probe_state.buffer_requests, probe_state.app_messages, probe_state.crashes);

	if (ret == 0)
		plog("RTKit reports ready (IOP and AP power states ON). Stopping here: endpoints 0x20/0x21 are NOT started, no initdata is sent");
	else
		plog("RTKit boot did not complete (%d). Stopping here", ret);
	plog("the RTKit instance stays attached: firmware syslog traffic is acknowledged, a crash is reported");
	return ret;
}

/*
 * ---- DCP mode (sfrtkitprobe=3), SF-H item H1 ----
 *
 * What Linux's display coprocessor driver does to the DCP's RTKit before any display
 * endpoint exists, and nothing after it. Read from the lab's pin, omacom/linux-aurora
 * 6a08a08e (sources/gpu-plan/linux-aurora), drivers/gpu/drm/apple/dcp.c,
 * dcp_comp_bind_pipe(), for a T8103 "apple,dcp" (hw data apple_dcp_hw_dcp, dcp.c:5144: no
 * adopt_live_session, default IOMFB profile, firmware compat 12.3/13.5):
 *
 *   Linux                                                      here
 *   CPU_CONTROL |= RUN (not adopt_live_session)  dcp.c:3979    not written: the caller stops unless RUN is already set
 *   apple_rtkit_init(dev, dcp, "mbox", 0, &rtkit_ops)   :3996  apple_rtkit_init(dev, cookie, NULL, 0, ops)
 *   apple_rtkit_wake(dcp->rtk)                          :4014  apple_rtkit_wake(rtk)
 *   schedule_work(&dcp->dimensions_wq)                  :4020  NOT done (DRM connector bookkeeping, iomfb.c:1431)
 *   dcp_start(): systemep, [ibootep, dptxep], iomfb     :2423  NOT done
 *
 * The other pair in that function, &external_h17p_rtkit_ops with
 * apple_rtkit_wake_early_ap() (:3993, :4012), is taken only for the
 * DCP_IOMFB_METHODS_H17P_EXTERNAL profile ("apple,t8140-dcpext", dcp.c:5121), not here.
 *
 * The ops are Linux's rtkit_ops for DCP (dcp.c:878): flags 0, crashed, recv_message,
 * shmem_setup, shmem_destroy, no recv_message_early. Linux's shmem_setup (dcp.c:817)
 * either maps a buffer the firmware placed itself (request carries an iova:
 * iommu_iova_to_phys() through the dcp DART, a contiguity check, memremap write-combined,
 * and RTKit sends no reply, rtkit.c:391) or allocates one through the dcp DART (iova 0:
 * dma_alloc_coherent, and RTKit replies with the new iova). There is no DART
 * driver here. Both kinds are refused, so the first puts nothing different on the wire
 * than Linux does and the second leaves the firmware without its reply: that request,
 * logged with endpoint, size and iova, is what H2 has to provide.
 */
#include <linux/spinlock.h>
#include <linux/soc/apple/mailbox.h>

#include "sf_dcp_dart.h"
/* mode 5: Linux's IOMMU/DMA API over the display DARTs (../../m1-dcp-dart) */
#include <linux/io.h>
#include <linux/mm.h>
#include "sf_dcp_iommu.h"

#define dlog(fmt, ...)	sf_rtkit_printk(NULL, "dcp " fmt, ##__VA_ARGS__)

/* received messages printed in full; after that only counted */
#define DCP_TRACE_MAX	96

struct dcp_probe_state {
	void (*orig_rx)(struct apple_mbox *mbox, struct apple_mbox_msg msg, void *cookie);
	unsigned int rx_total;
	unsigned int endpoints_announced;
	unsigned int buf_fw_placed;	/* request with an iova: no reply expected */
	unsigned int buf_need_alloc;	/* request without: the AP must allocate, map and reply */
	unsigned int shmem_calls;
	unsigned int app_messages;
	unsigned int crashes;
	/* 0: not served (mode 3); 1: through sf_dcp_dart directly (mode 4); 2: through the IOMMU/DMA API (mode 5) */
	int serve;
	struct device *dev;
	unsigned int served_fw_placed;
	unsigned int served_allocated;
};

static struct dcp_probe_state dcp_state;

/* rtkit.c system endpoints; drivers/gpu/drm/apple/dcp-internal.h:76-95 at the pin */
static const char *dcp_ep_name(unsigned int ep)
{
	switch (ep) {
	case 0x00: return "management";
	case 0x01: return "crashlog";
	case 0x02: return "syslog";
	case 0x03: return "debug";
	case 0x04: return "ioreport";
	case 0x08: return "oslog";
	case 0x0a: return "tracekit";
	case 0x20: return "system";
	case 0x21: return "test";
	case 0x22: return "dcp-expert";
	case 0x23: return "disp0/iBoot";
	case 0x24: return "dptx-controller/dpav-ctrl";
	case 0x28: return "dpavserv";
	case 0x29: return "av";
	case 0x2a: return "dptx";
	case 0x2b: return "hdcp";
	case 0x2c: return "dptx-v2";
	case 0x2d: return "remote-alloc";
	case 0x37: return "iomfb";
	default: return "unknown";
	}
}

static void dcp_note_buffer_request(unsigned int ep, u64 size, u64 iova)
{
	if (iova) {
		dcp_state.buf_fw_placed++;
		dlog("buffer-request: ep 0x%02x (%s) size 0x%llx iova 0x%llx: placed by the firmware; Linux maps it through the dcp DART and sends no reply; %s",
		     ep, dcp_ep_name(ep), (unsigned long long)size, (unsigned long long)iova,
		     dcp_state.serve ? "translating" : "not mapped here");
	} else {
		dcp_state.buf_need_alloc++;
		dlog("buffer-request: ep 0x%02x (%s) size 0x%llx iova 0: the AP must allocate, map through the dcp DART and reply; %s",
		     ep, dcp_ep_name(ep), (unsigned long long)size,
		     dcp_state.serve ? "allocating" : "NOT answered (needs H2)");
	}
}

/*
 * Sits between mailbox.c and rtkit.c's receive function: prints the message, then passes
 * it on unchanged. Runs on the mailbox interrupt thread with rx_lock held, like the
 * function it wraps. Field layouts are rtkit.c's (APPLE_RTKIT_MGMT_*, _BUFFER_REQUEST_*,
 * _OSLOG_*).
 */
static void dcp_rx_trace(struct apple_mbox *mbox, struct apple_mbox_msg msg, void *cookie)
{
	unsigned int ep = msg.msg1 & 0xff;
	u64 m = msg.msg0;
	unsigned int n = ++dcp_state.rx_total;
	unsigned int type = (m >> 52) & 0xff;
	unsigned int i;

	if (n <= DCP_TRACE_MAX)
		dlog("rx #%u: ep 0x%02x (%s) msg 0x%016llx", n, ep, dcp_ep_name(ep),
		     (unsigned long long)m);
	else if (n == DCP_TRACE_MAX + 1)
		dlog("rx: more than %d messages, the rest are only counted", DCP_TRACE_MAX);

	if (ep == 0x00 && type == 1) {
		dlog("hello: firmware speaks RTKit protocol versions %u..%u",
		     (unsigned int)(m & 0xffff), (unsigned int)((m >> 16) & 0xffff));
	} else if (ep == 0x00 && type == 8) {
		unsigned int base = (m >> 32) & 0x7;
		u32 bitmap = (u32)m;

		for (i = 0; i < 32; i++) {
			if (!(bitmap & (1u << i)))
				continue;
			dcp_state.endpoints_announced++;
			dlog("endpoint: 0x%02x (%s)", 32 * base + i, dcp_ep_name(32 * base + i));
		}
		if (m & (1ULL << 51))
			dlog("endpoint-map: complete, %u endpoints announced",
			     dcp_state.endpoints_announced);
	} else if (ep == 0x00 && type == 7) {
		dlog("power: IOP power state ack 0x%x", (unsigned int)(m & 0xffff));
	} else if (ep == 0x00 && type == 0xb) {
		dlog("power: AP power state ack 0x%x", (unsigned int)(m & 0xffff));
	} else if ((ep == 0x01 || ep == 0x02 || ep == 0x04) && type == 1) {
		dcp_note_buffer_request(ep, ((m >> 44) & 0xff) << 12, m & ((1ULL << 44) - 1));
	} else if (ep == 0x08 && ((m >> 56) & 0xff) == 1) {
		dcp_note_buffer_request(ep, (m >> 36) & 0xfffff, (m & ((1ULL << 36) - 1)) << 12);
	}

	if (dcp_state.orig_rx)
		dcp_state.orig_rx(mbox, msg, cookie);
}

static void dcp_crashed(void *cookie, const void *crashlog, size_t crashlog_size)
{
	struct dcp_probe_state *st = cookie;

	st->crashes++;
	dlog("CRASH: the coprocessor reported a crash (crashlog %zu bytes)", crashlog_size);
}

static void dcp_recv_message(void *cookie, u8 endpoint, u64 message)
{
	struct dcp_probe_state *st = cookie;

	st->app_messages++;
	dlog("app-message: endpoint 0x%02x (%s) message 0x%016llx; not answered (no endpoint above 0x1f was started)",
	     endpoint, dcp_ep_name(endpoint), (unsigned long long)message);
}

/* dcp.c:799-815 at the pin, unchanged */
static int dcp_check_iova_contiguous(struct iommu_domain *domain,
				     dma_addr_t iova, size_t size,
				     phys_addr_t phys)
{
	unsigned long pgsize = domain->pgsize_bitmap ?
			       1UL << __ffs(domain->pgsize_bitmap) : PAGE_SIZE;
	dma_addr_t end = iova + size;
	dma_addr_t addr = iova;

	while (addr < end) {
		if (iommu_iova_to_phys(domain, addr) != phys + (addr - iova))
			return -EINVAL;
		addr = ALIGN_DOWN(addr, pgsize) + pgsize;
	}

	return 0;
}

/*
 * Mode 5: dcp_rtk_shmem_setup() of the pin (dcp.c:817-866) with its calls as they are
 * there; sf_dcp_iommu.h turns them into the DART backend for the bound device. The log
 * lines replace Linux's dev_info().
 */
static int dcp_shmem_setup_iommu(struct dcp_probe_state *st, struct apple_rtkit_shmem *bfr)
{
	int ret;

	if (bfr->iova) {
		struct iommu_domain *domain = iommu_get_domain_for_dev(st->dev);
		phys_addr_t phy_addr;

		if (!domain)
			return -ENOMEM;

		phy_addr = iommu_iova_to_phys(domain, bfr->iova);
		if (!phy_addr)
			return -ENOMEM;

		/* memremap() below needs the whole buffer physically contiguous */
		ret = dcp_check_iova_contiguous(domain, bfr->iova, bfr->size, phy_addr);
		if (ret) {
			dlog("iommu: shmem_setup: iova 0x%llx (0x%zx bytes) is not physically contiguous",
			     (unsigned long long)bfr->iova, bfr->size);
			return ret;
		}

		/* Firmware writes without CPU cache maintenance. */
		bfr->buffer = memremap(phy_addr, bfr->size, MEMREMAP_WC);
		if (!bfr->buffer)
			return -ENOMEM;

		bfr->is_mapped = true;
		st->served_fw_placed++;
		dlog("iommu: shmem_setup: iommu_iova_to_phys(0x%llx) = 0x%llx, 0x%zx bytes contiguous, memremap -> %p; no reply",
		     (unsigned long long)bfr->iova, (unsigned long long)phy_addr, bfr->size, bfr->buffer);
	} else {
		bfr->buffer = dma_alloc_coherent(st->dev, bfr->size, &bfr->iova, GFP_KERNEL);
		if (!bfr->buffer)
			return -ENOMEM;

		st->served_allocated++;
		dlog("iommu: shmem_setup: dma_alloc_coherent(0x%zx) -> address 0x%llx, kernel %p, phys of first page 0x%llx; RTKit replies with the address",
		     bfr->size, (unsigned long long)bfr->iova, bfr->buffer,
		     (unsigned long long)iommu_iova_to_phys(iommu_get_domain_for_dev(st->dev), bfr->iova));
	}

	return 0;
}

/* dcp_rtk_shmem_destroy(), dcp.c:868-876 */
static void dcp_shmem_destroy_iommu(struct dcp_probe_state *st, struct apple_rtkit_shmem *bfr)
{
	if (bfr->is_mapped)
		memunmap(bfr->buffer);
	else
		dma_free_coherent(st->dev, bfr->size, bfr->buffer, bfr->iova);
}

/*
 * Mode 5, after the wake: the calls the driver makes later (AFK rings, shared memory,
 * framebuffer descriptors), once each, on the dcp stream. The addresses are never given to
 * the firmware.
 */
static int dcp_iommu_api_test(struct device *dev)
{
	struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
	const size_t size = 5 * PAGE_SIZE;
	struct sg_table sgt;
	struct scatterlist *sg;
	dma_addr_t dva = 0, res;
	phys_addr_t pa;
	u32 *buf;
	int i, bad = 0, ret;

	if (!domain || !iommu_is_dma_domain(domain) || iommu_device_use_default_domain(dev) ||
	    dma_set_mask_and_coherent(dev, DMA_BIT_MASK(42))) {
		dlog("iommu-test: no DMA domain for the dcp device");
		return -ENODEV;
	}

	/* several pages: separate pages made contiguous for the device, one kernel view */
	buf = dma_alloc_coherent(dev, size, &dva, GFP_KERNEL);
	if (!buf) {
		dlog("iommu-test: dma_alloc_coherent(0x%zx) failed", size);
		return -ENOMEM;
	}
	for (i = 0; i < 5; i++)
		buf[i * (PAGE_SIZE / 4)] = 0xdc900000u + i;
	ret = dma_get_sgtable(dev, &sgt, buf, dva, size);
	if (ret) {
		dlog("iommu-test: dma_get_sgtable failed: %d", ret);
		bad++;
	} else {
		for_each_sgtable_sg(&sgt, sg, i) {
			pa = iommu_iova_to_phys(domain, dva + (dma_addr_t)i * PAGE_SIZE);
			if (pa != sg_phys(sg) || *(u32 *)page_address(sg_page(sg)) != 0xdc900000u + i)
				bad++;
		}
		if (i != 5)
			bad++;
		sg_free_table(&sgt);
	}
	{
		/* what a user mapping of such a buffer is given (SFDRMDri's page resolver) */
		uint32_t pfn[5] = { 0 };

		if (sf_dma_buffer_pages(dev, buf, size, pfn))
			bad++;
		for (i = 0; i < 5; i++)
			if ((phys_addr_t)pfn[i] << PAGE_SHIFT != iommu_iova_to_phys(domain, dva + (dma_addr_t)i * PAGE_SIZE))
				bad++;
		if (sf_dma_buffer_pages(dev, buf + 1, size, pfn) != -ENOENT)
			bad++;
	}
	dlog("iommu-test: dma_alloc_coherent(0x%zx) -> address 0x%llx; 5 pages checked against dma_get_sgtable, sf_dma_buffer_pages and iommu_iova_to_phys: %s",
	     size, (unsigned long long)dva, bad ? "MISMATCH" : "ok");
	dma_free_coherent(dev, size, buf, dva);
	pa = iommu_iova_to_phys(domain, dva);
	dlog("iommu-test: dma_free_coherent; the address now translates to 0x%llx (%s)",
	     (unsigned long long)pa, pa ? "STILL MAPPED" : "unmapped, ok");
	if (pa)
		bad++;

	/* a physical range at a new address, as iomfb maps what the firmware names: one page of our own */
	struct page *pg = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (pg) {
		phys_addr_t phys = page_to_phys(pg) + 0x100;

		res = dma_map_resource(dev, phys, 0x200, DMA_BIDIRECTIONAL, 0);
		if (dma_mapping_error(dev, res)) {
			bad++;
			dlog("iommu-test: dma_map_resource failed");
		} else {
			pa = iommu_iova_to_phys(domain, res);
			dlog("iommu-test: dma_map_resource(phys 0x%llx) -> address 0x%llx, translates to 0x%llx: %s",
			     (unsigned long long)phys, (unsigned long long)res, (unsigned long long)pa,
			     pa == phys ? "ok" : "MISMATCH");
			if (pa != phys)
				bad++;
			dma_unmap_resource(dev, res, 0x200, DMA_BIDIRECTIONAL, 0);
			if (iommu_iova_to_phys(domain, res))
				bad++;
		}
		__free_page(pg);
	}
	dlog("iommu-test: %s", bad ? "FAILED" : "passed; everything it mapped is unmapped again");
	return bad ? -EIO : 0;
}

/* The request itself is printed, with its endpoint, by dcp_rx_trace(). */
static int dcp_shmem_setup(void *cookie, struct apple_rtkit_shmem *bfr)
{
	struct dcp_probe_state *st = cookie;

	st->shmem_calls++;
	if (!st->serve)
		return -ENOMEM;
	if (st->serve == 2)
		return dcp_shmem_setup_iommu(st, bfr);

	/*
	 * Linux dcp_rtk_shmem_setup(), dcp.c:817-866 at the pin. An error return leaves the
	 * request unanswered, exactly as mode 3 does.
	 */
	if (bfr->iova) {
		void *kva = sf_dcp_dart_map_fw(bfr->iova, bfr->size);

		if (!kva)
			return -ENOMEM;
		bfr->buffer = kva;
		bfr->is_mapped = true;
		st->served_fw_placed++;
		dlog("buffer-served: firmware-placed 0x%zx bytes at 0x%llx readable at kernel %p; RTKit sends no reply",
		     bfr->size, (unsigned long long)bfr->iova, kva);
	} else {
		u64 fw_addr = 0;
		void *kva = sf_dcp_dart_alloc(bfr->size, &fw_addr);

		if (!kva)
			return -ENOMEM;
		bfr->buffer = kva;
		bfr->iova = fw_addr;
		st->served_allocated++;
		dlog("buffer-served: allocated 0x%zx bytes, mapped and verified; RTKit now replies with address 0x%llx",
		     bfr->size, (unsigned long long)fw_addr);
	}
	return 0;
}

/* Reached only through apple_rtkit_quiesce() -> apple_rtkit_reinit() (exit mode 1). */
static void dcp_shmem_destroy(void *cookie, struct apple_rtkit_shmem *bfr)
{
	if (!dcp_state.serve || !bfr->buffer)
		return;
	if (dcp_state.serve == 2) {
		dcp_shmem_destroy_iommu(&dcp_state, bfr);
		return;
	}
	if (bfr->is_mapped)
		sf_dcp_dart_unmap_fw(bfr->buffer);
	else
		sf_dcp_dart_release(bfr->iova, bfr->size);
}

static const struct apple_rtkit_ops dcp_probe_ops = {
	.flags = 0,
	.crashed = dcp_crashed,
	.recv_message = dcp_recv_message,
	.shmem_setup = dcp_shmem_setup,
	.shmem_destroy = dcp_shmem_destroy,
};

int sf_rtkitprobe_dcp_wake(void)
{
	return sf_rtkitprobe_dcp_wake_serving(0, 0);
}

void sf_rtkitprobe_set_dva_mask(int on)
{
	sf_dcp_iommu_dva_mask = on != 0;
}

int sf_rtkitprobe_dcp_wake_serving(int serve, int quiesce)
{
	struct apple_rtkit *rtk;
	struct apple_mbox *mbox;
	struct device *dev;
	unsigned long flags;
	char eps[200];
	size_t n = 0;
	int ep, ret;

	sf_rtkit_enter();
	sf_rtkit_debug = 1;
	dcp_state.serve = serve;

	/* stands in for Linux's dcp@231c00000 platform device; its mboxes = <&dcp_mbox> */
	dev = sf_rtkit_device_create("dcp", NULL);
	if (!dev)
		return -ENOMEM;
	ret = sf_apple_mbox_bind(dev, 0, "/arm-io/dcp");
	if (ret) {
		dlog("rtkit-init: could not bind the mailbox: %d", ret);
		return ret;
	}
	dcp_state.dev = dev;
	if (serve == 2) {
		/*
		 * The tree walk the display driver's for_each_compatible_node() needs
		 * (../../m1-gpu-dt/kernel/sf_of_iter.c): every ASC wrapper of the device tree,
		 * once each, and an end.
		 */
		struct device_node *sf_of_find_compatible_node_from(struct device_node *, const char *, const char *);
		struct device_node *np = NULL;
		char names[160];
		size_t used = 0;
		int count = 0;

		names[0] = '\0';
		while ((np = sf_of_find_compatible_node_from(np, NULL, "iop,ascwrap-v4")) && count < 64) {
			if (used + 24 < sizeof(names))
				used += snprintf(names + used, sizeof(names) - used, " %s", np->name);
			count++;
		}
		dlog("of-iter: %d nodes compatible with iop,ascwrap-v4, each found once:%s%s", count, names,
		     count >= 64 ? " (STOPPED at 64: the walk did not end)" : "");

		/* Linux: iommus = <&dcp_dart 0 ...> on the dcp node, t8103.dtsi:663 */
		ret = sf_dcp_iommu_bind(dev, "/arm-io/dart-dcp", 0);
		if (ret) {
			dlog("iommu: the dcp device could not be bound to /arm-io/dart-dcp stream 0: %d; nothing was sent", ret);
			return ret;
		}
		dlog("iommu: dcp device bound to /arm-io/dart-dcp stream 0; addresses in %s form",
		     sf_dcp_iommu_dva_mask ? "m1n1's (| 0xf00000000)" : "Linux's (plain stream address)");
	}

	rtk = apple_rtkit_init(dev, &dcp_state, NULL, 0, &dcp_probe_ops);
	if (IS_ERR(rtk)) {
		ret = PTR_ERR(rtk);
		dlog("rtkit-init: apple_rtkit_init failed: %d", ret);
		return ret;
	}

	/* the same provider object apple_rtkit_init() attached to */
	mbox = apple_mbox_get(dev, 0);
	if (IS_ERR(mbox)) {
		ret = PTR_ERR(mbox);
		dlog("rtkit-init: mailbox lookup for the trace failed: %d; going no further", ret);
		return ret;
	}
	spin_lock_irqsave(&mbox->rx_lock, flags);
	dcp_state.orig_rx = mbox->rx;
	mbox->rx = dcp_rx_trace;
	spin_unlock_irqrestore(&mbox->rx_lock, flags);
	dlog("rtkit-init: ok, mailbox interrupts on, receive armed and traced; nothing sent yet");

	dlog("wake: apple_rtkit_wake(): send IOP power state 0x220, answer HELLO and the endpoint map, start the system endpoints (below 0x20), wait for the IOP power ack, set AP power 0x20");
	ret = apple_rtkit_wake(rtk);
	dlog("wake: apple_rtkit_wake returned %d; is_running %d, is_crashed %d", ret,
	     apple_rtkit_is_running(rtk), apple_rtkit_is_crashed(rtk));

	eps[0] = '\0';
	for (ep = 0; ep < 0x100 && n + 6 < sizeof(eps); ep++)
		if (apple_rtkit_has_endpoint(rtk, ep))
			n += snprintf(eps + n, sizeof(eps) - n, " 0x%02x", ep);
	dlog("endpoints:%s", eps);
	dlog("summary: %u messages received, %u endpoints announced, buffer requests: %u firmware-placed, %u needing an AP allocation (%u shmem_setup calls refused), %u application messages, %u crash reports",
	     dcp_state.rx_total, dcp_state.endpoints_announced, dcp_state.buf_fw_placed,
	     dcp_state.buf_need_alloc, dcp_state.shmem_calls, dcp_state.app_messages,
	     dcp_state.crashes);
	if (!serve)
		return ret;

	dlog("served: %u firmware-placed buffers mapped for reading, %u buffers allocated and mapped in the dcp DART",
	     dcp_state.served_fw_placed, dcp_state.served_allocated);
	if (serve == 2) {
		sf_dcp_iommu_report(dev, "after wake");
		if (!ret)
			dcp_iommu_api_test(dev);
		sf_dcp_iommu_report(dev, "after the API test");
	} else {
		sf_dcp_dart_check("after wake");
	}
	if (ret) {
		dlog("exit: wake did not complete (%d); nothing more is sent, the coprocessor is left as it is", ret);
		return ret;
	}
	if (!quiesce) {
		dlog("exit: left awake (IOP and AP power 0x20), as Linux leaves it between bind and dcp_start; RTKit stays attached and keeps serving syslog, oslog, ioreport and crashlog");
		return 0;
	}

	/*
	 * m1n1's hand-over state (src/dcp.c:309, src/rtkit.c:786-851): AP power 0x10, IOP power
	 * 0x10. apple_rtkit_quiesce() then reinitialises the instance, which releases the
	 * buffers through dcp_shmem_destroy(), as m1n1 unmaps its own after the quiesce.
	 */
	dlog("exit: apple_rtkit_quiesce(): AP power 0x10, IOP power 0x10, then buffers released");
	ret = apple_rtkit_quiesce(rtk);
	dlog("exit: apple_rtkit_quiesce returned %d; %u messages received in all", ret, dcp_state.rx_total);
	if (serve == 2)
		sf_dcp_iommu_report(dev, "after quiesce");
	else
		sf_dcp_dart_check("after quiesce");
	return ret;
}
