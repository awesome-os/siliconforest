/* SPDX-License-Identifier: MIT
 * Process-local diagnostic for background-I/O stalls. Calls the real API and
 * never reports a policy change the kernel did not accept. Off unless selected.
 */
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <stdint.h>
#include <unistd.h>

/* Opt-in startup QoS, never affinity/frequency writes. Read-only placement ABI:
 * pinned XNU libsyscall/os/tsd.h and arm64/machine_machdep.h. Log actual API
 * results and CPU separately; later Gecko policy changes remain respected. */
static uint64_t sffx_placement(void){
    uint64_t value;__asm__ volatile("mrs %0, TPIDR_EL0":"=r"(value));return value&0xfffff;
}
/* Diagnostic entry for paired measurements on the same browser main thread.
 * A placement observation is not a claim of fixed affinity or sustained QoS. */
__attribute__((visibility("default")))
int SFFXSetMainQoS(uint32_t requested,uint64_t values[4]){
    if(!values)return EINVAL;
    values[0]=(uint64_t)getpid();values[1]=sffx_placement();
    values[2]=values[1];values[3]=requested;
    if(!pthread_main_np())return EPERM;
    if(requested!=QOS_CLASS_DEFAULT&&requested!=QOS_CLASS_USER_INITIATED&&
       requested!=QOS_CLASS_USER_INTERACTIVE)return EINVAL;
    int status=pthread_set_qos_class_self_np((qos_class_t)requested,0);
    values[2]=sffx_placement();return status;
}
__attribute__((constructor)) static void sffx_initial_qos(void){
    const char *mode=getenv("SFFX_UI_QOS");if(!mode)return;
    qos_class_t requested;
    if(!strcmp(mode,"interactive"))requested=QOS_CLASS_USER_INTERACTIVE;
    else if(!strcmp(mode,"initiated"))requested=QOS_CLASS_USER_INITIATED;
    else{fprintf(stderr,"FXQOS invalid requested mode\n");return;}
    qos_class_t before=0,after=0;int relative_before=0,relative_after=0;
    int (*get_qos)(pthread_t,qos_class_t *,int *)=dlsym(RTLD_DEFAULT,"pthread_get_qos_class_np");
    int before_status=get_qos?get_qos(pthread_self(),&before,&relative_before):ENOSYS;
    uint64_t prior=sffx_placement();
    int status=pthread_set_qos_class_self_np(requested,0);
    int after_status=get_qos?get_qos(pthread_self(),&after,&relative_after):ENOSYS;
    uint64_t current=sffx_placement();
    fprintf(stderr,"FXQOS pid=%d before_status=%d before=%x before_cpu=%u before_cluster=%u requested=%x status=%d after_status=%d after=%x after_cpu=%u after_cluster=%u\n",
        getpid(),before_status,before,(unsigned)(prior&0xfff),(unsigned)(prior>>12),requested,status,after_status,after,(unsigned)(current&0xfff),(unsigned)(current>>12));
}
static int (*real_set)(int,int,int);
static pthread_once_t once=PTHREAD_ONCE_INIT;
static void resolve(void){real_set=dlsym(RTLD_NEXT,"setiopolicy_np");}
static int sffx_set(int type,int scope,int policy){
    pthread_once(&once,resolve);
    if(!real_set){errno=ENOSYS;return -1;}
    int effective=policy;
    const char *mode=getenv("SFFX_IO_POLICY");
    if(type==IOPOL_TYPE_DISK&&scope==IOPOL_SCOPE_THREAD&&policy==IOPOL_THROTTLE&&mode){
        if(!strcmp(mode,"utility"))effective=IOPOL_UTILITY;
        if(!strcmp(mode,"standard"))effective=IOPOL_STANDARD;
        if(!strcmp(mode,"default"))effective=IOPOL_DEFAULT;
    }
    int result=real_set(type,scope,effective),saved=errno;
    if(getenv("SFFX_IO_TRACE"))fprintf(stderr,"FXIOPOL type=%d scope=%d requested=%d effective=%d status=%d actual=%d\n",type,scope,policy,effective,result,getiopolicy_np(type,scope));
    errno=saved;return result;
}
__attribute__((used,section("__DATA,__interpose")))
static const struct{const void *replacement,*original;} interpose={
    (const void *)sffx_set,(const void *)setiopolicy_np
};
