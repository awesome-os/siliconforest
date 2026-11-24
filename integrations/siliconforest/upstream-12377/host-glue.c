/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
typedef unsigned device_t;
enum { PCIR_COMMAND=4,PCIM_CMD_BUSMASTEREN=4,PCIM_CMD_PORTEN=1,PCIM_CMD_MEMEN=2,SYS_RES_IOPORT=1,PCIY_EXPRESS=16 };
static uint16_t command,control;static bool cap;
static unsigned pci_read_config(device_t dev,unsigned offset,int width){assert(dev==1);if(offset==4){assert(width==2);return command;}if(offset==0x108)return control;if(offset==0xb)return 2;if(offset==0xa)return 0;assert(0);return 0;}
static void pci_write_config(device_t dev,unsigned offset,unsigned value,int width){assert(dev==1&&offset==4&&width==2);command=value;}
static int pci_find_cap(device_t dev,int type,int *out){assert(dev==1&&type==16);*out=0x100;return cap?0:1;}
@GLUE@
int main(void){
 command=0xffff;assert(!pci_disable_busmaster(1)&&command==0xfffb);assert(!pci_disable_io(1,SYS_RES_IOPORT)&&command==0xfffa);assert(!pci_disable_io(1,0)&&command==0xfff8);
 assert(!pci_get_max_payload(1));cap=true;for(unsigned n=0;n<6;n++){control=n<<5;assert(pci_get_max_payload(1)==(128<<n));}
 assert(pci_get_class(1)==2&&!pci_get_subclass(1));assert(!pci_has_pme(1,3));pci_save_state(1);pci_restore_state(1);pci_clear_pme(1);pci_enable_pme(1);
 puts("PASS actual shared PCI helpers: command-bit preservation, payload, class, explicit unsupported PME");
 puts("LIMIT no TX/RX, DMA sync, MSI or I210 packet proof; shared glue does not change those paths");
}
