/* SPDX-License-Identifier: MIT
 * Register only a fresh own name; verify lookup from an exec-spawned own child.
 * No existing service, external PID, desktop or GPU is changed.
 */
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <servers/bootstrap.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
extern char **environ;
int main(int argc,char **argv){
 setbuf(stdout,NULL);alarm(15);const char *prefix="org.siliconforest.sftb.bootstrap.";
 if(argc==3&&!strcmp(argv[1],"--child")&&!strncmp(argv[2],prefix,strlen(prefix))){
  mach_port_t found=MACH_PORT_NULL;kern_return_t kr=bootstrap_look_up(bootstrap_port,argv[2],&found);
  printf("BOOTSTRAP_CHILD pid=%d bootstrap_port=%u lookup=0x%x valid=%d\n",getpid(),bootstrap_port,kr,MACH_PORT_VALID(found));
  if(MACH_PORT_VALID(found))mach_port_deallocate(mach_task_self(),found);
  return kr==KERN_SUCCESS?0:1;
 }
 char uuid[128]={0};size_t n=sizeof uuid;
 if(argc!=2||sysctlbyname("kern.kernelcacheuuid",uuid,&n,NULL,0)||strcmp(uuid,argv[1]))return 77;
 char name[128];snprintf(name,sizeof name,"%s%d.%llu",prefix,getpid(),(unsigned long long)mach_absolute_time());
 mach_port_t receive=MACH_PORT_NULL;kern_return_t kr=mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&receive);
 if(kr!=KERN_SUCCESS)return 2;
 kr=mach_port_insert_right(mach_task_self(),receive,receive,MACH_MSG_TYPE_MAKE_SEND);
 if(kr!=KERN_SUCCESS)return 3;
 kr=bootstrap_register(bootstrap_port,name,receive);
 printf("BOOTSTRAP_PARENT pid=%d bootstrap_port=%u register=0x%x own_name=%s\n",getpid(),bootstrap_port,kr,name);
 if(kr!=KERN_SUCCESS)return 4;
 pid_t child=0;char *args[]={argv[0],"--child",name,NULL};int error=posix_spawn(&child,argv[0],NULL,NULL,args,environ);int status=0;
 if(!error&&waitpid(child,&status,0)!=child)error=1;
 /* launchd rejects deleting an active receive right. Close ours first and
  * allow its dead-name notification to retire the active service. */
 mach_port_destroy(mach_task_self(),receive);
 for(unsigned i=0;i<20;i++){
  kr=bootstrap_register(bootstrap_port,name,MACH_PORT_NULL);
  if(kr==KERN_SUCCESS)break;
  if(kr!=BOOTSTRAP_SERVICE_ACTIVE)break;
  usleep(50000);
 }
 printf("BOOTSTRAP_END spawn_error=%d child_status=%d unregister=0x%x\n",error,status,kr);
 return !error&&WIFEXITED(status)&&WEXITSTATUS(status)==0&&kr==KERN_SUCCESS?0:5;
}
