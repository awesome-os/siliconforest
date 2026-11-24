/* SPDX-License-Identifier: MIT */
/* One explicit administrator retry of the actual peerless port1 owner.
 * No addresses, register values, cable words or automatic retries.
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
 CFTypeRef obj=IORegistryEntryCreateCFProperty(service,CFSTR("retrain-request-count"),kCFAllocatorDefault,0);
 int ok=obj && CFGetTypeID(obj)==CFNumberGetTypeID() && CFNumberGetValue((CFNumberRef)obj,kCFNumberSInt32Type,value);
 if(obj)CFRelease(obj);return ok;
}
int main(int argc,char **argv)
{
 int exhausted=argc==2 && !strcmp(argv[1],"--expect-exhausted");
 int deny=argc==2 && !strcmp(argv[1],"--deny-unprivileged");
 if(argc!=1 && !deny && !exhausted){fprintf(stderr,"usage: sf-cio-retrain-peerless [--deny-unprivileged|--expect-exhausted]\n");return 2;}
 if(deny && !geteuid()){
  if(setgroups(0,NULL) || setgid(65534) || setuid(65534) || !geteuid()){perror("drop privileges");return 1;}
 }
 if(!deny && geteuid()){fprintf(stderr,"root required for peerless port1 retrain\n");return 1;}
 io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("SFAppleCIO"));
 if(!service){fprintf(stderr,"no native SFAppleCIO service\n");return 1;}
 uint32_t before=0,after=0;int had=count(service,&before);
 if(!had || (exhausted && before!=1)){
  fprintf(stderr,"retrain counter unavailable or retry budget not yet consumed\n");
  IOObjectRelease(service);return 1;
 }
 kern_return_t ret=IORegistryEntrySetCFProperty(service,CFSTR("sf-cio-retrain-peerless"),kCFBooleanTrue);
 int has=count(service,&after);IOObjectRelease(service);
 printf("Peerless port1 retrain request: uid=%u result=0x%08x count=%u->%u; restart is not remote discovery or PCI evidence\n",(unsigned)geteuid(),ret,before,after);
 if(!had || !has)return 1;
 if(deny)return ret==kIOReturnNotPrivileged && before==after?0:1;
 if(exhausted)return before==1 && after==before && ret==kIOReturnNoResources?0:1;
 return ret==KERN_SUCCESS && before!=UINT32_MAX && after==before+1?0:1;
}
