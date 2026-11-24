/* SPDX-License-Identifier: MIT
 * Owned-child Mach sampling discriminator. No external target PID is accepted.
 * The state/frame walk follows sf-sample.c (MIT), Activity Monitor's sampler.
 */
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <pthread.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond=PTHREAD_COND_INITIALIZER;
static volatile sig_atomic_t owned_child=-1;
static void timed_out(int sig){pid_t pid=(pid_t)owned_child;if(pid>1)kill(pid,SIGKILL);_exit(128+sig);}
static void *idle(void *arg){(void)arg;pthread_mutex_lock(&mutex);for(;;)pthread_cond_wait(&cond,&mutex);return NULL;}
static void *ipc_idle(void *arg){
 (void)arg;mach_port_t port=MACH_PORT_NULL;
 kern_return_t kr=mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&port);
 if(kr!=KERN_SUCCESS){printf("TASK_SAMPLE IPC allocate=%d\n",kr);_exit(94);}
 mach_msg_header_t msg;memset(&msg,0,sizeof msg);
 /* No sender and no RCV_INTERRUPT/TIMEOUT option. Report unexpected returns. */
 kr=mach_msg(&msg,MACH_RCV_MSG,0,sizeof msg,port,MACH_MSG_TIMEOUT_NONE,MACH_PORT_NULL);
 printf("TASK_SAMPLE IPC unexpected_receive_return=0x%x\n",kr);_exit(95);
}
static uint64_t ns(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;}
static void child(int ready,int ipc){
 alarm(30);pthread_t workers[32];
 for(unsigned i=0;i<32;i++)if(pthread_create(&workers[i],NULL,ipc&&(i&1)?ipc_idle:idle,NULL))_exit(91);
 if(write(ready,"R",1)!=1)_exit(92);close(ready);
 for(;;)usleep(10000);
}
static int walk(task_t task,thread_t thread,int frames,unsigned long *reads){
 arm_thread_state64_t st;mach_msg_type_number_t count=ARM_THREAD_STATE64_COUNT;
 kern_return_t kr=thread_get_state(thread,ARM_THREAD_STATE64,(thread_state_t)&st,&count);
 if(kr!=KERN_SUCCESS||count!=ARM_THREAD_STATE64_COUNT)return 1;
 if(!frames)return 0;
 uint64_t fp=(uint64_t)__darwin_arm_thread_state64_get_fp(st)&0x0000007fffffffffULL;
 for(unsigned n=0;n<256&&fp&&!(fp&7);n++){
  uint64_t pair[2];mach_vm_size_t got=0;
  kr=mach_vm_read_overwrite(task,fp,sizeof pair,(mach_vm_address_t)(uintptr_t)pair,&got);(*reads)++;
  if(kr!=KERN_SUCCESS||got!=sizeof pair)break;
  uint64_t next=pair[0]&0x0000007fffffffffULL;if(next<=fp)break;fp=next;
 }
 return 0;
}
int main(int argc,char **argv){
 setbuf(stdout,NULL);signal(SIGALRM,timed_out);alarm(25);
 char uuid[128]={0};size_t size=sizeof uuid;
 if((argc!=2&&argc!=3)||(argc==3&&strcmp(argv[2],"ipc"))||sysctlbyname("kern.kernelcacheuuid",uuid,&size,NULL,0)||strcmp(uuid,argv[1])){fputs("REFUSED: exact kernel UUID required; only forks its own target\n",stderr);return 77;}
 int ipc=argc==3;
 for(int mode=0;mode<4;mode++){
  int pipefd[2];if(pipe(pipefd))return 2;pid_t pid=fork();if(pid<0)return 3;
  if(!pid){close(pipefd[0]);child(pipefd[1],ipc);_exit(93);}close(pipefd[1]);char ready=0;
  owned_child=pid;printf("TASK_SAMPLE START mode=%d owned_pid=%d ipc_workers=%d\n",mode,pid,ipc?16:0);
  if(read(pipefd[0],&ready,1)!=1||ready!='R'){kill(pid,SIGKILL);waitpid(pid,NULL,0);return 4;}close(pipefd[0]);
  task_t task=MACH_PORT_NULL;kern_return_t kr=task_for_pid(mach_task_self(),pid,&task);
  if(kr!=KERN_SUCCESS){kill(pid,SIGKILL);waitpid(pid,NULL,0);printf("TASK_SAMPLE FAIL task_for_pid=%d\n",kr);return 5;}
  unsigned samples=0,errors=0,min_threads=~0u,max_threads=0;unsigned long reads=0;uint64_t held=0,worst=0,start=ns();int early=0,status=0;
  while(ns()-start<1000000000ull){
   if(waitpid(pid,&status,WNOHANG)==pid){owned_child=-1;early=1;break;}
   uint64_t before=ns();int suspended=0;
   if(mode){kr=task_suspend(task);if(kr!=KERN_SUCCESS){errors++;break;}suspended=1;}
   thread_act_array_t threads=NULL;mach_msg_type_number_t count=0;
   kr=task_threads(task,&threads,&count);
   if(kr!=KERN_SUCCESS)errors++;
   else{
    if(count<33)errors++;
    if(count<min_threads)min_threads=count;if(count>max_threads)max_threads=count;
    for(mach_msg_type_number_t i=0;i<count;i++){
     if(mode>=2)errors+=(unsigned)walk(task,threads[i],mode==3,&reads);
     mach_port_deallocate(mach_task_self(),threads[i]);
    }
    vm_deallocate(mach_task_self(),(vm_address_t)threads,count*sizeof(thread_t));
   }
   if(suspended){kr=task_resume(task);if(kr!=KERN_SUCCESS)errors++;uint64_t duration=ns()-before;held+=duration;if(duration>worst)worst=duration;}
   samples++;if(errors)break;usleep(2000);
  }
  mach_port_deallocate(mach_task_self(),task);
  if(!early){
   kill(pid,SIGTERM);uint64_t deadline=ns()+2000000000ull;pid_t got;
   do{got=waitpid(pid,&status,WNOHANG);if(got==0)usleep(1000);}while(got==0&&ns()<deadline);
   if(got==0){kill(pid,SIGKILL);do{got=waitpid(pid,&status,0);}while(got<0&&errno==EINTR);}
   if(got!=pid)return 6;
  }
  owned_child=-1;
  printf("TASK_SAMPLE mode=%d samples=%u errors=%u threads_min=%u threads_max=%u reads=%lu suspended_us=%llu max_pause_us=%llu child_early=%d wait_status=%d\n",mode,samples,errors,min_threads,max_threads,reads,(unsigned long long)(held/1000),(unsigned long long)(worst/1000),early,status);
  if(early||errors||samples==0)return 1;
 }
 puts("TASK_SAMPLE PASS owned pthread-idle children only; no Firefox/GPU/whole-desktop proof");return 0;
}
