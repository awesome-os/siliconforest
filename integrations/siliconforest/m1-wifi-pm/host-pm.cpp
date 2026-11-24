// SPDX-License-Identifier: MIT
// Actual PM functions and dispatch branch, fake firmware/locks only.
#include <cassert>
#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <vector>
#include <cstring>
#include <cstdarg>
#define BWFM_PM_CAM 0
#define BWFM_PM_FAST_PS 2
#define BWFM_C_SET_PM 86
#define BWFM_C_GET_PM 85
#define IFF_RUNNING 1
#define IEEE80211_M_HOSTAP 6
#define APPLE80211_IOC_POWERSAVE 5
#define APPLE80211_VERSION 1
#define kIOReturnSuccess 0
#define DEVNAME(sc) "fake"
#define BWFM_CTLWHY(sc) "fake firmware refusal"
#define LOG(...) ((void)0)
struct ifnet { int if_flags; };
struct ieee80211com { ifnet ic_if; int ic_opmode; };
struct bwfm_softc { int sc_pm_known,sc_pm_mode,sc_pm2_ret_ms,sc_initialized,sc_bus_gone; ieee80211com sc_ic; };
static int depth,fail_ret,fail_pm,fw_mode=2,fw_ret=200,boot_mode=2,boot_ret=2000;
static std::vector<int> calls;
static void bwfm_lifecycle_enter(bwfm_softc*) { assert(depth++==0); }
static void bwfm_lifecycle_exit(bwfm_softc*) { assert(--depth==0); }
static void PE_parse_boot_argn(const char *name, int *p, unsigned) {
 if(!strcmp(name,"bwfm_pm")) *p=boot_mode; else *p=boot_ret;
}
static int bwfm_fwvar_var_set_int(bwfm_softc*,const char *name,int v) {
 assert(!strcmp(name,"pm2_sleep_ret"));calls.push_back(1);if(fail_ret)return 1;fw_ret=v;return 0;
}
static int bwfm_fwvar_cmd_set_int(bwfm_softc*,int cmd,int v) {
 assert(cmd==BWFM_C_SET_PM);calls.push_back(2);if(fail_pm)return 1;fw_mode=v;return 0;
}
static int bwfm_fwvar_var_get_int(bwfm_softc*,const char*,uint32_t *v) { if(fail_ret)return 1;*v=fw_ret;return 0; }
static int bwfm_fwvar_cmd_get_int(bwfm_softc*,int,uint32_t *v) { if(fail_pm)return 1;*v=fw_mode;return 0; }
@BOOT@
@SET@
@GET@
static void init_pm(bwfm_softc *sc) {
 auto *ic=&sc->sc_ic;int pm;
@INIT@
}
@PAYLOAD@
static int ups;
static int sf_bwfm_set_pm(void *d,int m,int r) { return bwfm_pm_set(static_cast<bwfm_softc*>(d),m,r); }
static void sf_bwfm_get_pm(void *d,int *m,int *r,int *w,int *t) { bwfm_pm_get(static_cast<bwfm_softc*>(d),m,r,w,t); }
struct Driver {
 void *fIC;bwfm_softc *fDev;bool fChipDown,fPortDown;
 int chipUp(bool,const char*) { ++ups;fChipDown=fPortDown=false;return 0; }
 int request(bool set,int type,void *data,uint32_t *len) {
@DISPATCH@
 return -99;
 }
};
static bwfm_softc online() { bwfm_softc s{};s.sc_initialized=1;s.sc_ic.ic_if.if_flags=IFF_RUNNING;return s; }
int main() {
 static_assert(sizeof(sf80211_powersave_data)==20);
 assert(sf_bwfm_want_pm()==2 && sf_bwfm_want_pm2_ret()==2000);
 for(int b:{-1,3,2147483647}) { boot_mode=b;assert(sf_bwfm_want_pm()==2); }
 for(int b:{-1,60001,2147483647}) { boot_ret=b;assert(sf_bwfm_want_pm2_ret()==2000); }
 boot_mode=0;boot_ret=0;assert(sf_bwfm_want_pm()==0&&sf_bwfm_want_pm2_ret()==0);boot_mode=2;boot_ret=2000;
 auto sc=online();init_pm(&sc);assert((calls==std::vector<int>{1,2}));assert(fw_ret==2000&&fw_mode==2);calls.clear();
 sc.sc_ic.ic_opmode=IEEE80211_M_HOSTAP;init_pm(&sc);assert((calls==std::vector<int>{2})&&fw_mode==0);sc.sc_ic.ic_opmode=0;calls.clear();
 int m,r,w,t;bwfm_pm_get(&sc,&m,&r,&w,&t);assert(m==0&&r==2000&&w==2&&t==2000);
 assert(bwfm_pm_set(&sc,2,200)==0&&fw_ret==200&&fw_mode==2);assert((calls==std::vector<int>{1,2}));calls.clear();
 assert(bwfm_pm_set(&sc,0,0)==0&&fw_mode==0&&sc.sc_pm2_ret_ms==200);assert((calls==std::vector<int>{2}));calls.clear();
 assert(bwfm_pm_set(&sc,-1,1)==EINVAL&&bwfm_pm_set(&sc,3,1)==EINVAL&&bwfm_pm_set(&sc,2,-1)==EINVAL&&bwfm_pm_set(&sc,2,60001)==EINVAL&&calls.empty());
 fail_ret=1;assert(bwfm_pm_set(&sc,2,2000)==EIO);assert(fw_ret==200&&fw_mode==2);bwfm_pm_get(&sc,&m,&r,&w,&t);assert(m==2&&r==-1&&w==2&&t==2000);fail_ret=0;
 fail_pm=1;assert(bwfm_pm_set(&sc,0,0)==EIO);bwfm_pm_get(&sc,&m,&r,&w,&t);assert(m==-1&&r==200);fail_pm=0;
 for(int condition=0;condition<3;++condition) {
  sc=online();sc.sc_initialized=condition!=0;sc.sc_bus_gone=condition==1;sc.sc_ic.ic_if.if_flags=condition==2?0:IFF_RUNNING;calls.clear();
  assert(bwfm_pm_set(&sc,2,0)==0&&calls.empty()&&sc.sc_pm2_ret_ms==2000);
  bwfm_pm_get(&sc,&m,&r,&w,&t);assert(m==-1&&r==-1&&w==2&&t==2000);
 }
 sc=online();Driver d{&sc,&sc,true,false};sf80211_powersave_data ps{1,2,2000,-1,-1};uint32_t len=12;
 for(int bad=0;bad<4;++bad) {
  ps={1,2,2000,-1,-1};len=12;
  if(bad==0)ps.version=2;if(bad==1)ps.mode=UINT32_MAX;if(bad==2)ps.pm2_ret_ms=60001;if(bad==3)len=11;
  assert(d.request(true,5,&ps,&len)==EINVAL&&ups==0&&d.fChipDown);
 }
 ps={1,2,2000,-1,-1};len=12;assert(d.request(true,5,&ps,&len)==0&&ups==1);len=19;assert(d.request(false,5,&ps,&len)==EINVAL);
 len=20;assert(d.request(false,5,&ps,&len)==0&&ps.fw_mode==2&&ps.fw_pm2_ret_ms==2000&&ps.version==1&&len==20);
 assert(d.request(true,5,nullptr,&len)==-99&&d.request(true,5,&ps,nullptr)==-99&&d.request(true,99,&ps,&len)==-99);
 assert(depth==0);puts("PASS actual PM boot defaults/bounds, init ordering/AP, runtime firmware/cache failures, dispatch validation before chipUp, 20-byte ABI; fake providers only");
}
