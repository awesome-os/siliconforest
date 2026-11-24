/* SPDX-License-Identifier: MIT
 * Give only this browser family an IOSurface broker at a new private service
 * name. The installed canonical service and framework are unchanged.
 */
#include <mach/mach.h>
#include <servers/bootstrap.h>
#include <spawn.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
static volatile sig_atomic_t stopping;
static void stop(int signal_number){ (void)signal_number;stopping=1; }
static void end_child(pid_t pid){
 int status;
 if(pid<=0)return;
 pid_t got=waitpid(pid,&status,WNOHANG);
 if(got==pid||(got<0&&errno==ECHILD))return;
 kill(pid,SIGTERM);
 for(int i=0;i<20;i++){
  if(waitpid(pid,&status,WNOHANG)==pid)return;
  usleep(50000);
 }
 kill(pid,SIGKILL);while(waitpid(pid,&status,0)<0&&errno==EINTR){}
}
static void remove_service(const char *service){
 kern_return_t removed=bootstrap_register(bootstrap_port,service,MACH_PORT_NULL);
 fprintf(stderr,"FXSURFACE private service removal status=%d\n",removed);
}
int main(int argc,char **argv){
 if(argc<3){fprintf(stderr,"usage: surface-session /private/.../iosurfaced command [args...]\n");return 2;}
 const char *service=getenv("SFFX_IOSURFACE_SERVICE");
 const char *prefix="org.siliconforest.iosurfaced.sffx.";
 if(!service||strncmp(service,prefix,strlen(prefix))||strlen(service)>=128){
  fprintf(stderr,"FXSURFACE private service name required\n");return 3;
 }
 mach_port_t found=MACH_PORT_NULL;
 if(bootstrap_look_up(bootstrap_port,service,&found)==KERN_SUCCESS){
  mach_port_deallocate(mach_task_self(),found);
  fprintf(stderr,"FXSURFACE private name already occupied; refused\n");return 4;
 }
 struct sigaction action={0};action.sa_handler=stop;sigemptyset(&action.sa_mask);
 sigaction(SIGTERM,&action,NULL);sigaction(SIGINT,&action,NULL);
 char *broker_argv[]={argv[1],"--standalone",NULL};pid_t broker=0,child=0;int status=0;
 int error=posix_spawn(&broker,argv[1],NULL,NULL,broker_argv,environ);
 if(error){fprintf(stderr,"FXSURFACE spawn broker error=%d\n",error);return 6;}
 for(int i=0;i<40&&!stopping;i++){
  if(bootstrap_look_up(bootstrap_port,service,&found)==KERN_SUCCESS)break;
  if(waitpid(broker,&status,WNOHANG)==broker){broker=0;break;}
  usleep(50000);
 }
 if(found==MACH_PORT_NULL||stopping){
  end_child(broker);
  if(found!=MACH_PORT_NULL){mach_port_deallocate(mach_task_self(),found);remove_service(service);}
  fprintf(stderr,"FXSURFACE private broker unavailable\n");return 7;
 }
 mach_port_deallocate(mach_task_self(),found);
 fprintf(stderr,"FXSURFACE private named broker ready pid=%d\n",broker);
 error=posix_spawn(&child,argv[2],NULL,NULL,argv+2,environ);
 if(error){end_child(broker);remove_service(service);fprintf(stderr,"FXSURFACE spawn command error=%d\n",error);return 8;}
 while(!stopping){
  pid_t got=waitpid(child,&status,0);
  if(got==child){child=0;break;}
  if(got<0&&errno!=EINTR)break;
 }
 end_child(child);end_child(broker);
 remove_service(service);
 return stopping?128+SIGTERM:WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
}
