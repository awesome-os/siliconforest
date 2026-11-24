/* SPDX-License-Identifier: MIT */
/* Read actual native PCI registry ancestry and public IDs only. This neither
 * probes config space nor authorizes a router or establishes device function.
 * MACs, UIDs, serials, DROM, resources and firmware bytes are never printed. */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <stdint.h>
static int number(io_registry_entry_t entry,CFStringRef key,uint32_t *out)
{
 CFTypeRef value=IORegistryEntryCreateCFProperty(entry,key,kCFAllocatorDefault,0);
 int ok=0;
 if(value && CFGetTypeID(value)==CFDataGetTypeID() && CFDataGetLength(value)>=4){
  const UInt8 *p=CFDataGetBytePtr(value);
  *out=(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;ok=1;
 }else if(value && CFGetTypeID(value)==CFNumberGetTypeID())
  ok=CFNumberGetValue(value,kCFNumberSInt32Type,out);
 if(value)CFRelease(value);return ok;
}
static int under_host(io_registry_entry_t entry,io_registry_entry_t root)
{
 io_registry_entry_t current=entry;IOObjectRetain(current);
 for(unsigned depth=0;depth<64;++depth){
  if(IOObjectConformsTo(current,"SFCIOPCIHost")){IOObjectRelease(current);return 1;}
  if(IOObjectIsEqualTo(current,root)){IOObjectRelease(current);return 0;}
  io_registry_entry_t parent=0;
  kern_return_t ret=IORegistryEntryGetParentEntry(current,kIOServicePlane,&parent);
  IOObjectRelease(current);if(ret!=KERN_SUCCESS || !parent)return -1;
  current=parent;
 }
 IOObjectRelease(current);return -1;
}
int main(int argc,char **argv)
{
 (void)argv;if(argc!=1){fprintf(stderr,"usage: sf-cio-pci-inventory\n");return 1;}
 io_registry_entry_t root=IORegistryGetRootEntry(kIOMasterPortDefault);
 CFMutableDictionaryRef match=IOServiceMatching("IOPCIDevice");
 if(!root || !match){if(root)IOObjectRelease(root);if(match)CFRelease(match);return 1;}
 io_iterator_t iterator=0;
 kern_return_t ret=IOServiceGetMatchingServices(kIOMasterPortDefault,match,&iterator);
 if(ret!=KERN_SUCCESS){IOObjectRelease(root);fprintf(stderr,"PCI registry query failed: 0x%08x\n",ret);return 1;}
 unsigned scanned=0,found=0,xhci=0,i210=0;int error=0;io_service_t entry;
 while((entry=IOIteratorNext(iterator))){
  if(++scanned>256){IOObjectRelease(entry);error=1;break;}
  int owned=under_host(entry,root);
  if(owned<0){IOObjectRelease(entry);error=1;break;}
  if(owned){
   uint32_t vendor,device,code,address;
   if(!number(entry,CFSTR("vendor-id"),&vendor) ||
      !number(entry,CFSTR("device-id"),&device) ||
      !number(entry,CFSTR("class-code"),&code) ||
      !number(entry,CFSTR("reg"),&address)){
    IOObjectRelease(entry);error=1;break;
   }
   printf("%02x:%02x.%x %04x:%04x class=%06x\n",(address>>16)&255,
    (address>>11)&31,(address>>8)&7,vendor&65535,device&65535,code&0xffffff);
   ++found;xhci+=(code&0xffffff)==0x0c0330;
   i210+=(vendor&65535)==0x8086 && (device&65535)==0x1533;
  }
  IOObjectRelease(entry);
 }
 if(!IOIteratorIsValid(iterator))error=1;
 IOObjectRelease(iterator);IOObjectRelease(root);
 if(error){fprintf(stderr,"incomplete PCI ancestry/property inventory; no acceptance result\n");return 1;}
 printf("native-tunnel-pci-functions=%u xhci=%u i210=%u (registry presence only)\n",found,xhci,i210);
 /* A root bridge or a partial subtree is not the dock acceptance set. */
 return xhci==4 && i210==1?0:2;
}
