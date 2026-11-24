#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile actual bridge source; fake only the Linux provider and its caller.

No native connector, DCP activation, callback drain or device lifetime proof.
"""
from pathlib import Path
import os
import subprocess
import tempfile

here = Path(__file__).parents[1]
fixture = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sf_dcp_tunnel_epoch.h"
struct device_node {int value;};
static struct device_node connector;
static unsigned driver_calls, callback_calls, inactive_calls;
static int forced_error, callback_error;
static bool inside_driver;
static int (*saved_callback)(void *,bool);
static void *saved_cookie;
static uint64_t expected_generation=0xf123456789abcdefULL;
static int owner, context, foreign;
static int set_active(void *opaque,uint64_t generation,bool active)
{
 assert(inside_driver && opaque==&context && generation==expected_generation);
 ++callback_calls;if(!active)++inactive_calls;return callback_error;
}
int apple_dcp_tb_dp_tunnel(struct device_node *node,unsigned dpin,uint64_t generation,bool active,
                           int (*callback)(void *,bool),void *cookie)
{
 assert(node==&connector && dpin==1 && generation==expected_generation);
 ++driver_calls;saved_callback=callback;saved_cookie=cookie;inside_driver=true;
 if(!active)assert(callback(cookie,true)==-ESTALE); // No reactivation during detach.
 int result=callback(cookie,active);
 if(active && forced_error && !result)assert(callback(cookie,false)==0); // Mock drain.
 inside_driver=false;return result?result:forced_error;
}
static struct sf_dcp_tunnel_request request(void)
{
 struct sf_dcp_tunnel_request r={0};r.version=1;r.size=sizeof(r);r.typec_port=1;r.dpin=1;
 r.generation=expected_generation;r.active=1;r.set_active=set_active;r.context=&context;return r;
}
int main(void)
{
 assert(sizeof(struct sf_dcp_tunnel_request)==48);
 struct sf_dcp_tunnel_epoch epoch={0};struct sf_dcp_tunnel_request r=request();
 assert(sf_dcp_tunnel_epoch_init(NULL,&connector,&owner)==-EINVAL);
 assert(sf_dcp_tunnel_epoch_init(&epoch,NULL,&owner)==-EINVAL);
 assert(sf_dcp_tunnel_epoch_init(&epoch,&connector,&owner)==0);
 assert(sf_dcp_tunnel_epoch_init(&epoch,&connector,&owner)==-EINVAL);
 for(unsigned n=0;n<9;++n){struct sf_dcp_tunnel_request bad=r;
  switch(n){case 0:bad.version=2;break;case 1:--bad.size;break;case 2:bad.typec_port=0;break;
   case 3:bad.dpin=2;break;case 4:bad.generation=0;break;case 5:bad.active=2;break;
   case 6:bad.reserved=1;break;case 7:bad.context=NULL;break;case 8:bad.set_active=NULL;break;}
  assert(sf_dcp_tunnel_epoch_call(&epoch,&owner,&bad)==-EINVAL && !driver_calls && !epoch.generation);
 }
 assert(sf_dcp_tunnel_epoch_call(&epoch,&foreign,&r)==-EPERM && !driver_calls);
 r.active=0;assert(sf_dcp_tunnel_epoch_call(&epoch,&owner,&r)==-ESTALE);r.active=1;
 assert(sf_dcp_tunnel_epoch_call(&epoch,&owner,&r)==0 && epoch.state==SF_DCP_TUNNEL_ACTIVE);
 assert(driver_calls==1 && callback_calls==1); // Synchronous callback before attach returned.
 for(unsigned n=0;n<4;++n){struct sf_dcp_tunnel_request stale=r;
  switch(n){case 0:stale.generation^=1ULL<<60;break;case 1:stale.dpin=0;break;
   case 2:stale.context=&foreign;break;case 3:stale.set_active=(int (*)(void *,uint64_t,bool))NULL;break;}
  assert(sf_dcp_tunnel_epoch_call(&epoch,&owner,&stale)==(n==3?-EINVAL:-ESTALE) && driver_calls==1);
 }
 // Identical retry reaches the real provider; the bridge does not invent success.
 assert(sf_dcp_tunnel_epoch_call(&epoch,&owner,&r)==0 && driver_calls==2);
 r.active=0;assert(sf_dcp_tunnel_epoch_call(&epoch,&owner,&r)==0);
 assert(epoch.state==SF_DCP_TUNNEL_RETIRED && !epoch.accepting && inactive_calls==1);
 assert(saved_callback(saved_cookie,true)==-ESTALE && saved_callback(saved_cookie,false)==-ESTALE);
 r.active=1;assert(sf_dcp_tunnel_epoch_call(&epoch,&owner,&r)==-ESTALE && driver_calls==3);
 for(unsigned n=0;n<3;++n){struct sf_dcp_tunnel_epoch fresh={0};r=request();
  assert(!sf_dcp_tunnel_epoch_init(&fresh,&connector,&owner));
  forced_error=n==0?-EBUSY:n==1?-EADDRINUSE:-EIO;
  assert(sf_dcp_tunnel_epoch_call(&fresh,&owner,&r)==forced_error && !fresh.accepting);
  assert(saved_callback(saved_cookie,true)==-ESTALE);
  if(n<2){assert(fresh.state==SF_DCP_TUNNEL_BOUND);forced_error=0;
   assert(!sf_dcp_tunnel_epoch_call(&fresh,&owner,&r));
   r.active=0;forced_error=-EIO;
   assert(sf_dcp_tunnel_epoch_call(&fresh,&owner,&r)==-EIO && fresh.state==SF_DCP_TUNNEL_FAILED);
  }else assert(fresh.state==SF_DCP_TUNNEL_FAILED);
  forced_error=0;r.active=1;assert(sf_dcp_tunnel_epoch_call(&fresh,&owner,&r)==-EIO);
 }
 struct sf_dcp_tunnel_epoch callback_failed={0};r=request();
 assert(!sf_dcp_tunnel_epoch_init(&callback_failed,&connector,&owner));callback_error=-ETIMEDOUT;
 assert(sf_dcp_tunnel_epoch_call(&callback_failed,&owner,&r)==-ETIMEDOUT && !callback_failed.accepting);
 assert(callback_failed.state==SF_DCP_TUNNEL_FAILED);
 puts("actual DCP tunnel bridge: ABI/owner/full64bit generation/immutable cookie/synchronous callback/retry/drain/error/retire cases PASS; host provider only");
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    (tmp / 'linux/soc/apple').mkdir(parents=True)
    (tmp / 'linux/errno.h').write_text('#include_next <linux/errno.h>\n')
    (tmp / 'linux/soc/apple/dp-tunnel.h').write_text('''#include <stdint.h>
#include <stdbool.h>
struct device_node;
int apple_dcp_tb_dp_tunnel(struct device_node *,unsigned,uint64_t,bool,int (*)(void *,bool),void *);
''')
    (tmp / 'test.c').write_text(fixture)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I' + str(tmp), '-I' + str(here / 'include'),
                    str(here / 'kext/sf_dcp_tunnel_bridge.c'), str(tmp / 'test.c'),
                    '-o', str(tmp / 'test')], check=True)
    subprocess.run([str(tmp / 'test')], check=True)
