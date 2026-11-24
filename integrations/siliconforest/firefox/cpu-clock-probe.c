/* SPDX-License-Identifier: MIT
 * Check whether profiler thread CPU clocks exclude a blocking sleep.
 * An owned, finite diagnostic: no priority, affinity or frequency changes.
 */
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <sys/resource.h>
#include <unistd.h>
static uint64_t clock_ns(clockid_t id,int *status){struct timespec t={0};*status=clock_gettime(id,&t);return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
static uint64_t usage_ns(void){struct rusage r={0};getrusage(RUSAGE_SELF,&r);return ((uint64_t)r.ru_utime.tv_sec+r.ru_stime.tv_sec)*1000000000+((uint64_t)r.ru_utime.tv_usec+r.ru_stime.tv_usec)*1000;}
int main(void){
 alarm(3);
 int status=0;uint64_t checksum=1;unsigned failures=0;
 for(unsigned busy=0;busy<2;busy++){
  uint64_t wall=clock_ns(CLOCK_MONOTONIC,&status),cpu=clock_ns(CLOCK_THREAD_CPUTIME_ID,&status),usage=usage_ns();int initial=status;
  if(busy){while(clock_ns(CLOCK_MONOTONIC,&status)-wall<250000000){for(unsigned i=0;i<1024;i++)checksum=(checksum^i)*UINT64_C(0x2545f4914f6cdd1d);}}
  else{struct timespec sleep={0,250000000};nanosleep(&sleep,NULL);}
  uint64_t elapsed=clock_ns(CLOCK_MONOTONIC,&status)-wall,used=clock_ns(CLOCK_THREAD_CPUTIME_ID,&status)-cpu,process=usage_ns()-usage;
  int pass=!initial&&!status&&(busy?(used>elapsed/4&&used<elapsed*2):(used<elapsed/10));failures+=!pass;
  printf("FXCPUCLOCK phase=%s wall_ns=%llu thread_cpu_ns=%llu process_usage_ns=%llu status=%d initial_status=%d pass=%d checksum=%llu\n",busy?"busy":"sleep",(unsigned long long)elapsed,(unsigned long long)used,(unsigned long long)process,status,initial,pass,(unsigned long long)checksum);
 }
 return failures?1:0;
}
