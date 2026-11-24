"""Extract actual guest whitespace parser and queues; execute on host pipes.
No Darwin syscalls, fake target, serial device or hardware is involved.
"""
from pathlib import Path
import tempfile,subprocess
ROOT=Path(__file__).resolve().parents[4]
src=(Path(__file__).parent/'net_egress.c').read_text()
def function(name):
 start=src.index('static int '+name+'(');opening=src.index('{',start);depth=0
 for i in range(opening,len(src)):
  if src[i]=='{':depth+=1
  elif src[i]=='}':
   depth-=1
   if not depth:return src[start:i+1]+'\n'
 raise ValueError(name)
types=src[src.index('#define QMAX'):src.index('static volatile sig_atomic_t stopped;')]
head='''#define _DEFAULT_SOURCE
#include "transport.h"
#include "owned_interfaces.h"
#include "egress_validate.h"
#include <sys/socket.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <termios.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#ifdef NDEBUG
#error assertions required
#endif
'''
main=r'''
static unsigned checks;
static void check(int yes) { checks++;assert(yes); }
static void reset(struct state *s,int *writer) {
 int f[2];check(pipe(f)==0);check(dup2(f[0],0)==0);close(f[0]);*writer=f[1];check(fcntl(0,F_SETFL,O_NONBLOCK)==0);memset(s,0,sizeof(*s));uint8_t nonce[24]={0};check(dln_rx_init(&s->rx,nonce,2)==0);
}
static void input(struct state *s,int w,const uint8_t *raw,size_t n,unsigned stride,int good) {
 uint8_t wire[DLNET_FRAME_BYTES*24];size_t used=0;
 for(size_t i=0;i<n;i++)for(int b=7;b>=0;b--){wire[used++]=' ';if(raw[i]&(1u<<b))wire[used++]=' ';wire[used++]='\n';}
 for(size_t i=0;i<used;i+=stride){size_t k=used-i;if(k>stride)k=stride;check(write(w,wire+i,k)==(ssize_t)k);int rc=read_console(s);if(rc){check(!good);return;}}
 check(good);
}
static size_t encode(struct dln_tx *t,uint8_t type,const uint8_t *p,size_t n,uint8_t *out) {size_t written=0;check(dln_encode(t,type,p,n,out,DLNET_FRAME_BYTES,&written)==0);return written;}
int main(void) {
 struct state *s=calloc(1,sizeof(*s));check(s!=NULL);uint8_t frame[DLNET_FRAME_BYTES],zero[24]={0};unsigned cases=0;
 for(unsigned stride=1;stride<=256;stride*=2){int w;reset(s,&w);struct dln_tx t;check(dln_tx_init(&t,zero,2)==0);size_t n=encode(&t,1,NULL,0,frame);input(s,w,frame,n,stride,1);check(s->peer_open&&s->frames_in==1&&!s->error);
 uint8_t ip[20]={0x45,0,0,20,0,0,0,0,64,17,0,0,10,0,2,2,10,0,2,15};unsigned sum=0;for(unsigned j=0;j<20;j+=2)sum+=(ip[j]<<8)|ip[j+1];while(sum>>16)sum=(sum&65535)+(sum>>16);sum=(~sum)&65535;ip[10]=(uint8_t)(sum>>8);ip[11]=(uint8_t)sum;
 n=encode(&t,2,ip,sizeof(ip),frame);input(s,w,frame,n,stride,1);check(s->input.count==1&&s->packets_in==1);check(s->input.item[0].length==24&&!memcmp(s->input.item[0].data+4,ip,20));s->closing=1;n=encode(&t,3,NULL,0,frame);input(s,w,frame,n,stride,1);check(s->peer_close&&s->bit_count==0&&s->line_used==0&&dln_end(&s->rx)==0);close(w);cases++;}
 for(unsigned b=0;b<256;b++){if(b==' '||b=='\n')continue;int w;reset(s,&w);uint8_t one=(uint8_t)b;check(write(w,&one,1)==1);check(read_console(s)<0&&s->error);close(w);cases++;}
 for(unsigned spaces=0;spaces<=3;spaces++){if(spaces==1||spaces==2)continue;int w;reset(s,&w);char line[4]={' ',' ',' ','\n'};check(write(w,line+3-spaces,spaces+1)==(ssize_t)spaces+1);check(read_console(s)<0);close(w);cases++;}
 int w;reset(s,&w);struct dln_tx t;check(dln_tx_init(&t,zero,2)==0);size_t n=encode(&t,1,NULL,0,frame);frame[55]^=1;input(s,w,frame,n,64,0);check(!s->peer_open&&s->error);close(w);cases++;
 reset(s,&w);s->console_bytes_in=131071;check(write(w," \n",2)==2);check(read_console(s)<0&&!strcmp(s->error,"ingress_byte_cap"));close(w);cases++;
 reset(s,&w);close(w);check(read_console(s)<0&&!strcmp(s->error,"console_eof"));cases++;
 reset(s,&w);check(dln_tx_init(&t,zero,2)==0);n=encode(&t,1,NULL,0,frame);
 input(s,w,frame,16,64,1);check(s->line_used==0&&s->bit_count==0&&s->rx.used==16&&!can_transition(s));
 input(s,w,frame+16,n-16,64,1);check(s->peer_open&&can_transition(s));
 check(push(&s->output,(const uint8_t *)"x",1)==0&&!can_transition(s));s->output.count=0;
 check(push(&s->input,(const uint8_t *)"x",1)==0&&!can_transition(s));close(w);cases++;
 free(s);printf("actual parser %u cases %u checks PASS\n",cases,checks);return 0;
}
'''
# Python raw string above intentionally contains escaped line spellings for C.
main=main.replace("'\\\\n'", "'\\n'").replace('" \\\\n"','" \\n"')
with tempfile.TemporaryDirectory(prefix='darwin-link-parser-') as temp:
 p=Path(temp)/'test.c';binary=Path(temp)/'test';p.write_text(head+types+''.join(function(n) for n in ['fail','push','can_transition','received','line_received','read_console'])+main)
 codec=ROOT/'integrations/puredarwin/m1-network-transport/codec'
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(codec),'-I'+str(ROOT/'integrations/puredarwin/m1-network-egress-guest/codec'),'-I'+str(ROOT/'integrations/puredarwin/m1-network-egress-guest/interfaces'),str(p),str(codec/'transport.c'),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True,timeout=10)
