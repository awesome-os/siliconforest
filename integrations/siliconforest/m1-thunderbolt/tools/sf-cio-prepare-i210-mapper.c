/* SPDX-License-Identifier: MIT */
/* Explicit administrator request for the real I210 IOMapper.
 * Requires the actual completed blocked inventory; no arbitrary address,
 * bus-master grant or endpoint-driver admission. No DMA success is claimed.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOReturn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <grp.h>
static int count(io_service_t service,uint32_t *value)
{
 CFTypeRef obj=IORegistryEntryCreateCFProperty(service,CFSTR("i210-mapper-request-count"),kCFAllocatorDefault,0);
 int ok=obj && CFGetTypeID(obj)==CFNumberGetTypeID() && CFNumberGetValue((CFNumberRef)obj,kCFNumberSInt32Type,value);
 if(obj)CFRelease(obj);return ok;
}
int main(int argc,char **argv)
{
 int deny=argc==2 && !strcmp(argv[1],"--deny-unprivileged");
 if(argc!=1 && !deny){fprintf(stderr,"usage: sf-cio-prepare-i210-mapper [--deny-unprivileged]\n");return 2;}
 if(deny && !geteuid()){
  if(setgroups(0,NULL) || setgid(65534) || setuid(65534) || !geteuid()){perror("drop privileges");return 1;}
 }
 if(!deny && geteuid()){fprintf(stderr,"root required for I210 mapper preparation\n");return 1;}
 io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("SFAppleCIO"));
 if(!service){fprintf(stderr,"no native SFAppleCIO service\n");return 1;}
 uint32_t before=0,after=0;int had=count(service,&before);
 kern_return_t ret=IORegistryEntrySetCFProperty(service,CFSTR("sf-cio-prepare-i210-mapper"),kCFBooleanTrue);
 int has=count(service,&after);
 CFTypeRef state=IORegistryEntryCreateCFProperty(service,CFSTR("pcie-endpoint-DMA-admitted"),kCFAllocatorDefault,0);
 const char *admission=state && CFGetTypeID(state)==CFBooleanGetTypeID()?(CFBooleanGetValue((CFBooleanRef)state)?"reported-enabled":"reported-blocked"):"unknown";
 IOObjectRelease(service);
 printf("I210 mapper request: uid=%u result=0x%08x count=%u->%u; kernel DMA admission checkpoint=%s; independent interface/packet evidence required\n",(unsigned)geteuid(),ret,before,after,admission);
 if(state)CFRelease(state);
 if(!had || !has)return 1;
 if(deny)return ret==kIOReturnNotPrivileged && before==after?0:1;
 return ret==KERN_SUCCESS && before!=UINT32_MAX && after==before+1?0:1;
}
