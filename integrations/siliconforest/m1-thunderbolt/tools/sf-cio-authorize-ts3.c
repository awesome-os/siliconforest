/* SPDX-License-Identifier: MIT */
/* Explicit administrator request for the actual direct CalDigit TS3 Plus.
 * No arbitrary route/PCI address or key input. Success publishes a DMA-blocked
 * native host only; the independent PCI inventory must still prove devices.
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
 CFTypeRef obj=IORegistryEntryCreateCFProperty(service,CFSTR("authorization-request-count"),kCFAllocatorDefault,0);
 int ok=obj && CFGetTypeID(obj)==CFNumberGetTypeID() && CFNumberGetValue((CFNumberRef)obj,kCFNumberSInt32Type,value);
 if(obj)CFRelease(obj);return ok;
}
int main(int argc,char **argv)
{
 int deny=argc==2 && !strcmp(argv[1],"--deny-unprivileged");
 if(argc!=1 && !deny){fprintf(stderr,"usage: sf-cio-authorize-ts3 [--deny-unprivileged]\n");return 2;}
 if(deny && !geteuid()){
  if(setgroups(0,NULL) || setgid(65534) || setuid(65534) || !geteuid()){perror("drop privileges");return 1;}
 }
 if(!deny && geteuid()){fprintf(stderr,"root required for TS3 authorization\n");return 1;}
 io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("SFAppleCIO"));
 if(!service){fprintf(stderr,"no native SFAppleCIO service\n");return 1;}
 uint32_t before=0,after=0;int had=count(service,&before);
 kern_return_t ret=IORegistryEntrySetCFProperty(service,CFSTR("sf-cio-authorize-ts3"),kCFBooleanTrue);
 int has=count(service,&after);IOObjectRelease(service);
 printf("TS3 authorization/host request: uid=%u result=0x%08x count=%u->%u; independent PCI inventory required\n",(unsigned)geteuid(),ret,before,after);
 if(!had || !has)return 1;
 if(deny)return ret==kIOReturnNotPrivileged && before==after?0:1;
 return ret==KERN_SUCCESS && before!=UINT32_MAX && after==before+1?0:1;
}
