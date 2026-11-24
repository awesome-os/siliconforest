/* SPDX-License-Identifier: MIT */
/* Actual native PMGR extension with ADT/MMIO fault injection. */
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
#define PMGR_RESET (1u<<31)
#define PMGR_DEV_DISABLE (1u<<10)
#define PMGR_WAS_CLKGATED (1u<<9)
#define PMGR_WAS_PWRGATED (1u<<8)
#define PMGR_PS_ACTIVE 15
#define OSDynamicCast(type,obj) static_cast<type *>(obj)
#define kIOMemoryMapperNone 0
struct IOMemoryDescriptor {
 uint64_t base=0x23b700450ULL,len=0x1000;
 uint64_t getLength(){return len;}
 uint64_t getPhysicalSegment(unsigned,void *,int){return base;}
} md;
struct Array {unsigned n=1;unsigned getCount(){return n;}void *getObject(unsigned i){return i<n?&md:nullptr;}} regs;
struct {int *lock;unsigned psRegsLen;uint8_t *psRegs;Array *regs;} gPmgr;
static uint8_t devices[4][48],ps[12];
static uint32_t states[4],cells[17];static unsigned nc=3,reads,live;
static int loadError,failMap=-1,stuck=-1;static bool missing;
static std::vector<unsigned> writes;
static int mutex;
static void pmgr_lock_init(){gPmgr.lock=&mutex;}
static void IOLockLock(int *){}static void IOLockUnlock(int *){}
static void IODelay(unsigned){}
static unsigned sf_adt_cells(const char *,const char *name){assert(!std::strcmp(name,"power-gates"));return nc;}
static bool sf_adt_u32(const char *,const char *name,unsigned i,uint32_t *p){assert(!std::strcmp(name,"power-gates"));*p=cells[i];return true;}
static int pmgr_load(){return loadError;}
static const uint8_t *dev_find(uint16_t id){return !missing && id && id<=4?devices[id-1]:nullptr;}
static const uint8_t *dev_find_named(const char *name){assert(!std::strcmp(name,"ATC1_PCIE"));return dev_find(4);}
static uint16_t dev_id(const uint8_t *dev){return (uint16_t)((dev-&devices[0][0])/48+1);}
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
struct SFMmio {
 bool mapped=false;unsigned index=0;uint64_t phys=0;
 bool mapPhys(uint64_t base,uint64_t len){assert(base>=md.base && base-md.base<=md.len && len<=md.len-(base-md.base));phys=base;index=(base-md.base)/8;if((int)index==failMap)return false;assert(index<4 && !mapped);mapped=true;++live;return true;}
 uint32_t r32(unsigned off){assert(mapped && !off);++reads;return states[index];}
 void w32(unsigned off,uint32_t value){assert(mapped && !off);writes.push_back(index);states[index]=value;if((int)index!=stuck)states[index]=(value&~PMGR_PS_ACTUAL)|((value&PMGR_PS_TARGET)<<4);}
 void mask32(unsigned off,uint32_t clear,uint32_t set){w32(off,(r32(off)&~clear)|set);}
 void unmap(){if(mapped){mapped=false;--live;}}
};
#include "sf_cio_pmgr.inc"
static void reset(){assert(!live);std::memset(devices,0,sizeof(devices));std::memset(ps,0,sizeof(ps));gPmgr={&mutex,12,ps,&regs};nc=3;for(unsigned i=0;i<4;++i){cells[i]=i+1;devices[i][DEV_ADDR_OFFSET]=(i+1)%4;states[i]=0xff;}reads=0;loadError=0;failMap=stuck=-1;missing=false;regs.n=1;md={};writes.clear();}
int main(){
 reset();assert(!sf_cio_power_off());assert((writes==std::vector<unsigned>{3,2,1}));for(unsigned i=1;i<4;++i)assert(!(states[i]&0xff));assert(!live);
 for(int i=1;i<4;++i){reset();failMap=i;assert(sf_cio_power_off()==ENXIO && writes.empty() && !live);}
 reset();loadError=ENXIO;assert(sf_cio_power_off()==ENXIO && writes.empty());
 reset();missing=true;assert(sf_cio_power_off()==ENOENT && writes.empty());
 reset();devices[1][DEV_FLAGS]=PMGR_FLAG_VIRTUAL;assert(sf_cio_power_off()==ENOENT && writes.empty());
 reset();devices[2][DEV_PSREG_IDX]=1;assert(sf_cio_power_off()==EINVAL && writes.empty());
 reset();ps[0]=1;assert(sf_cio_power_off()==EINVAL && writes.empty());
 reset();nc=17;assert(sf_cio_power_off()==EINVAL && writes.empty());
 reset();md.len=4;assert(sf_cio_power_off()==EINVAL && writes.empty());
 reset();stuck=2;assert(sf_cio_power_off()==ETIMEDOUT);assert((writes==std::vector<unsigned>{3,2}));assert(states[0]==0xff && states[1]==0xff && !live);
 reset();states[3]|=PMGR_RESET;assert(sf_cio_power_off()==EBUSY && writes.empty());
 reset();states[0]=0x44;assert(sf_cio_pcie_reset()==EBUSY && writes.empty());
 reset();states[0]=UINT32_MAX;assert(sf_cio_pcie_reset()==EBUSY && writes.empty());
 reset();assert(!sf_cio_pcie_reset());assert((writes==std::vector<unsigned>{0,0,0,0}));assert(states[0]==0xff && !live);
 reset();states[0]|=1u<<11;assert(!sf_cio_pcie_reset());assert(!(states[0]&(1u<<11)));
 puts("Native ACIO PMGR: all gates preflight before writes; reverse shutdown, timeout retains parents, PCIe-C reset/off refusal and no BUSY replay passed");
}
