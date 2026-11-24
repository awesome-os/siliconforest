// SPDX-License-Identifier: GPL-2.0
/* Native adapter for sf_pciec_port, following pinned pcie-apple.c.
 * Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 * Copyright (C) 2021 Google LLC
 * Copyright (C) 2021 Corellium LLC
 * Copyright (C) 2021 Mark Kettenis <kettenis@openbsd.org>
 */
#include "sf_cio_pcie_controller.h"
#include <IOKit/IODeviceTreeSupport.h>
#include <machine/machine_routines.h>
#include <sys/errno.h>
OSDefineMetaClassAndStructors(SFCIOPCIController,OSObject)
static void ordered(){__asm__ volatile("dsb sy; isb" ::: "memory");}
bool SFCIOPCIController::initWithOwner(IOService *lease,void *ctx,
 bool (*power)(void *),bool (*tunnel)(void *))
{
 owner=NULL;context=NULL;powerProof=NULL;tunnelProof=NULL;lock=NULL;
 admitted=touched=poisoned=cold=linked=false;
 dmaOwner=NULL;
 requesters={};
 if(!lease || !ctx || !power || !tunnel || !OSObject::init())return false;
 uint32_t chip=0;if(!sf_adt_u32("/chosen","chip-id",0,&chip) || chip!=0x8103)return false;
 // Actual registry children, not a caller-supplied "unbound" flag.
 const char *paths[]={"/arm-io/apciec1","/arm-io/dart-apciec1"};
 for(auto path:paths){
  auto node=IORegistryEntry::fromPath(path,gIODTPlane);if(!node)return false;
  auto children=node->getChildIterator(gIOServicePlane);
  bool bound=children && children->getNextObject();
  OSSafeReleaseNULL(children);node->release();if(bound)return false;
 }
 lock=IOLockAlloc();if(!lock)return false;
 lease->retain();owner=lease;context=ctx;powerProof=power;tunnelProof=tunnel;
 static const unsigned indices[]={0,2,4,3};
 static const uint64_t bases[]={0x530000000ULL,0x521000000ULL,0x520200000ULL,0x520004000ULL};
 static const uint64_t lengths[]={0x10000000,0x8000,0x4000,0x4000};
 for(unsigned n=0;n<4;++n)
  if(!windows[n].mapNode(paths[0],indices[n]) || windows[n].phys!=bases[n] || windows[n].len<lengths[n])return false;
 // T8103's published oe-fabric region has no separate native ADT reg entry.
 if(!windows[SF_PCIEC_OE].mapPhys(0x520008000ULL,0x4000) ||
    !powerGate.mapPhys(0x23b700450ULL,4))return false;
 io={this,read,write,tune,delay};admitted=true;return true;
}
bool SFCIOPCIController::live()
{
 if(!admitted || poisoned || !powerProof || !powerProof(context))return false;
 ordered();uint32_t gate=powerGate.r32(0);ordered();
 return gate!=UINT32_MAX && (gate&0xf0)==0xf0 && !(gate&((1u<<31)|(1u<<10)));
}
int SFCIOPCIController::read(void *ctx,sf_pciec_window win,uint32_t off,uint32_t *out)
{
 auto s=(SFCIOPCIController *)ctx;
 if(!out || (unsigned)win>SF_PCIEC_OE || (off&3) ||
    off>s->windows[win].len || s->windows[win].len-off<4)return -EINVAL;
 if(!s->live() || !s->tunnelProof(s->context))return -ENODEV;
 ordered();uint32_t value=s->windows[win].r32(off);ordered();*out=value;return 0;
}
int SFCIOPCIController::write(void *ctx,sf_pciec_window win,uint32_t off,uint32_t value)
{
 auto s=(SFCIOPCIController *)ctx;
 if((unsigned)win>SF_PCIEC_OE || (off&3) ||
    off>s->windows[win].len || s->windows[win].len-off<4)return -EINVAL;
 if(!s->live() || !s->tunnelProof(s->context))return -ENODEV;
 ordered();s->windows[win].w32(off,value);ordered();return 0;
}
int SFCIOPCIController::tune(void *ctx,sf_pciec_window win)
{auto s=(SFCIOPCIController *)ctx;return sf_pciec_t8103_tune(&s->io,win);}
void SFCIOPCIController::delay(void *,unsigned us)
{if(us>=1000)IOSleep(us/1000);else IODelay(us);}
int SFCIOPCIController::coldInit()
{
 if(!lock || ml_at_interrupt_context())return EINVAL;
 IOLockLock(lock);int ret;
 if(!live() || !tunnelProof(context))ret=ENODEV;
 else if(touched)ret=EBUSY;
 else{
  touched=true;ret=sf_pciec_cold_init(&io);if(ret)poisoned=true;else cold=true;
 }
 IOLockUnlock(lock);return ret<0?-ret:ret;
}
int SFCIOPCIController::linkStart()
{
 if(!lock || ml_at_interrupt_context())return EINVAL;
 IOLockLock(lock);int ret;
 if(!live() || !cold || !tunnelProof(context))ret=ENODEV;
 else if(linked)ret=EALREADY;
 else{ret=sf_pciec_link_start(&io);if(ret)poisoned=true;else linked=true;}
 IOLockUnlock(lock);return ret<0?-ret:ret;
}
int SFCIOPCIController::reserveRequester(unsigned bus,unsigned device,unsigned function,uint32_t *sid)
{
 if(!sid || !lock || ml_at_interrupt_context())return EINVAL;
 IOLockLock(lock);int ret=ENODEV;
 if(live() && linked && tunnelProof(context)){
  uint32_t run=0,link=0;
  ret=read(this,SF_PCIEC_PORT,0x804,&run);
  if(!ret)ret=read(this,SF_PCIEC_PORT,0x208,&link);
  if(!ret && (run==UINT32_MAX || link==UINT32_MAX || !(run&1) || !(link&1)))ret=ENODEV;
  if(!ret){
   sf_pciec_requester_io access={this,
    [](void *p,uint32_t off,uint32_t *v)->int {
     int e=read(p,SF_PCIEC_PORT,off,v);return e<0?-e:e;
    },
    [](void *p,uint32_t off,uint32_t v)->int {
     int e=write(p,SF_PCIEC_PORT,off,v);return e<0?-e:e;
    }};
   ret=sf_pciec_requester_reserve(&requesters,&access,bus,device,function,sid);
   if(requesters.poisoned)poisoned=true;
  }
 }
 IOLockUnlock(lock);return ret<0?-ret:ret;
}
int SFCIOPCIController::revoke()
{
 if(!lock || ml_at_interrupt_context())return EINVAL;
 IOLockLock(lock);admitted=false;int ret=touched?EBUSY:0;IOLockUnlock(lock);return ret;
}
bool SFCIOPCIController::endpointAccess()
{
 if(!live() || !linked || !tunnelProof(context) || !endpointDart.va)return false;
 ordered();uint32_t run=windows[SF_PCIEC_PORT].r32(0x804);
 uint32_t link=windows[SF_PCIEC_PORT].r32(0x208);ordered();
 return run!=UINT32_MAX && link!=UINT32_MAX && (run&1) && (link&1);
}
static bool dartWord(uint32_t off)
{
 return !(off&3) && ((off>=0x100 && off<0x200) || (off>=0x400 && off<0x800));
}
int SFCIOPCIController::dartRead(void *p,uint32_t off,uint32_t *value)
{
 auto s=(SFCIOPCIController *)p;
 if(!value || !(dartWord(off) || off==0x20 || off==0x60))return EINVAL;
 if(!s->endpointAccess())return ENODEV;
 ordered();*value=s->endpointDart.r32(off);ordered();return 0;
}
int SFCIOPCIController::dartWrite(void *p,uint32_t off,uint32_t value)
{
 auto s=(SFCIOPCIController *)p;
 if(!(dartWord(off) || off==0x20 || off==0x34 || off==0x38))return EINVAL;
 if(!s->endpointAccess())return ENODEV;
 ordered();s->endpointDart.w32(off,value);ordered();return 0;
}
int SFCIOPCIController::blockEndpointDMA()
{
 if(!lock || ml_at_interrupt_context())return EINVAL;
 IOLockLock(lock);int ret=ENODEV;
 if(dmaOwner)ret=EBUSY;
 else if(live() && linked && tunnelProof(context)){
  auto node=IORegistryEntry::fromPath("/arm-io/dart-apciec1",gIODTPlane);
  auto clients=node?node->getChildIterator(gIOServicePlane):NULL;
  bool unbound=node && clients && !clients->getNextObject();
  OSSafeReleaseNULL(clients);OSSafeReleaseNULL(node);
  if(!unbound)ret=EBUSY;
  else if((!endpointDart.va && !endpointDart.mapNode("/arm-io/dart-apciec1",0)) ||
          endpointDart.phys!=0x521008000ULL || endpointDart.len<0x800)ret=ENODEV;
  else{
   sf_pciec_dart_block_io access={this,dartRead,dartWrite,delay};
   ret=sf_pciec_dart_block(&access);if(ret)poisoned=true;
  }
 }
 IOLockUnlock(lock);return ret;
}
void SFCIOPCIController::free()
{
 if(touched){IOLog("SFCIOPCIController: retaining programmed controller ownership until reboot\n");return;}
 for(auto &window:windows)window.unmap();powerGate.unmap();endpointDart.unmap();
 if(lock){IOLockFree(lock);lock=NULL;}
 OSSafeReleaseNULL(owner);OSObject::free();
}
