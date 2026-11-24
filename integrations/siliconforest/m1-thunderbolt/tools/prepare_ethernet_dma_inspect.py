#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Inspect only owned DMA at the existing finite I210 doorbell trace sites."""
from pathlib import Path
import sys

root = Path(sys.argv[1])
def replace(path, old, new):
    text = path.read_text()
    if text.count(old) != 1:
        raise SystemExit('I210 DMA inspect source drift: ' + old[:80])
    path.write_text(text.replace(old, new))

p = root / 'src/Kernel/Extensions/SFFBSDem/dev/e1000/igb_txrx.c'
replace(p, 'if (sf_i210_trace_take(&sf_trace_flush))\n',
        'if (sf_i210_trace_take(&sf_trace_flush)) {\n')
replace(p, 'E1000_READ_REG(&sc->hw,E1000_RCTL));',
        'E1000_READ_REG(&sc->hw,E1000_RCTL));\n        sf_i210_dma_trace();\n    }')
replace(p, '        sf_i210_dma_trace();', '''        sf_i210_trace_log("SFI210IRQMASK ims=%08x eims=%08x eiac=%08x eiam=%08x gpie=%08x ivar0=%08x ivarmisc=%08x ctrl=%08x ctrlext=%08x tipg=%08x dtxswc=%08x txswc=%08x\\n",
            E1000_READ_REG(&sc->hw,E1000_IMS), E1000_READ_REG(&sc->hw,E1000_EIMS),
            E1000_READ_REG(&sc->hw,E1000_EIAC), E1000_READ_REG(&sc->hw,E1000_EIAM),
            E1000_READ_REG(&sc->hw,E1000_GPIE), E1000_READ_REG(&sc->hw,E1000_IVAR0),
            E1000_READ_REG(&sc->hw,E1000_IVAR_MISC), E1000_READ_REG(&sc->hw,E1000_CTRL),
            E1000_READ_REG(&sc->hw,E1000_CTRL_EXT), E1000_READ_REG(&sc->hw,E1000_TIPG),
            E1000_READ_REG(&sc->hw,E1000_DTXSWC), E1000_READ_REG(&sc->hw,E1000_TXSWC));
        sf_i210_dma_trace();''')

p = root / 'src/Kernel/fbsdkpi/xnu/sf_fbsd_os.cpp'
replace(p, '#include "sf_pci_dma_retire.h"',
        '#include "sf_pci_dma_retire.h"\n#include "sf_i210_trace.h"\n#include "sf_pciec_dma_inspect.h"')
replace(p, 'static const OSSymbol *sf_i210_access_key;', r'''static const OSSymbol *sf_i210_access_key;
extern "C" void sf_i210_dma_trace(void)
{
 // Called only inside the driver's existing saturating eight-flush budget.
 if(!sf_i210_device || ml_at_interrupt_context())return;
 if(!sf_i210_host || !sf_i210_access_key ||
    sf_i210_host->callPlatformFunction(sf_i210_access_key,false,sf_i210_device,NULL,NULL,NULL)!=kIOReturnSuccess)return;
 // Configuration reads take their normal native host lock, outside mapper locks.
 auto endpoint=OSDynamicCast(IOPCIDevice,sf_i210_device);
 if(!endpoint)return;
 sf_i210_trace_log("SFI210PCI endpoint b=%u d=%u f=%u id=%08x commandstatus=%08x bar0=%08x bar1=%08x\n",
  endpoint->getBusNumber(),endpoint->getDeviceNumber(),endpoint->getFunctionNumber(),
  endpoint->configRead32(0),endpoint->configRead32(4),endpoint->configRead32(0x10),endpoint->configRead32(0x14));
 unsigned cap=endpoint->configRead8(0x34)&0xfc;
 for(unsigned n=0;cap>=0x40 && cap<=0xfc && n<48;++n){
  unsigned header=endpoint->configRead16(cap),next=(header>>8)&0xfc;
  if((header&0xff)==5 && cap<=0xec){
   sf_i210_trace_log("SFI210PCI msi cap=%x flags=%04x addrlo=%08x addrhi_or_data=%08x data_or_mask=%08x\n",
    cap,endpoint->configRead16(cap+2),endpoint->configRead32(cap+4),
    endpoint->configRead32(cap+8),endpoint->configRead32(cap+12));break;
  }
  if(next==cap)break;cap=next;
 }
 IORegistryEntry *parent=endpoint->getParentEntry(gIOServicePlane);
 for(unsigned n=0;parent && n<64;++n,parent=parent->getParentEntry(gIOServicePlane)){
  auto bridge=OSDynamicCast(IOPCIDevice,parent);
  if(!bridge || (bridge->configRead32(8)>>16)!=0x0604)continue;
  sf_i210_trace_log("SFI210PCI bridge b=%u d=%u f=%u id=%08x commandstatus=%08x buses=%08x memory=%08x prefetch=%08x\n",
   bridge->getBusNumber(),bridge->getDeviceNumber(),bridge->getFunctionNumber(),
   bridge->configRead32(0),bridge->configRead32(4),bridge->configRead32(0x18),
   bridge->configRead32(0x20),bridge->configRead32(0x24));
 }
 auto mapper=IOMapper::copyMapperForDevice(sf_i210_device);
 if(!mapper)return;
 sf_pciec_dma_inspect s={};s.version=SF_PCIEC_DMA_INSPECT_VERSION;s.size=sizeof(s);
 IOReturn result=kIOReturnUnsupported;
 if(mapper->metaCast("SFCIOPCIDartMapper"))
  result=mapper->callPlatformFunction(SF_PCIEC_DMA_INSPECT_FUNCTION,false,sf_i210_device,&s,NULL,NULL);
 mapper->release();
 sf_i210_trace_log("SFI210DMA snapshot result=%x maps=%u tables=%u root=%llx rootpar=%llx cmd=%08x error=%08x faultlo=%08x faulthi=%08x config=%08x enable=%08x tcr0=%08x ttbr0=%08x ttbr1=%08x ttbr2=%08x ttbr3=%08x tcr1=%08x\n",
  (unsigned)result,s.mapping_count,s.table_count,
  (unsigned long long)s.root_physical,(unsigned long long)s.root_cpu_par,
  s.regs[0],s.regs[1],s.regs[2],s.regs[3],s.regs[4],s.regs[5],s.regs[6],
  s.regs[7],s.regs[8],s.regs[9],s.regs[10],s.regs[11]);
 if(result!=kIOReturnSuccess)return;
 for(unsigned n=0;n<64;++n)if(s.rid2sid[n] || s.tcr[n])
  sf_i210_trace_log("SFI210DMA stream n=%u rid2sid=%08x tcr=%08x\n",n,s.rid2sid[n],s.tcr[n]);
 for(unsigned n=0;n<SF_PCIEC_DMA_INSPECT_SAMPLES;++n){
  const auto &m=s.sample[n];
  sf_i210_trace_log("SFI210DMA sample n=%u slot=%u iova=%llx bytes=%llx pa=%llx span=%llx walkerr=%d walked=%llx l1e=%llx pte=%llx cpupar=%llx\n",
   n,m.slot,(unsigned long long)m.iova,(unsigned long long)m.bytes,
   (unsigned long long)m.physical,(unsigned long long)m.span,m.walk_error,
   (unsigned long long)m.walked,(unsigned long long)m.l1e,
   (unsigned long long)m.pte,(unsigned long long)m.cpu_par);
  sf_i210_trace_log("SFI210DMA words n=%u w0=%016llx w1=%016llx w2=%016llx w3=%016llx\n",n,
   (unsigned long long)m.words[0],(unsigned long long)m.words[1],
   (unsigned long long)m.words[2],(unsigned long long)m.words[3]);
 }
}''')
