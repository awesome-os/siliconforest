/* SPDX-License-Identifier: MIT */
/* Request normal IONetworkStack naming only after the actual I210 driver
 * published its Ethernet interface. No PCI/DMA grant, interface-up or DHCP. */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOReturn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <grp.h>
static CFTypeRef property(io_registry_entry_t e,const char *key)
{
 CFStringRef name=CFStringCreateWithCString(NULL,key,kCFStringEncodingUTF8);
 if(!name)return NULL;
 CFTypeRef v=IORegistryEntryCreateCFProperty(e,name,kCFAllocatorDefault,0);
 CFRelease(name);return v;
}
static int data32(io_registry_entry_t e,const char *key,uint32_t expected)
{
 CFTypeRef v=property(e,key);uint32_t n=0;
 int ok=v && CFGetTypeID(v)==CFDataGetTypeID() && CFDataGetLength((CFDataRef)v)==4;
 if(ok)memcpy(&n,CFDataGetBytePtr((CFDataRef)v),4);
 if(v)CFRelease(v);return ok && n==expected;
}
static int true_property(io_registry_entry_t e,const char *key)
{
 CFTypeRef v=property(e,key);int ok=v && CFGetTypeID(v)==CFBooleanGetTypeID() && CFBooleanGetValue((CFBooleanRef)v);
 if(v)CFRelease(v);return ok;
}
static int bsd_name(io_registry_entry_t e,char name[16])
{
 CFTypeRef v=property(e,"BSD Name");
 int ok=v && CFGetTypeID(v)==CFStringGetTypeID() && CFStringGetCString((CFStringRef)v,name,16,kCFStringEncodingUTF8);
 if(v)CFRelease(v);
 if(!ok || name[0]!='e' || name[1]!='n' || !name[2])return 0;
 for(unsigned n=2;name[n];++n)if(!isdigit((unsigned char)name[n]))return 0;
 return strcmp(name,"en0")!=0;
}
int main(int argc,char **argv)
{
 int deny=argc==2 && !strcmp(argv[1],"--deny-unprivileged");
 if(argc!=1 && !deny){fprintf(stderr,"usage: sf-dock-register-interface [--deny-unprivileged]\n");return 2;}
 if(deny && !geteuid()){
  if(setgroups(0,NULL) || setgid(65534) || setuid(65534) || !geteuid()){perror("drop privileges");return 1;}
 }
 if(geteuid()){
  fprintf(stderr,"root required for dock interface registration (actual uid=%u); no request sent\n",(unsigned)geteuid());
  return deny?0:1;
 }
 io_iterator_t drivers=0;
 kern_return_t error=IOServiceGetMatchingServices(kIOMasterPortDefault,IOServiceMatching("SFFBSDem"),&drivers);
 if(error || !drivers){fprintf(stderr,"I210 driver iterator unavailable\n");return 1;}
 io_service_t driver=IOIteratorNext(drivers),extra=IOIteratorNext(drivers);
 IOObjectRelease(drivers);
 if(!driver || extra){if(driver)IOObjectRelease(driver);if(extra)IOObjectRelease(extra);fprintf(stderr,"I210 driver is not unique\n");return 1;}
 io_registry_entry_t pci=0;
 int ok=IORegistryEntryGetParentEntry(driver,kIOServicePlane,&pci)==KERN_SUCCESS &&
  data32(pci,"vendor-id",0x8086) && data32(pci,"device-id",0x1533) &&
  data32(pci,"class-code",0x020000) && true_property(pci,"SFCIOI210BusMasterAdmitted");
 if(pci)IOObjectRelease(pci);
 if(!ok){IOObjectRelease(driver);fprintf(stderr,"actual I210 identity/admission unavailable\n");return 1;}
 io_iterator_t children=0;io_registry_entry_t interface=0;
 error=IORegistryEntryGetChildIterator(driver,kIOServicePlane,&children);
 if(!error && children)for(io_registry_entry_t child;(child=IOIteratorNext(children));){
  CFTypeRef type=property(child,"IOInterfaceType");int n=-1;
  int candidate=type && CFGetTypeID(type)==CFNumberGetTypeID() && CFNumberGetValue((CFNumberRef)type,kCFNumberIntType,&n) && n==6;
  if(type)CFRelease(type);
  if(candidate && interface){IOObjectRelease(child);IOObjectRelease(interface);interface=0;error=kIOReturnBadArgument;break;}
  if(candidate)interface=child;else IOObjectRelease(child);
 }
 if(children)IOObjectRelease(children);IOObjectRelease(driver);
 if(error || !interface){fprintf(stderr,"actual I210 Ethernet interface is not unique\n");return 1;}
 char name[16]={};
 if(!bsd_name(interface,name)){
  // Same documented RegisterInterfaceAll request used by the working Wi-Fi
  // driver in this system without InterfaceNamer. Already-named interfaces
  // keep their names. This does not select a fixed unit or change addresses.
  io_service_t stack=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("IONetworkStack"));
  int command=2;CFNumberRef number=CFNumberCreate(NULL,kCFNumberIntType,&command);
  error=stack && number?IORegistryEntrySetCFProperty(stack,CFSTR("IONetworkStackUserCommand"),number):kIOReturnNotFound;
  if(number)CFRelease(number);if(stack)IOObjectRelease(stack);
  if(error){IOObjectRelease(interface);fprintf(stderr,"native interface registration refused 0x%08x\n",error);return 1;}
  for(unsigned attempt=0;attempt<10 && !bsd_name(interface,name);++attempt)usleep(50000);
 }
 ok=bsd_name(interface,name);IOObjectRelease(interface);
 if(!ok){fprintf(stderr,"native I210 BSD name not published\n");return 1;}
 printf("REGISTER_I210 interface=%s; no interface-up/address/route/DHCP action\n",name);return 0;
}
