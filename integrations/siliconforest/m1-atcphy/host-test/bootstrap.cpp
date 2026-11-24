// SPDX-License-Identifier: MIT
#include "sf_atc_pipe.h"
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <cstdio>
struct Model {
 uint32_t regs[9]={0,0,0,0x22,0,0,0,0x30,0x1332};unsigned writes=0,stuck=0;
 uint32_t usb2[8]={0x104,0xb,0,0,0,0,0,0x608c0013};
 static uint32_t read(void *p,sf_atc_window w,uint32_t off){auto m=(Model *)p;assert(!(off&3));if(w==SF_ATC_USB2){assert(off<=0x1c);return m->usb2[off/4];}assert(w==SF_ATC_PIPE && off<=0x20);return m->regs[off/4];}
 static void write(void *p,sf_atc_window w,uint32_t off,uint32_t value){auto m=(Model *)p;++m->writes;if(w==SF_ATC_USB2){assert(off==0x1c && !(m->usb2[0]&2) && ((m->usb2[7]^value)==0x800));if(m->writes!=m->stuck)m->usb2[off/4]=value;}else{assert(w==SF_ATC_PIPE && (off==0x1c || off==0x20));if(m->writes!=m->stuck)m->regs[off/4]=value;}assert(!(m->regs[7]&1));}
 sf_atc_io io(){return {this,read,write,nullptr,nullptr};}
};
int main(){
 sf_atc_pipe_state state={SF_ATC_DUMMY,false};
 {Model m;auto io=m.io();assert(!sf_atc_pipe_bootstrap_dummy(&io,&state,1));assert(m.writes==1 && m.regs[7]==0x30 && m.regs[8]==0x9332);assert(!sf_atc_pipe_bootstrap_dummy(&io,&state,1) && m.writes==1);}
 {Model m;auto io=m.io();assert(sf_atc_pipe_bootstrap_dummy(&io,&state,0)==EBUSY && !m.writes);}
 for(unsigned i=0;i<4;i++){
  Model m;auto io=m.io();unsigned off[]={3,5,7,8};m.regs[off[i]]=UINT32_MAX;
  assert(sf_atc_pipe_bootstrap_dummy(&io,&state,1)==EIO && !m.writes);
 }
 for(unsigned i=0;i<3;i++){
  Model m;auto io=m.io();if(i==0)m.regs[3]=8;if(i==1)m.regs[5]=1;if(i==2)m.regs[7]|=1;
  assert(sf_atc_pipe_bootstrap_dummy(&io,&state,1)==EBUSY && !m.writes);
 }
 {Model m;m.stuck=1;auto io=m.io();assert(sf_atc_pipe_bootstrap_dummy(&io,&state,1)==EIO && m.writes==1);}
 {Model m;m.regs[8]|=1u<<22;auto io=m.io();assert(!sf_atc_pipe_bootstrap_dummy(&io,&state,1) && m.regs[8]==(0x9332|(1u<<22)) && m.regs[7]==0x30);}
 for(unsigned i=0;i<2;i++){Model m;auto io=m.io();if(i)m.regs[8]&=~0x1000u;else m.regs[8]=(m.regs[8]&~0xfu)|3u;assert(sf_atc_pipe_bootstrap_dummy(&io,&state,1)==EBUSY && !m.writes);}
 state.dummy_pending=true;{Model m;auto io=m.io();assert(sf_atc_pipe_bootstrap_dummy(&io,&state,1)==EBUSY && !m.writes);}
 state={SF_ATC_DUMMY,false};
 {Model m;m.regs[8]=0x9332;auto io=m.io();assert(!sf_atc_pipe_bootstrap_aon(&io,&state,1));assert(m.writes==1 && m.regs[7]==0x10 && m.regs[8]==0x9332);assert(!sf_atc_pipe_bootstrap_aon(&io,&state,1) && m.writes==1);}
 for(unsigned n=0;n<32;++n){
  Model m;m.regs[8]=0x9332;m.regs[7]=0x30^(1u<<n);auto io=m.io();
  if(n==5)assert(!sf_atc_pipe_bootstrap_aon(&io,&state,1) && !m.writes);
  else assert(sf_atc_pipe_bootstrap_aon(&io,&state,1)==EBUSY && !m.writes);
 }
 for(unsigned n=0;n<4;++n){Model m;m.regs[8]=0x9332;unsigned off[]={3,5,7,8};m.regs[off[n]]=UINT32_MAX;auto io=m.io();assert(sf_atc_pipe_bootstrap_aon(&io,&state,1)==EIO && !m.writes);}
 {Model m;m.regs[8]=0x9332;auto io=m.io();assert(sf_atc_pipe_bootstrap_aon(&io,&state,0)==EBUSY && !m.writes);}
 {Model m;auto io=m.io();assert(sf_atc_pipe_bootstrap_aon(&io,&state,1)==EBUSY && !m.writes);}
 {Model m;m.regs[8]=0x9332;m.stuck=1;auto io=m.io();assert(sf_atc_pipe_bootstrap_aon(&io,&state,1)==EIO && m.writes==1);}
 for(unsigned n=0;n<2;++n){Model m;m.regs[8]=0x9332;auto io=m.io();sf_atc_pipe_state bad={n?SF_ATC_DUMMY:SF_ATC_USB3_BACKEND,(bool)n};assert(sf_atc_pipe_bootstrap_aon(&io,&bad,1)==EBUSY && !m.writes);}
 {Model m;m.regs[7]=0x10;m.regs[8]=0x9332;auto io=m.io();assert(!sf_atc_usb2_bootstrap_t8103(&io,&state,1));assert(m.writes==1 && m.usb2[7]==0x608c0813);assert(!sf_atc_usb2_bootstrap_t8103(&io,&state,1) && m.writes==1);}
 for(unsigned n=0;n<32;++n){Model m;m.regs[7]=0x10;m.regs[8]=0x9332;m.usb2[7]^=1u<<n;auto io=m.io();if(n==11)assert(!sf_atc_usb2_bootstrap_t8103(&io,&state,1) && !m.writes);else assert(sf_atc_usb2_bootstrap_t8103(&io,&state,1)==EBUSY && !m.writes);}
 for(unsigned n=0;n<7;++n){Model m;m.regs[7]=0x10;m.regs[8]=0x9332;unsigned off[]={3,5,7,8};if(n<4)m.regs[off[n]]=UINT32_MAX;else m.usb2[n==4?0:n==5?1:7]=UINT32_MAX;auto io=m.io();assert(sf_atc_usb2_bootstrap_t8103(&io,&state,1)==EIO && !m.writes);}
 for(unsigned n=0;n<9;++n){Model m;m.regs[7]=0x10;m.regs[8]=0x9332;auto io=m.io();sf_atc_pipe_state bad=state;if(n==0)bad.backend=SF_ATC_USB3_BACKEND;if(n==1)bad.dummy_pending=true;if(n==2)m.regs[3]=8;if(n==3)m.regs[5]=1;if(n==4)m.regs[7]|=1;if(n==5)m.regs[8]&=~0x1000u;if(n==6)m.usb2[0]|=2;if(n==7)m.usb2[1]|=4;assert(sf_atc_usb2_bootstrap_t8103(&io,&bad,n!=8)==EBUSY && !m.writes);}
 {Model m;m.regs[7]=0x10;m.regs[8]=0x9332;m.stuck=1;auto io=m.io();assert(sf_atc_usb2_bootstrap_t8103(&io,&state,1)==EIO && m.writes==1);}
 puts("HOST ONLY: parked PIPE/AON/USB2 baseline, every foreign tuning bit, physical-reset/isolation/ACIO proof refusals, inaccessible MMIO and failed readbacks pass");
}
