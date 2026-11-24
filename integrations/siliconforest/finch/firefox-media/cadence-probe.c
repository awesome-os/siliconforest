/* SPDX-License-Identifier: MIT */
/* Public CV callback/handler forwarding control; no GUI or GL context. */
#include <CoreVideo/CVDisplayLink.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef SF_HAVE_CADENCE
extern void SFMediaCadenceFlush(void);
#endif
static unsigned calls[2],bad;
static CVDisplayLinkRef expected;
static int cookie;
static CVReturn receive(unsigned phase,CVDisplayLinkRef link,const CVTimeStamp *now,const CVTimeStamp *out,CVOptionFlags input,CVOptionFlags *result,void *data)
{
 if(link!=expected||!now||!out||!now->hostTime||!out->hostTime||!result||data!=&cookie) __atomic_fetch_add(&bad,1,__ATOMIC_RELAXED);
 (void)input;
 if(result)*result=0;
 __atomic_fetch_add(&calls[phase],1,__ATOMIC_RELAXED);
 return 0;
}
static CVReturn callback(CVDisplayLinkRef link,const CVTimeStamp *now,const CVTimeStamp *out,CVOptionFlags input,CVOptionFlags *result,void *data)
{return receive(0,link,now,out,input,result,data);}
static void need(int okay,const char *what){if(!okay){fprintf(stderr,"CADENCE_PROBE_FAIL %s\n",what);exit(1);}}
int main(void)
{
 setvbuf(stdout,NULL,_IOLBF,0);
 need(CVDisplayLinkCreateWithActiveCGDisplays(&expected)==0&&expected,"create");
 CVTime nominal=CVDisplayLinkGetNominalOutputVideoRefreshPeriod(expected);
 printf("CADENCE_PROBE nominal-value=%lld nominal-scale=%d actual-seconds=%.17g\n",(long long)nominal.timeValue,nominal.timeScale,CVDisplayLinkGetActualOutputVideoRefreshPeriod(expected));
 need(CVDisplayLinkSetOutputCallback(expected,callback,&cookie)==0,"callback registration");
 need(CVDisplayLinkStart(expected)==0,"callback start");usleep(1800000);need(CVDisplayLinkStop(expected)==0,"callback stop");
 CVDisplayLinkRelease(expected);
#ifdef SF_HAVE_CADENCE
 SFMediaCadenceFlush();
#endif
 printf("CADENCE_PROBE_DONE callbacks=%u handlers=%u forwarding-errors=%u diagnostic-only=1\n",calls[0],calls[1],bad);
 return !calls[0]||bad?1:0;
}
