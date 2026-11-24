// SPDX-License-Identifier: MIT
// Actual patched getter with fake sessions/cookies; no kernel/DART/IRQ execution.
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <sfavd/sfavd_ioctl.h>
@STATS@
struct Session { sf_avd_stats stats; };
struct DmaCookie { size_t bytes; uint64_t iova; bool prepared; };
struct Context { Session *session; int owner,ownerTask;uint64_t generation,apiDecodeCalls,apiDecodeErrors;unsigned buffers;bool imported,submitted; };
static Context contexts[2];static DmaCookie cookies[2];static struct {bool poisoned;} gDart;
static int pid=100,task=7,buffer_calls=0,read_calls=0;static bool bad_cookie;
static int proc_selfpid(){return pid;}static int current_task(){return task;}
static Context *context_of(uint32_t ctx){return ctx<2&&contexts[ctx].session?&contexts[ctx]:nullptr;}
static void sf_avd_session_stats(Session *s,sf_avd_stats *out){read_calls++;*out=s->stats;}
static int sf_avd_session_buffer(Session *,unsigned i,void **cpu,size_t *size,void **cookie){buffer_calls++;if(i>=2)return -EINVAL;*cpu=nullptr;*size=cookies[i].bytes;*cookie=bad_cookie?nullptr:&cookies[i];return 0;}
@GETTER@
static sfavd_context_stats query(){sfavd_context_stats q={};q.size=sizeof q;q.version=1;q.generation=11;return q;}
static void refused(sfavd_context_stats q,int error=EINVAL){auto before=q;int calls=buffer_calls,reads=read_calls;assert(do_context_stats(&q)==error);assert(!std::memcmp(&q,&before,sizeof q));if(error==EINVAL){assert(calls==buffer_calls&&reads==read_calls);}}
int main(){
 static_assert(sizeof(sfavd_context_stats)==128);static_assert(offsetof(sfavd_context_stats,generation)==16);static_assert(offsetof(sfavd_context_stats,jobs)==24);
 Session own={},foreign={};own.stats.jobs=10;own.stats.done=9;own.stats.errors=1;own.stats.total_decode_us=1000;foreign.stats.jobs=585;
 contexts[0]={&own,100,7,11,12,2,2,true,false};contexts[1]={&foreign,200,8,12,585,0,2,false,true};
 cookies[0]={16385,0x10000,true};cookies[1]={1,0,false};
 auto q=query();assert(do_context_stats(&q)==0);assert(q.jobs==10&&q.done==9&&q.errors==1&&q.timeouts==0&&q.total_decode_us==1000);assert(q.capture_backing_bytes==49152&&q.capture_iova_bytes==32768&&q.capture_prepared_bytes==32768);assert(q.capture_buffers==2&&q.flags==7&&q.reserved==0&&q.api_decode_calls==12&&q.api_decode_errors==2);
 auto repeated=q;assert(do_context_stats(&repeated)==0);assert(!std::memcmp(&q,&repeated,sizeof q));
 auto unknown=query();unknown.generation=0;unknown.flags=~0u;assert(do_context_stats(&unknown)==0&&unknown.generation==11&&unknown.flags==7);
 q=query();q.size--;refused(q);q=query();q.version=2;refused(q);q=query();q.reserved=1;refused(q);q=query();q.ctx=2;refused(q);q=query();q.ctx=1;refused(q);q=query();q.generation=12;refused(q);
 task=8;refused(query());task=7;pid=200;refused(query());pid=100;
 contexts[0].session=nullptr;refused(query());contexts[0].session=&own;contexts[0].generation=13;refused(query());contexts[0].generation=11;
 bad_cookie=true;refused(query(),EIO);bad_cookie=false;cookies[0].bytes=SIZE_MAX;refused(query(),EOVERFLOW);cookies[0].bytes=16385;
 gDart.poisoned=true;q=query();assert(do_context_stats(&q)==0&&(q.flags&SFAVD_CTX_STATS_GLOBAL_POISONED));gDart.poisoned=false;
 for(unsigned i=0;i<1000;i++){foreign.stats.jobs+=100;foreign.stats.done+=99;auto before=contexts[0];q=query();assert(do_context_stats(&q)==0&&q.jobs==10&&q.done==9);assert(!std::memcmp(&before,&contexts[0],sizeof before));}
 puts("PASS actual getter: 128-byte ABI, owner/task/generation/version/size/reserved guards, repeated reads, local capture scope, poison diagnostics, overflow/failed-cookie, foreign stats independence");
 puts("LIMIT: fake metadata providers; no real IRQ/DART/job/pin/native/retirement proof");
}
