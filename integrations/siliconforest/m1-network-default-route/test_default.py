"""Host faults, actual extracted syscall control flow, and mutation checks.
No Darwin executable, native route syscall, interface or target is exercised.
"""
from pathlib import Path
import json,subprocess,tempfile
HERE=Path(__file__).resolve().parent
FLAGS=['-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-O1','-g']
SAN=['-fsanitize=address,undefined','-fno-sanitize-recover=all']

def run(cmd,okay=True):
 p=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,timeout=60)
 if okay and p.returncode:raise RuntimeError(p.stdout+p.stderr)
 return p

def extract(source,name):
 start=source.index('static int '+name+'(');pos=source.index('{',start);depth=0
 for i in range(pos,len(source)):
  if source[i]=='{':depth+=1
  if source[i]=='}':
   depth-=1
   if not depth:return source[start:i+1]+'\n'
 raise ValueError(name)

HARNESS=r'''
#include "native.h"
#include "route_wire.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
static int writes,reads,closes,write_error,close_error,lost_ack,last_type,now_calls;
static const struct dlf_owner owner={4,"utun1"};
static uint64_t millis(void){return 1;}
static int wait_fd(int fd,int wr,uint64_t end){(void)fd;(void)end;now_calls++;if(!wr&&lost_ack){errno=ETIMEDOUT;return -1;}return 0;}
static void record(struct dlf_native *s,const char *k,const uint8_t *b,size_t n){(void)s;(void)k;(void)b;(void)n;errno=EPIPE;}
static int fake_socket(int d,int t,int p){(void)d;(void)t;(void)p;return 7;}
static int fake_fcntl(int fd,int command,...){(void)fd;(void)command;return 0;}
static int fake_getpid(void){return 42;}
static ssize_t fake_write(int fd,const void *data,size_t n){(void)fd;writes++;last_type=((const uint8_t *)data)[3];if(write_error&&writes==1){errno=write_error;return -1;}return (ssize_t)n;}
static ssize_t fake_recv(int fd,void *out,size_t n,int flags){
 (void)fd;(void)flags;reads++;size_t length=0;
 assert(!dlf_encode(out,n,&length,42,(uint8_t)last_type,last_type==1?&owner:NULL));
 uint8_t *b=out;if(last_type==1){b[8]=0x43;b[9]=8;}else b[24]=3;
 return (ssize_t)length;
}
static int fake_close(int fd){(void)fd;closes++;if(close_error){errno=EIO;return -1;}return 0;}
#define socket fake_socket
#define fcntl fake_fcntl
#define getpid fake_getpid
#define write fake_write
#define recv fake_recv
#define close fake_close
__EXCHANGE__
int main(void){
 struct dlf_native context={0};struct dlf_route out;
 assert(!exchange(&context,1,&owner,&out));assert(writes==1&&reads==1&&closes==1);
 writes=reads=closes=0;write_error=EINTR;assert(exchange(&context,1,&owner,&out));assert(writes==1&&reads==0&&closes==1&&context.last_errno==EINTR);
 writes=reads=closes=0;write_error=EAGAIN;assert(exchange(&context,1,&owner,&out));assert(writes==1&&reads==0&&closes==1);
 writes=reads=closes=0;write_error=EEXIST;assert(exchange(&context,1,&owner,&out));assert(writes==1&&reads==0&&closes==1);
 writes=reads=closes=0;write_error=EINTR;assert(!exchange(&context,4,NULL,&out));assert(writes==2&&reads==1&&closes==1&&out.error==3);
 writes=reads=closes=0;write_error=0;lost_ack=1;assert(exchange(&context,1,&owner,&out));assert(writes==1&&reads==0&&closes==1);
 writes=reads=closes=0;lost_ack=0;close_error=1;memset(&out,0xa5,sizeof(out));struct dlf_route before=out;
 assert(exchange(&context,1,&owner,&out));assert(writes==1&&reads==1&&closes==1&&!memcmp(&out,&before,sizeof(out)));
 puts("actual route exchange: 7 errno/lost-ACK/close cases PASS");return 0;
}
'''

def main():
 result={'scope':'Host-only portable/fake-syscall checks, never kernel runtime','groups':[],'mutations':[]}
 with tempfile.TemporaryDirectory(prefix='darwin-default-tests-') as temp:
  tmp=Path(temp)
  for test,sources in [('test_default',['default_route.c','route_wire.c']),('test_inventory',['inventory.c','route_wire.c'])]:
   exe=tmp/test;run(['clang',*FLAGS,*SAN,*[HERE/s for s in sources],HERE/(test+'.c'),'-o',exe]);p=run([exe]);result['groups'].append(p.stdout.strip())
   ndebug=run(['clang',*FLAGS,'-DNDEBUG','-c',HERE/(test+'.c'),'-o',tmp/'bad.o'],False)
   assert ndebug.returncode and 'assertions required' in ndebug.stderr
  native=(HERE/'native.c').read_text();h=tmp/'native_exchange.c';h.write_text(HARNESS.replace('__EXCHANGE__',extract(native,'exchange')))
  exe=tmp/'native_exchange';run(['clang',*FLAGS,*SAN,'-I',HERE,h,HERE/'route_wire.c','-o',exe]);result['groups'].append(run([exe]).stdout.strip())
  mutations=[
   ('lost_ack_success','default_route.c','if(s->ops->add_default(s->context,&s->owner))','if(0 && s->ops->add_default(s->context,&s->owner))'),
   ('skip_owner_close','default_route.c','int closed=s->ops->close_owner(s->context);','int closed=0;'),
   ('skip_readback_binding','default_route.c','!dlf_route_matches(&r,&s->owner)','0'),
   ('ignore_flags','default_route.c','!(r->flags&~DLF_ALLOWED_FLAGS)','1'),
   ('ignore_ifa','default_route.c','r->ifa==DLF_LOCAL','1'),
   ('foreign_route_absent','default_route.c','return r->error==3 && r->have_destination && !r->destination;','return r->have_destination;'),
   ('delete_encoder','route_wire.c','(type!=1&&type!=4)','(type!=1&&type!=2&&type!=4)'),
   ('request_zero_mask','route_wire.c','addr(b+pos,0);pos+=16;b[pos]','b[pos]=0;pos+=16;b[pos]'),
  ]
  for label,file,before,after in mutations:
   source=(HERE/file).read_text();assert before in source,label;mut=tmp/(label+'.c');mut.write_text(source.replace(before,after))
   other=HERE/('route_wire.c' if file=='default_route.c' else 'default_route.c');exe=tmp/label
   run(['clang',*FLAGS,*SAN,'-I',HERE,mut,other,HERE/'test_default.c','-o',exe]);p=run([exe],False)
   if not p.returncode or 'Assertion' not in p.stderr:raise RuntimeError('survived mutation '+label+'\n'+p.stderr)
   result['mutations'].append(label)
  original=(HERE/'inventory.c').read_text()
  for label,before,after in [
   ('ignore_scoped_default','if(dst_zero&&mask_zero&&!(k->flags&4u))','if(dst_zero&&mask_zero&&!(k->flags&4u)&&!(k->flags&0x01000000u))'),
   ('ignore_route_config','!memcmp(a->key,b->key,a->count*sizeof(a->key[0]))','1')]:
   assert before in original;mut=tmp/(label+'.c');mut.write_text(original.replace(before,after));exe=tmp/label
   run(['clang',*FLAGS,*SAN,'-I',HERE,mut,HERE/'route_wire.c',HERE/'test_inventory.c','-o',exe]);p=run([exe],False)
   assert p.returncode and 'Assertion' in p.stderr;result['mutations'].append(label)
  # Actual syscall-control-flow mutation: ADD must never retry EINTR/EAGAIN.
  bad=native.replace('if(type==4&&n<0&&(errno==EINTR||errno==EAGAIN))continue;','if(n<0&&(errno==EINTR||errno==EAGAIN))continue;')
  assert bad!=native;h.write_text(HARNESS.replace('__EXCHANGE__',extract(bad,'exchange')));exe=tmp/'retry_add'
  run(['clang',*FLAGS,*SAN,'-I',HERE,h,HERE/'route_wire.c','-o',exe]);p=run([exe],False)
  assert p.returncode and 'Assertion' in p.stderr;result['mutations'].append('actual_native_ADD_retry')
 print(json.dumps(result,indent=2))
if __name__=='__main__':main()
