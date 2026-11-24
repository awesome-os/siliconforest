/* SPDX-License-Identifier: GPL-2.0 */
#include "sf_tb_retrain_policy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct sf_tb_nhi_status sample(void)
{
 struct sf_tb_nhi_status s;memset(&s,0,sizeof(s));
 s.root_header_valid=1;s.port_count=2;s.cable_info=0x411;
 for(unsigned n=0;n<2;++n){s.ports[n].port=n+1;s.ports[n].state=7;s.ports[n].flags_valid=1;s.ports[n].cap_phy=17;}
 return s;
}
int main(void)
{
 struct sf_tb_nhi_status s=sample();assert(!sf_tb_retrain_peerless(&s));
 assert(sf_tb_retrain_peerless(NULL)==EINVAL);
 for(unsigned n=0;n<16;++n){s=sample();s.port_count=n;if(n!=2)assert(sf_tb_retrain_peerless(&s));}
 s=sample();s.root_header_valid=0;assert(sf_tb_retrain_peerless(&s));
 for(unsigned bit=0;bit<32;++bit){s=sample();s.cable_info^=1u<<bit;assert(sf_tb_retrain_peerless(&s));}
 for(unsigned lane=0;lane<2;++lane){
  for(unsigned state=0;state<16;++state){s=sample();s.ports[lane].state=state;assert((sf_tb_retrain_peerless(&s)==0)==(state==7));}
  s=sample();s.ports[lane].port=0;assert(sf_tb_retrain_peerless(&s));
  s=sample();s.ports[lane].port=3;assert(sf_tb_retrain_peerless(&s));
  s=sample();s.ports[lane].port=s.ports[1-lane].port;assert(sf_tb_retrain_peerless(&s));
  s=sample();s.ports[lane].remote=1;assert(sf_tb_retrain_peerless(&s));
  s=sample();s.ports[lane].remote_authorized=1;assert(sf_tb_retrain_peerless(&s));
  s=sample();s.ports[lane].error=EIO;assert(sf_tb_retrain_peerless(&s));
  s=sample();s.ports[lane].flags_valid=0;assert(sf_tb_retrain_peerless(&s));
  s=sample();s.ports[lane].cap_phy=0;assert(sf_tb_retrain_peerless(&s));
  s=sample();s.ports[lane].lane_disabled=1;assert(sf_tb_retrain_peerless(&s));
  s=sample();s.ports[lane].adapter_locked=1;assert(sf_tb_retrain_peerless(&s));
 }
 puts("Peerless retrain policy: both actual unplugged lanes/cable/root validity; reject remote/connected/locked/disabled/error/partial/duplicate observations PASS (policy only, no drain/hardware proof)");
}
