// SPDX-License-Identifier: MIT
// the openbsd autoconf, pci, bus_space, bus_dma and fdt interfaces bwfm(4) uses,
// carried out by SFBwfm.cpp and sf_bwfm_glue.c. Derived from SFQwx's sf_qwx_compat.h.
#ifndef SF_BWFM_COMPAT_H
#define SF_BWFM_COMPAT_H

#ifdef __cplusplus
extern "C" {
#endif

// every driver message carries this prefix, it is the device name bwfm prints
#define SF_BWFM_NAME		"SFBwfm"
// firmware, clm, txcap and nvram files live here, under their linux names
#define SF_BWFM_FIRMWARE_DIR	"/Library/Firmware/brcm"
// bwfm_pci_intr runs from the MSI and, as a safety net, from a timer every
// SF_BWFM_MSI_FALLBACK_POLL_US; without an MSI vector the timer alone drives it, every SF_BWFM_POLL_US
#define SF_BWFM_POLL_US		2000
#define SF_BWFM_MSI_FALLBACK_POLL_US	250000

// bwfm.c and if_bwfm_pci.c bring their own
#undef DPRINTF
#undef DPRINTFN
#undef DEVNAME

// autoconf

struct device {
	char		dv_xname[16];
};
struct cfattach {
	size_t		ca_devsize;
	int		(*ca_match)(struct device *, void *, void *);
	void		(*ca_attach)(struct device *, struct device *, void *);
	int		(*ca_detach)(struct device *, int);
	int		(*ca_activate)(struct device *, int);
};
struct cfdriver {
	void		**cd_devs;
	const char	*cd_name;
	int		cd_class;
};
#define DV_IFNET	1
#define DVACT_QUIESCE	2
#define DVACT_SUSPEND	3
#define DVACT_RESUME	4
#define DVACT_WAKEUP	5
#define DVACT_POWERDOWN	6
// SFBwfm runs the hook itself once the root file system is mounted
void	sf_bwfm_mountroot(struct device *, void (*)(struct device *));
#define config_mountroot(dev, fn)	sf_bwfm_mountroot((dev), (fn))

// registers, the handle is the kernel virtual address of the BAR mapping

typedef int		bus_space_tag_t;
typedef uintptr_t	bus_space_handle_t;
typedef uint64_t	bus_size_t;
typedef uint64_t	bus_addr_t;
typedef uint64_t	paddr_t;

static inline uint8_t
bus_space_read_1(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
	(void)t;
	return *(volatile uint8_t *)(h + o);
}

static inline uint16_t
bus_space_read_2(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
	(void)t;
	return OSSwapLittleToHostInt16(*(volatile uint16_t *)(h + o));
}

static inline uint32_t
bus_space_read_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
	(void)t;
	return OSSwapLittleToHostInt32(*(volatile uint32_t *)(h + o));
}

static inline void
bus_space_write_1(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o, uint8_t v)
{
	(void)t;
	*(volatile uint8_t *)(h + o) = v;
}

static inline void
bus_space_write_2(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o, uint16_t v)
{
	(void)t;
	*(volatile uint16_t *)(h + o) = OSSwapHostToLittleInt16(v);
}

static inline void
bus_space_write_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o, uint32_t v)
{
	(void)t;
	*(volatile uint32_t *)(h + o) = OSSwapHostToLittleInt32(v);
}

static inline void
bus_space_read_region_1(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o, uint8_t *buf, size_t n)
{
	while (n-- > 0)
		*buf++ = bus_space_read_1(t, h, o++);
}

static inline void
bus_space_write_region_1(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o, const uint8_t *buf, size_t n)
{
	while (n-- > 0)
		bus_space_write_1(t, h, o++, *buf++);
}

static inline int
bus_space_subregion(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o, bus_size_t size,
    bus_space_handle_t *nh)
{
	(void)t;
	(void)size;
	*nh = h + o;
	return 0;
}

// the IOMemoryMap behind a BAR stays with SFBwfm until its stop releases it, after the detach
#define bus_space_unmap(t, h, s)	((void)0)

// dma, by default (sf_dart_map.h): every device visible address is an iova the DART mapper
// handed out for exactly that buffer. bus_dmamem memory is wired kernel memory mapped for its
// lifetime, an mbuf is mapped in place (its own data pages, no copy) from bus_dmamap_load_mbuf
// to bus_dmamap_unload. A map only gets a slot, a mapped bounce buffer of its maximum size,
// the first time an mbuf cannot be mapped in place; see sf_bwfm_glue.c.
//
// dma with SF_BWFM_DMA_BOUNCE defined (sf_dart_pool.h): every device visible address is an
// iova inside the DART bounce pool. A map owns a pool slot of its maximum size. Transmit data
// is copied into the slot when the mbuf is loaded, a map created with BUS_DMA_READ is a
// receive buffer whose slot is copied into the loaded mbuf at BUS_DMASYNC_POSTREAD.

typedef struct sf_bwfm_dma *bus_dma_tag_t;

typedef struct {
	bus_addr_t	ds_addr;
	bus_size_t	ds_len;
	void		*_ds_mem;
} bus_dma_segment_t;

struct bus_dmamap {
	bus_size_t	dm_mapsize;
	int		dm_nsegs;
	int		_dm_flags;
	bus_size_t	_dm_size;
	void		*_dm_slot;
	uint64_t	_dm_slot_iova;
	struct mbuf	*_dm_mbuf;
#ifndef SF_BWFM_DMA_BOUNCE
	int		_dm_kind;	/* what is loaded, SF_BWFM_DMA_* in sf_bwfm_glue.c */
	uint64_t	_dm_dva;	/* an mbuf mapped in place: its device address */
#endif
	bus_dma_segment_t dm_segs[1];
};
typedef struct bus_dmamap *bus_dmamap_t;

#define BUS_DMA_WAITOK		0x0000
#define BUS_DMA_NOWAIT		0x0001
#define BUS_DMA_ALLOCNOW	0x0002
#define BUS_DMA_COHERENT	0x0004
#define BUS_DMA_ZERO		0x0008
#define BUS_DMA_64BIT		0x0010
#define BUS_DMA_READ		0x0200
#define BUS_DMA_WRITE		0x0400
#define BUS_DMASYNC_PREREAD	0x01
#define BUS_DMASYNC_POSTREAD	0x02
#define BUS_DMASYNC_PREWRITE	0x04
#define BUS_DMASYNC_POSTWRITE	0x08

int	bus_dmamap_create(bus_dma_tag_t, bus_size_t, int, bus_size_t, bus_size_t, int, bus_dmamap_t *);
void	bus_dmamap_destroy(bus_dma_tag_t, bus_dmamap_t);
int	bus_dmamap_load(bus_dma_tag_t, bus_dmamap_t, void *, bus_size_t, void *, int);
int	bus_dmamap_load_mbuf(bus_dma_tag_t, bus_dmamap_t, struct mbuf *, int);
void	bus_dmamap_unload(bus_dma_tag_t, bus_dmamap_t);
void	bus_dmamap_sync(bus_dma_tag_t, bus_dmamap_t, bus_addr_t, bus_size_t, int);
int	bus_dmamem_alloc(bus_dma_tag_t, bus_size_t, bus_size_t, bus_size_t, bus_dma_segment_t *, int, int *, int);
void	bus_dmamem_free(bus_dma_tag_t, bus_dma_segment_t *, int);
int	bus_dmamem_map(bus_dma_tag_t, bus_dma_segment_t *, int, size_t, caddr_t *, int);
void	bus_dmamem_unmap(bus_dma_tag_t, caddr_t, size_t);

// the packet id tables are sized to what the 16 MiB pool can back: every id owns a slot,
// 2 KiB for transmit and 8 KiB for receive. OpenBSD uses 2048 and 1024. The per-buffer
// mapper has no such limit; the sizes stay the same so the two can be compared.
#define BWFM_NUM_TX_PKTIDS	512
#define BWFM_NUM_RX_PKTIDS			640

// pci, the tag is the IOPCIDevice

typedef uint32_t	pcireg_t;
typedef void		*pci_chipset_tag_t;
typedef void		*pcitag_t;
typedef struct {
	int		ih_index;
} pci_intr_handle_t;

struct pci_attach_args {
	pci_chipset_tag_t pa_pc;
	pcitag_t	pa_tag;
	bus_dma_tag_t	pa_dmat;
	pcireg_t	pa_id;
};
struct pci_matchid {
	uint16_t	pm_vid;
	uint16_t	pm_pid;
};

#define PCI_VENDOR(id)		((id) & 0xffff)
#define PCI_PRODUCT(id)		(((id) >> 16) & 0xffff)
// from openbsd's pcidevs
#define PCI_VENDOR_BROADCOM		0x14e4
#define PCI_PRODUCT_BROADCOM_BCM4350	0x43a3
#define PCI_PRODUCT_BROADCOM_BCM4356	0x43ec
#define PCI_PRODUCT_BROADCOM_BCM43602	0x43ba
#define PCI_PRODUCT_BROADCOM_BCM4371	0x440d
#define PCI_PRODUCT_BROADCOM_BCM4378	0x4425
#define PCI_PRODUCT_BROADCOM_BCM4387	0x4433
#define PCI_MAPREG_START	0x10
#define PCI_MAPREG_TYPE_MEM	0x00000000
#define PCI_MAPREG_MEM_TYPE_64BIT	0x00000004

pcireg_t pci_conf_read(pci_chipset_tag_t, pcitag_t, int);
void	pci_conf_write(pci_chipset_tag_t, pcitag_t, int, pcireg_t);
int	pci_mapreg_map(struct pci_attach_args *, int, pcireg_t, int, bus_space_tag_t *,
	    bus_space_handle_t *, bus_addr_t *, bus_size_t *, bus_size_t);
int	pci_matchbyid(struct pci_attach_args *, const struct pci_matchid *, int);
int	pci_intr_map_msi(struct pci_attach_args *, pci_intr_handle_t *);
int	pci_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
const char *pci_intr_string(pci_chipset_tag_t, pci_intr_handle_t);
void	*pci_intr_establish(pci_chipset_tag_t, pci_intr_handle_t, int, int (*)(void *), void *, const char *);
void	pci_intr_disestablish(pci_chipset_tag_t, void *);

// the device tree: there is no fdt on Darwin. bwfm asks two nodes for properties, the root
// for the board and the pci function for what m1n1 copies out of the Apple device tree on
// linux. sf_bwfm_glue.c answers both, see sf_bwfm_board_identity() for the hard-coded part.

#define __HAVE_FDT		1
#define SF_BWFM_NODE_WLAN	1
#define SF_BWFM_NODE_ROOT	2
#define PCITAG_NODE(tag)	SF_BWFM_NODE_WLAN
#define OF_peer(node)		SF_BWFM_NODE_ROOT
int	OF_getproplen(int, const char *);
int	OF_getprop(int, const char *, void *, int);

// raw properties of the Apple device tree, read from the IORegistry by SFBwfm.cpp.
// Returns the property length, or -1 when the node or the property is missing.
int	sf_bwfm_adt_prop(const char *path, const char *name, void *buf, int len);
// boot-args that override the hard-coded board identity, 0 when absent
int	sf_bwfm_bootarg(const char *name, char *buf, int len);
// 1 when the firmware supplicant should do the WPA2-PSK handshake (boot-arg bwfm_fwsup=0 turns it off)
int	sf_bwfm_want_fwsup(void);
// 1 when the firmware's deep sleep request is acknowledged as brcmfmac does (boot-arg bwfm_dsack=1; default 0)
int	sf_bwfm_want_dsack(void);
// the station's power save mode, BWFM_PM_* (boot-arg bwfm_pm, default 2: fast power save)
int	sf_bwfm_want_pm(void);
// ms the firmware stays awake after the last frame in fast power save (boot-arg bwfm_pm2_ret,
// default 2000; 0 leaves the firmware's own 200 ms)
int	sf_bwfm_want_pm2_ret(void);

// the rest of the kernel bwfm touches

#define le16toh(x)	OSSwapLittleToHostInt16(x)
#define le32toh(x)	OSSwapLittleToHostInt32(x)
#define le64toh(x)	OSSwapLittleToHostInt64(x)
#define htolem32(p, v)	(*(uint32_t *)(p) = htole32(v))

#define PWAIT		32
#define taskq_create(n, t, ipl, f)	((struct taskq *)systq)
#define taskq_destroy(tq)	((void)0)
#define taskq_barrier(tq)	((void)0)

// SFBwfm.cpp has the one from <net/ethernet.h>, the compat layer of the C files has none
#ifndef __cplusplus
struct ether_addr {
	uint8_t		ether_addr_octet[ETHER_ADDR_LEN];
};
#endif

// receive ring accounting, without openbsd's livelock back off
struct if_rxring {
	u_int		rxr_alive;
	u_int		rxr_hwm;
};

static inline void
if_rxr_init(struct if_rxring *rxr, u_int lwm, u_int hwm)
{
	(void)lwm;
	rxr->rxr_alive = 0;
	rxr->rxr_hwm = hwm;
}

static inline u_int
if_rxr_get(struct if_rxring *rxr, u_int max)
{
	u_int n = rxr->rxr_hwm - rxr->rxr_alive;

	if (n > max)
		n = max;
	rxr->rxr_alive += n;
	return n;
}

static inline void
if_rxr_put(struct if_rxring *rxr, u_int n)
{
	rxr->rxr_alive -= n;
}

#define ifq_restart(q)	do { ifq_clr_oactive(q); if_start((q)->ifq_if); } while (0)

// one contiguous buffer of the asked size, the receive paths need that up to 8 KiB
struct mbuf *sf_bwfm_mclgetl(int, u_int);
#undef MCLGETL
#define MCLGETL(m, how, l)	sf_bwfm_mclgetl((how), (l))

// bwfm_lifecycle_enter remembers which thread is inside
#include <kern/thread.h>

int	loadfirmware(const char *, u_char **, size_t *);
int	sf_bwfm_m_defrag(struct mbuf **, int);
#define m_defrag(m, how)	sf_bwfm_m_defrag(&(m), (how))
struct ifnet;
int	if_setlladdr(struct ifnet *, const uint8_t *);

#ifdef __cplusplus
}
#endif

/* Driver messages. Define SF_BWFM_SLOW_LOG to follow each with a short sleep so the
 * serial console drains before a possible hard stop (useful when chasing an SError). */
int sf_bwfm_printf(const char *fmt, ...) __printflike(1, 2);
/* log the first n occurrences of something on the data path */
#define SF_LOGN(n, ...) do { static int sf_logn_; if (sf_logn_ < (n)) { sf_logn_++; sf_bwfm_printf(__VA_ARGS__); } } while (0)
#ifndef SF_BWFM_NO_PRINTF_WRAP
#undef printf
#define printf(...) sf_bwfm_printf(__VA_ARGS__)
#endif

#endif
