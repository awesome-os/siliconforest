#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Add bounded evidence to the private I210 source, retaining upstream code.

No descriptor, MMIO write, polling cadence or driver policy is changed.
Every register read still passes through the existing retained access guard.
"""
from pathlib import Path
import sys

root = Path(sys.argv[1])
p = root / 'src/Kernel/Extensions/SFFBSDem/dev/e1000/igb_txrx.c'
s = p.read_text()

def replace(old, new):
    global s
    if s.count(old) != 1:
        raise SystemExit('I210 trace source drift: ' + old[:80])
    s = s.replace(old, new)

replace('static int igb_isc_txd_encap(void *, if_pkt_info_t);', '''#include "sf_i210_trace.h"
static uint32_t sf_trace_encap, sf_trace_flush, sf_trace_credit, sf_trace_rx;
static int igb_isc_txd_encap(void *, if_pkt_info_t);''')
replace('\t++txr->tx_packets;', '''\t++txr->tx_packets;
    if (sf_i210_trace_take(&sf_trace_encap))
        printf("SFI210TRACE encap q=%u len=%u segs=%d old=%u new=%u dma=%llx cmd=%08x olinfo=%08x rs=%u\\n",
            (unsigned)pi->ipi_qsidx, (unsigned)pi->ipi_len, nsegs,
            (unsigned)pi->ipi_pidx, (unsigned)pi->ipi_new_pidx,
            (unsigned long long)le64toh(txd->read.buffer_addr),
            (unsigned)le32toh(txd->read.cmd_type_len),
            (unsigned)le32toh(txd->read.olinfo_status), (unsigned)txr->tx_rs_pidx);''')
replace('\tE1000_WRITE_REG(&sc->hw, E1000_TDT(txr->me), pidx);', '''\tE1000_WRITE_REG(&sc->hw, E1000_TDT(txr->me), pidx);
    if (sf_i210_trace_take(&sf_trace_flush))
        printf("SFI210TRACE doorbell q=%u requested=%u tdh=%08x tdt=%08x tdbal=%08x tdbah=%08x tdlen=%08x txdctl=%08x tctl=%08x status=%08x rdh=%08x rdt=%08x rdbal=%08x rdbah=%08x rdlen=%08x rxdctl=%08x rctl=%08x\\n",
            (unsigned)txqid, (unsigned)pidx,
            E1000_READ_REG(&sc->hw,E1000_TDH(txr->me)),
            E1000_READ_REG(&sc->hw,E1000_TDT(txr->me)),
            E1000_READ_REG(&sc->hw,E1000_TDBAL(txr->me)),
            E1000_READ_REG(&sc->hw,E1000_TDBAH(txr->me)),
            E1000_READ_REG(&sc->hw,E1000_TDLEN(txr->me)),
            E1000_READ_REG(&sc->hw,E1000_TXDCTL(txr->me)),
            E1000_READ_REG(&sc->hw,E1000_TCTL),
            E1000_READ_REG(&sc->hw,E1000_STATUS),
            E1000_READ_REG(&sc->hw,E1000_RDH(0)),
            E1000_READ_REG(&sc->hw,E1000_RDT(0)),
            E1000_READ_REG(&sc->hw,E1000_RDBAL(0)),
            E1000_READ_REG(&sc->hw,E1000_RDBAH(0)),
            E1000_READ_REG(&sc->hw,E1000_RDLEN(0)),
            E1000_READ_REG(&sc->hw,E1000_RXDCTL(0)),
            E1000_READ_REG(&sc->hw,E1000_RCTL));''')
replace('\tupdated = !!(status & E1000_TXD_STAT_DD);', '''\tupdated = !!(status & E1000_TXD_STAT_DD);
    if (sf_i210_trace_take(&sf_trace_credit))
        printf("SFI210TRACE credit q=%u descriptor=%u status=%02x tdh=%08x tdt=%08x\\n",
            (unsigned)txqid, (unsigned)cur, (unsigned)status,
            E1000_READ_REG(&sc->hw,E1000_TDH(txr->me)),
            E1000_READ_REG(&sc->hw,E1000_TDT(txr->me)));''')
# RX queries already occur in the real work path. This adds neither a timer
# nor a read of destructive interrupt/statistics registers.
anchor = 'igb_isc_rxd_available(void *arg, uint16_t rxqid, qidx_t idx, qidx_t budget)'
start = s.index(anchor)
brace = s.index('{', start)
s = s[:brace+1] + '''
    if (sf_i210_trace_take(&sf_trace_rx)) {
        struct e1000_softc *diagnostic_sc = arg;
        printf("SFI210TRACE rx-query q=%u idx=%u rdh=%08x rdt=%08x\\n",
            (unsigned)rxqid, (unsigned)idx,
            E1000_READ_REG(&diagnostic_sc->hw,E1000_RDH(rxqid)),
            E1000_READ_REG(&diagnostic_sc->hw,E1000_RDT(rxqid)));
    }
''' + s[brace+1:]
p.write_text(s)
