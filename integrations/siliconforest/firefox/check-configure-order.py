#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Host ordering counterexample using selected actual WindowServer functions.

Wayland calls are order-recording stubs, not a protocol server. An unlock hook
forces an unmap between the configure callback's stale check and its ACK. This
checks the source ordering under that schedule; it is not native M1 evidence.
"""
from pathlib import Path
import argparse
import subprocess,tempfile,json,hashlib
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--old',type=Path,required=True)
ap.add_argument('--fixed',type=Path,required=True)
ap.add_argument('--output',type=Path)
args=ap.parse_args()
old=args.old
new=args.fixed
def function(text,name):
 a=text.index('static void '+name+'(');b=text.index('{',a);depth=1;i=b+1
 while depth:
  if text[i]=='{':depth+=1
  elif text[i]=='}':depth-=1
  i+=1
 return text[a:i]
header=r'''
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct wl_surface {int value;}; struct xdg_surface {int value;};struct wl_proxy{int value;};struct wl_callback{int value;};
struct wsWindow {int staleConfigures,windowID,state,configured,attached;void *toplevel;struct wl_surface *surface;};
static struct {pthread_mutex_t queueLock;pthread_cond_t queueReady;void *display;}ws={.queueLock=PTHREAD_MUTEX_INITIALIZER,.queueReady=PTHREAD_COND_INITIALIZER};
static pthread_mutex_t orderLock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t orderCond=PTHREAD_COND_INITIALIZER;
static int checked,unmapped,finished,ack_bad,ack_calls;
static _Thread_local int configure_thread,hooked;
static void gate_unlock(pthread_mutex_t *m){
 pthread_mutex_unlock(m);
 if(configure_thread&&!hooked){hooked=1;pthread_mutex_lock(&orderLock);checked=1;pthread_cond_broadcast(&orderCond);while(!finished)pthread_cond_wait(&orderCond,&orderLock);pthread_mutex_unlock(&orderLock);}
}
#define HIDDEN 1
#define MINIMIZED 2
static int unmapListener;
static unsigned wl_proxy_get_id(struct wl_proxy *p){(void)p;return 1;}
static void xdg_surface_ack_configure(struct xdg_surface *s,uint32_t n){(void)s;(void)n;ack_calls++;if(unmapped)ack_bad++;}
static void wl_surface_attach(struct wl_surface *s,void *b,int x,int y){(void)s;(void)b;(void)x;(void)y;}
static void wl_surface_commit(struct wl_surface *s){(void)s;unmapped=1;}
static void wl_display_flush(void *d){(void)d;}
static struct wl_callback *wl_display_sync(void *d){(void)d;static struct wl_callback cb;return &cb;}
static void wl_callback_add_listener(struct wl_callback *p,void *l,void *v){(void)p;(void)l;(void)v;}
static int windowShowFront(struct wsWindow *w){(void)w;return 0;}
'''
footer=r'''
static void *configure(void *v){configure_thread=1;struct xdg_surface s={0};xdgSurfaceConfigure(v,&s,123);return NULL;}
int main(void){
 struct wl_surface s={0};struct wsWindow w={.windowID=1,.state=HIDDEN,.attached=1,.configured=1,.toplevel=(void*)1,.surface=&s};pthread_t thread;
 if(pthread_create(&thread,NULL,configure,&w))return 2;
 pthread_mutex_lock(&orderLock);while(!checked)pthread_cond_wait(&orderCond,&orderLock);pthread_mutex_unlock(&orderLock);
 windowUnmap(&w);
 pthread_mutex_lock(&orderLock);finished=1;pthread_cond_broadcast(&orderCond);pthread_mutex_unlock(&orderLock);pthread_join(thread,NULL);
 printf("ack_calls=%d ack_after_null_commit=%d\n",ack_calls,ack_bad);return ack_calls==1?ack_bad:2;
}
'''
rows=[]
with tempfile.TemporaryDirectory(prefix='sffx-configure-order-')as t:
 for name,p in [('old',old),('fixed',new)]:
  text=p.read_text();body='\n'.join(function(text,n)for n in ['xdgSurfaceConfigure','windowUnmap']).replace('pthread_mutex_unlock(', 'gate_unlock(')
  src=Path(t)/(name+'.c');src.write_text(header+body+footer);exe=Path(t)/name
  c=subprocess.run(['cc','-std=c11','-pthread','-O1',str(src),'-o',str(exe)],capture_output=True,text=True);c.check_returncode()
  r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=5)
  rows.append(dict(case=name,source_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),returncode=r.returncode,stdout=r.stdout,stderr=r.stderr))
assert [r['returncode']for r in rows]==[1,0],rows
result=dict(host_only=True,actual_selected_function_bodies=True,unlock_hook_forces_unmap_between_check_and_ack=True,wayland_calls_are_order_recording_stubs=True,native_protocol_qualified=False,cases=rows)
if args.output:args.output.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
