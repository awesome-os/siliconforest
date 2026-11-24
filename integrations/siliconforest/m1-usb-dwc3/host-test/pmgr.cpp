/* SPDX-License-Identifier: MIT */
/* Exercise the actual native read-only extension with a bounded ADT/MMIO
 * fixture. This is parser/refusal coverage, not an M1 power observation. */
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <cassert>
#include <cstdio>
#include <vector>
#define PMGR_DEVICE_ID 0xffffu
#define PMGR_FLAG_VIRTUAL 0x10
#define DEV_FLAGS 0
#define DEV_ADDR_OFFSET 10
#define DEV_PSREG_IDX 11
#define PMGR_PS_ACTUAL 0xf0u
#define PMGR_PS_TARGET 0xfu
#define PMGR_AUTO_ENABLE (1u<<28)
#define OSDynamicCast(type,obj) static_cast<type *>(obj)
#define kIOMemoryMapperNone 0
struct IOMemoryDescriptor {
 uint64_t base=0x23b700458ULL,len=0x1000;
 uint64_t getLength(){return len;}
 uint64_t getPhysicalSegment(unsigned,void *,int){return base;}
} md;
struct Array { unsigned n=1;unsigned getCount(){return n;}void *getObject(unsigned i){return i<n?&md:nullptr;} } regs;
struct {int *lock;unsigned psRegsLen;uint8_t *psRegs;Array *regs;} gPmgr;
static uint8_t devices[3][48],ps[12];
static uint32_t raw, cells[17];static unsigned nc=3,reads,live;
static int loadError;static bool failCell,failMap,missing;
static int mutex;
static void pmgr_lock_init(){gPmgr.lock=&mutex;}
static void IOLockLock(int *){}static void IOLockUnlock(int *){}
static unsigned sf_adt_cells(const char *,const char *name){assert(!std::strcmp(name,"power-gates"));return nc;}
static bool sf_adt_u32(const char *,const char *name,unsigned i,uint32_t *p){assert(!std::strcmp(name,"power-gates"));if(failCell)return false;*p=cells[i];return true;}
static int pmgr_load(){return loadError;}
static const uint8_t *dev_find(uint16_t id){return !missing && id && id<=3?devices[id-1]:nullptr;}
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
struct SFMmio {
 bool mapped=false;
 bool mapPhys(uint64_t base,uint64_t len){assert(base>=md.base && base-md.base<=md.len && len<=md.len-(base-md.base));if(failMap)return false;assert(!mapped);mapped=true;++live;return true;}
 uint32_t r32(unsigned off){assert(mapped && !off);++reads;return raw;}
 void unmap(){assert(mapped);mapped=false;--live;}
};
#include "sf_usbc_pmgr.inc"
static void reset(){std::memset(devices,0,sizeof(devices));std::memset(ps,0,sizeof(ps));gPmgr={&mutex,12,ps,&regs};nc=3;cells[0]=1;cells[1]=2;cells[2]=3;devices[1][DEV_ADDR_OFFSET]=1;devices[2][DEV_ADDR_OFFSET]=2;raw=0;reads=0;loadError=0;failCell=failMap=missing=false;regs.n=1;md={};}
int main(){
 uint32_t value;
 reset();assert(!sf_usbc_pmgr_node_off("/arm-io/acio1",&value) && value==0 && reads==3);
 for(uint32_t r:{0x1u,0x40u,0xf0u,0xffu,PMGR_AUTO_ENABLE,UINT32_MAX}){reset();raw=r;assert(sf_usbc_pmgr_node_off("/arm-io/acio1",&value)==EBUSY && value==r && !live);}
 reset();nc=2;assert(sf_usbc_pmgr_node_off("/arm-io/acio1",&value)==ENOENT && !reads);
 reset();missing=true;assert(sf_usbc_pmgr_node_off("/arm-io/acio1",&value)==ENOENT && !reads);
 reset();devices[0][DEV_FLAGS]=PMGR_FLAG_VIRTUAL;assert(sf_usbc_pmgr_node_off("/arm-io/acio1",&value)==ENOENT && !reads);
 reset();cells[0]=0x10000001;assert(sf_usbc_pmgr_node_off("/arm-io/acio1",&value)==EINVAL && !reads);
 reset();cells[0]=0;assert(sf_usbc_pmgr_node_off("/arm-io/acio1",&value)==ENOENT && !reads);
 reset();nc=0;assert(sf_usbc_pmgr_node_off("/arm-io/acio1",&value)==ENOENT && !reads);
 reset();nc=37;assert(sf_usbc_pmgr_node_off("/arm-io/acio1",&value)==ENOENT && !reads);
 reset();failCell=true;assert(sf_usbc_pmgr_node_off("/arm-io/acio1",&value)==EINVAL && !reads);
 reset();loadError=ENXIO;assert(sf_usbc_pmgr_node_off("/arm-io/acio1",&value)==ENXIO && !reads);
 reset();devices[0][DEV_PSREG_IDX]=1;assert(sf_usbc_pmgr_node_off("/arm-io/acio1",&value)==EINVAL && !reads);
 reset();ps[0]=1;assert(sf_usbc_pmgr_node_off("/arm-io/acio1",&value)==EINVAL && !reads);
 reset();ps[5]=0x10;assert(sf_usbc_pmgr_node_off("/arm-io/acio1",&value)==EINVAL && !reads);
 reset();md.len=3;assert(sf_usbc_pmgr_node_off("/arm-io/acio1",&value)==EINVAL && !reads);
 reset();md.base=0;assert(sf_usbc_pmgr_node_off("/arm-io/acio1",&value)==ENXIO && !reads);
 reset();failMap=true;assert(sf_usbc_pmgr_node_off("/arm-io/acio1",&value)==ENXIO && !reads);
 reset();assert(sf_usbc_pmgr_node_off(nullptr,&value)==EINVAL);assert(sf_usbc_pmgr_node_off("/arm-io/acio1",nullptr)==EINVAL);
 assert(!live);puts("Actual native PMGR check: unknown/malformed/mapping failures never report off; active/clock-gated/auto gates refuse PHY handoff");
}
