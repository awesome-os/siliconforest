#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Execute actual native notification glue against explicit host fixtures.

No CM, provider, PHY, MMIO, HPD or external display execution is established.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

here = Path(__file__).parents[1]
s = (here / 'native/sf_tb_nhi.c').read_text()
functions = []
for name in ['present', 'fail', 'dp_index', 'dp_changed', 'dp_waiting']:
    match = re.search(r'static (?:int|bool|void)\s+' + name + r'\([^{};]*\)\s*\{', s)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (s[end] == '{') - (s[end] == '}')
        end += 1
    functions.append(s[match.start():end])
fixture = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "sf_tb_nhi.h"
typedef uint8_t u8;typedef uint64_t u64;
struct tb_switch;
struct tb_port {struct tb_switch *sw;unsigned port;bool dpin;};
struct tb_switch {struct tb_port ports[16];};
struct tb {struct tb_switch *root_switch;};
struct sf_tb_nhi;
struct tb_nhi {struct sf_tb_nhi *owner;};
struct sf_tb_nhi {struct tb_nhi nhi;struct sf_tb_nhi_config config;struct tb *domain;bool failed;};
static struct sf_tb_nhi *owner(struct tb_nhi *nhi){return nhi->owner;}
static bool tb_port_is_dpin(struct tb_port *p){return p->dpin;}
#define tb_switch_for_each_port(root,p) for((p)=(root)->ports;(p)<(root)->ports+16;++(p))
static unsigned quarantines,faults;
static void sf_tb_dma_quarantine(void){++quarantines;}
static void sf_tb_native_fault(int error){assert(error==-EIO);++faults;}
FUNCTIONS
struct fixture {bool powered,waiting;int error;unsigned calls,index,port;uint64_t generation;bool active;};
static bool power(void *p){return ((struct fixture *)p)->powered;}
static int changed(void *p,unsigned index,uint32_t port,uint64_t generation,bool active)
{
 struct fixture *f=p;++f->calls;f->index=index;f->port=port;f->generation=generation;f->active=active;return f->error;
}
static bool waiting(void *p,unsigned index){struct fixture *f=p;f->index=index;return f->waiting;}
int main(void)
{
 struct tb_switch root={0},foreign={0};
 for(unsigned n=0;n<16;++n){root.ports[n].sw=&root;root.ports[n].port=n;foreign.ports[n].sw=&foreign;foreign.ports[n].port=n;}
 root.ports[7].dpin=root.ports[12].dpin=true;foreign.ports[7].dpin=true;
 struct tb domain={&root};struct fixture f={.powered=true};
 struct sf_tb_nhi s={0};s.nhi.owner=&s;s.domain=&domain;
 s.config.ctx=&f;s.config.powered=power;s.config.dp_changed=changed;s.config.dp_waiting=waiting;
 assert(dp_index(&s,&root.ports[7])==0 && dp_index(&s,&root.ports[12])==1);
 assert(dp_index(&s,NULL)==-ENODEV && dp_index(&s,&root.ports[6])==-ENODEV && dp_index(&s,&foreign.ports[7])==-ENODEV);
 root.ports[14].dpin=true;assert(dp_index(&s,&root.ports[14])==-ERANGE);root.ports[14].dpin=false;
 uint64_t token=0x123456789abcdef0ULL;
 dp_changed(&s.nhi,12,token,true);
 assert(f.calls==1 && f.index==1 && f.port==12 && f.generation==token && f.active && !s.failed);
 f.error=-ESTALE;dp_changed(&s.nhi,7,token-1,true);
 assert(f.calls==2 && !s.failed && !faults && !quarantines);
 f.error=0;f.powered=false;dp_changed(&s.nhi,12,token,false);
 assert(f.calls==3 && !f.active && !s.failed); // Retirement notification still reaches owner.
 assert(!dp_waiting(&s.nhi,&root.ports[7]));
 f.powered=f.waiting=true;assert(dp_waiting(&s.nhi,&root.ports[7]) && f.index==0);
 f.waiting=false;assert(!dp_waiting(&s.nhi,&root.ports[12]));
 f.error=-EIO;dp_changed(&s.nhi,7,token,true);
 assert(s.failed && faults==1 && quarantines==1);
 s.failed=false;f.error=0;unsigned calls=f.calls;dp_changed(&s.nhi,6,token,true);
 assert(s.failed && f.calls==calls && faults==2 && quarantines==2);
 s.failed=false;s.config.dp_changed=NULL;dp_changed(&s.nhi,7,token,true);
 assert(s.failed && f.calls==calls && faults==3 && quarantines==3);
 puts("actual native DP notification glue: root-only ordered DPIN mapping, full64-bit generation, stale refusal, powered retirement delivery, factual capacity and failure quarantine PASS (host fixtures only)");
}
'''.replace('FUNCTIONS', '\n'.join(functions))
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    (temp / 'test.c').write_text(fixture)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', '-g',
                    '-fsanitize=address,undefined', '-I', str(here / 'native'),
                    str(temp / 'test.c'), '-o', str(temp / 'test')], check=True)
    subprocess.run([str(temp / 'test')], check=True)
