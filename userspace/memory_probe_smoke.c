/* Original Darwin Lab code. License selection pending.
 * --posix-control exercises actual host mmap/fork/exit with a disposable file.
 * --device requires explicit opt-in and talks ONLY to the experimental protocol.
 * Control success is NOT an XNU/provider test. No implicit device discovery.
 */
#define _POSIX_C_SOURCE 200809L
#include "memory_probe_ioctl.h"
#include "memory_probe_result.h"
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

static unsigned checks;
#define REQUIRE(x, label) do { if (!(x)) { fprintf(stderr,"FAIL: %s (errno=%d)\n",label,errno); goto cleanup; } ++checks; } while(0)
static int byte_read(int fd,char *out) {
    struct pollfd p={fd,POLLIN,0};
    int n;do { n=poll(&p,1,10000); } while(n<0&&errno==EINTR);
    if(n!=1 || !(p.revents&(POLLIN|POLLHUP))) return -1;
    ssize_t got;do{got=read(fd,out,1);}while(got<0&&errno==EINTR);
    return got==1?0:-1;
}
static int byte_write(int fd,char ch) {
    ssize_t n;do{n=write(fd,&ch,1);}while(n<0&&errno==EINTR);
    return n==1?0:-1;
}
static int collect(int fd,struct dlmp_collect *c) {
    *c=(struct dlmp_collect){.version=DLMP_VERSION};
    return ioctl(fd,DLMP_COLLECT,c);
}
static int stats(int fd,struct dlmp_stats *s) {
    *s=(struct dlmp_stats){.version=DLMP_VERSION};
    return ioctl(fd,DLMP_STATS,s);
}
static void diagnose(const char *stage,const struct dlmp_stats *baseline,
                     const struct dlmp_stats *s,const struct dlmp_collect *c,
                     uint64_t total_drained,uint64_t total_reclaimed) {
    fprintf(stderr,"%s: baseline published=%" PRIu64 " drained=%" PRIu64 " reclaimed=%" PRIu64
            "; stats sessions=%" PRIu64 " bytes=%" PRIu64 " live=%" PRIu64
            " published=%" PRIu64 " pending=%" PRIu64 " drained=%" PRIu64
            " reclaimed=%" PRIu64 " quarantined=%" PRIu64
            "; collect drained=%" PRIu64 " reclaimed=%" PRIu64 " retained=%" PRIu64
            " failures=%" PRIu64 " quarantined=%" PRIu64
            "; totals drained=%" PRIu64 " reclaimed=%" PRIu64 "\n",
            stage,baseline->published_maps,baseline->drained_maps,baseline->reclaimed_buffers,
            s->open_sessions,s->resident_bytes,s->live_buffers,s->published_maps,s->pending_maps,
            s->drained_maps,s->reclaimed_buffers,s->quarantined_buffers,c->drained,c->reclaimed,
            c->retained,c->backend_failures,c->quarantined,total_drained,total_reclaimed);
}
static int run(const char *device,int control,int unmapped) {
    int result=1,fd=-1,monitor=-1,child_to_parent[2]={-1,-1},parent_to_child[2]={-1,-1};
    pid_t child=-1;
    unsigned char *a=MAP_FAILED,*b=MAP_FAILED;
    size_t bytes=0;
    uint64_t capabilities=0,total_drained=0,total_reclaimed=0;
    unsigned checkpoints=0;
    struct dlmp_stats s={0},baseline={0};
    struct dlmp_collect c={0};
    const char *backend=control?"posix-control":"experimental-xnu-device";
    if(control) {
        char path[]="/tmp/darwin-lab-control-XXXXXX";
        fd=mkstemp(path);REQUIRE(fd>=0,"create disposable control file");
        if(unlink(path)!=0) { close(fd);fd=-1;perror("unlink control file");goto cleanup; }
        long page=sysconf(_SC_PAGESIZE);
        REQUIRE(page>0 && page<=65536,"host page size");bytes=(size_t)page*2;
        REQUIRE(ftruncate(fd,(off_t)bytes)==0,"size control file");
    } else {
        fd=open(device,O_RDWR|O_CLOEXEC);REQUIRE(fd>=0,"open experimental device");
        monitor=open(device,O_RDWR|O_CLOEXEC);REQUIRE(monitor>=0,"open monitor session");
        struct dlmp_info i={.version=DLMP_VERSION};
        REQUIRE(ioctl(fd,DLMP_INFO,&i)==0,"query probe identity");
        REQUIRE(i.version==DLMP_VERSION && !i.flags && dlmp_result_capabilities(i.capabilities),
                "kernel RAM-probe identity");
        REQUIRE(i.page_size==4096||i.page_size==16384||i.page_size==65536,"supported page size");
        capabilities=i.capabilities;bytes=(size_t)i.page_size*2;
        REQUIRE(stats(monitor,&baseline)==0 && dlmp_result_baseline(&baseline),
                "clean baseline with two copied-out sessions");
        struct dlmp_alloc request={.version=DLMP_VERSION,.bytes=i.page_size+1};
        REQUIRE(ioctl(fd,DLMP_ALLOC,&request)==0 && request.version==DLMP_VERSION &&
                !request.flags && request.object_id && request.bytes==bytes,
                "allocate and round page-plus-one kernel backing");
        REQUIRE(stats(monitor,&s)==0 && dlmp_result_allocated(bytes,&baseline,&s),
                "allocated backing and historical counters");
        struct dlmp_stats owner={0};
        REQUIRE(stats(fd,&owner)==0 && owner.version==DLMP_VERSION && !owner.flags &&
                owner.own_object==request.object_id && owner.own_bytes==bytes,
                "owner session is bound to the returned allocation");
        if(unmapped) {
            REQUIRE(close(fd)==0,"close unmapped owner");fd=-1;
            REQUIRE(collect(monitor,&c)==0 && stats(monitor,&s)==0 &&
                    dlmp_result_unmapped(&baseline,&s,&c),"reclaim exactly one unexposed allocation");
            result=0;goto cleanup;
        }
        void *invalid=mmap(NULL,bytes,PROT_READ,MAP_SHARED,fd,(off_t)bytes);
        int rejection_errno=errno;
        if(invalid!=MAP_FAILED) munmap(invalid,bytes);
        REQUIRE(invalid==MAP_FAILED && rejection_errno==EINVAL,"reject out-of-range mapping with EINVAL");
        invalid=mmap(NULL,bytes,PROT_READ|PROT_EXEC,MAP_SHARED,fd,0);
        rejection_errno=errno;
        if(invalid!=MAP_FAILED) munmap(invalid,bytes);
        REQUIRE(invalid==MAP_FAILED && rejection_errno==EINVAL,"reject initially executable mapping with EINVAL");
        invalid=mmap(NULL,bytes,PROT_READ,MAP_PRIVATE,fd,0);
        rejection_errno=errno;
        if(invalid!=MAP_FAILED) munmap(invalid,bytes);
        REQUIRE(invalid==MAP_FAILED && rejection_errno==ENODEV,"reject private mapping with ENODEV");
        REQUIRE(stats(monitor,&s)==0 && dlmp_result_allocated(bytes,&baseline,&s),
                "rejected mappings publish no tickets or exports");
    }
    a=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    REQUIRE(a!=MAP_FAILED,"first shared mapping");
    b=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    REQUIRE(b!=MAP_FAILED,"second shared mapping");
    int all_zero=1;
    for(size_t j=0;j<bytes;j++) if(a[j]!=0 || b[j]!=0) all_zero=0;
    REQUIRE(all_zero,"zero all rounded bytes");
    for(size_t j=0;j<bytes;j++) a[j]=(unsigned char)(j*17+23);
    int coherent=1;
    for(size_t j=0;j<bytes;j++) if(b[j]!=(unsigned char)(j*17+23)) coherent=0;
    REQUIRE(coherent,"alias coherence");
    REQUIRE(close(fd)==0,"close allocating file while both mappings live");fd=-1;
    b[bytes-1]=91;REQUIRE(a[bytes-1]==91,"mapping survives file close");
    if(!control) {
        REQUIRE(collect(monitor,&c)==0 && stats(monitor,&s)==0,"observe parent-live mappings");
        bool parent_live=dlmp_result_live(capabilities,bytes,&baseline,&s,&c);
        if(!parent_live)
            diagnose("parent-live mismatch",&baseline,&s,&c,0,0);
        REQUIRE(parent_live,
                "two pending maps and no drains after owner close");
        checkpoints|=parent_live?DLMP_CHECK_PARENT_LIVE:0;
    }
    REQUIRE(pipe(child_to_parent)==0 && pipe(parent_to_child)==0,"create child synchronization pipes");
    child=fork();REQUIRE(child>=0,"fork mapped child");
    if(child==0) {
        alarm(15);
        close(child_to_parent[0]);close(parent_to_child[1]);
        if(monitor>=0) close(monitor);
        a[0]=0x5a;
        if(b[0]!=0x5a || byte_write(child_to_parent[1],'r')) _exit(11);
        char command=0;
        if(byte_read(parent_to_child[0],&command) || command!='x' || b[0]!=0x5a) _exit(12);
        // Deliberately no munmap: process exit must tear down inherited mappings.
        _exit(0);
    }
    close(child_to_parent[1]);child_to_parent[1]=-1;
    close(parent_to_child[0]);parent_to_child[0]=-1;
    char ready=0;REQUIRE(byte_read(child_to_parent[0],&ready)==0&&ready=='r',"child sees inherited mappings");
    REQUIRE(a[0]==0x5a && b[0]==0x5a,"cross-process shared write");
    REQUIRE(munmap(a,bytes)==0,"unmap parent first alias");a=MAP_FAILED;
    REQUIRE(munmap(b,bytes)==0,"unmap parent second alias");b=MAP_FAILED;
    if(!control) {
        REQUIRE(collect(monitor,&c)==0 && stats(monitor,&s)==0,"observe child-live mappings");
        bool child_live=dlmp_result_live(capabilities,bytes,&baseline,&s,&c);
        if(!child_live)
            diagnose("child-live mismatch",&baseline,&s,&c,0,0);
        REQUIRE(child_live,
                "two pending maps and no drains while child retains mappings");
        checkpoints|=child_live?DLMP_CHECK_CHILD_LIVE:0;
    }
    REQUIRE(byte_write(parent_to_child[1],'x')==0,"release child to exit");
    int child_status=0;pid_t waited;
    do{waited=waitpid(child,&child_status,0);}while(waited<0&&errno==EINTR);
    child=-1;
    bool child_exited=waited>0&&WIFEXITED(child_status)&&WEXITSTATUS(child_status)==0;
    REQUIRE(child_exited,"child exit cleanup");
    if(!control) {
        checkpoints|=child_exited?DLMP_CHECK_CHILD_EXITED:0;
        for(unsigned attempt=0;attempt<30;attempt++) {
            REQUIRE(collect(monitor,&c)==0 && stats(monitor,&s)==0,"collect and inspect after child exit");
            REQUIRE(c.drained<=2-total_drained && c.reclaimed<=1-total_reclaimed,
                    "bounded collection totals");
            total_drained+=c.drained;total_reclaimed+=c.reclaimed;
            enum dlmp_result decision=dlmp_result_mapped(capabilities,bytes,&baseline,&s,&c,
                                                       total_drained,total_reclaimed,checkpoints);
            if(decision==DLMP_RESULT_FAILED)
                diagnose("final lifecycle mismatch",&baseline,&s,&c,total_drained,total_reclaimed);
            REQUIRE(decision!=DLMP_RESULT_FAILED,"consistent final mapping lifecycle");
            if(decision==DLMP_RESULT_BLOCKED || decision==DLMP_RESULT_PASSED) {
                result=(int)decision;goto cleanup;
            }
            struct timespec delay={0,100000000};nanosleep(&delay,NULL);
        }
        diagnose("collection timeout",&baseline,&s,&c,total_drained,total_reclaimed);
        REQUIRE(0,"bounded collection timeout");
    }
    result=0;
cleanup:
    if(child>0) { kill(child,SIGKILL);while(waitpid(child,NULL,0)<0&&errno==EINTR){} }
    if(a!=MAP_FAILED) munmap(a,bytes);
    if(b!=MAP_FAILED) munmap(b,bytes);
    if(fd>=0) close(fd);
    if(monitor>=0) close(monitor);
    for(unsigned n=0;n<2;n++) {if(child_to_parent[n]>=0)close(child_to_parent[n]);if(parent_to_child[n]>=0)close(parent_to_child[n]);}
    const char *status=result==77?"BLOCKED_MAPPED_RECLAMATION":result?"FAILED":
                       control?"PASSED_POSIX_CONTROL":unmapped?"PASSED_KERNEL_UNMAPPED":"PASSED_KERNEL_MAPPED";
    printf("{\"status\":\"%s\",\"backend\":\"%s\",\"checks\":%u,\"xnu_claim\":%s,"
           "\"evidence\":\"%s\",\"host_verified\":false}\n",
           status,backend,checks,(!control&&result==0)?"true":"false",
           control?"host_posix_control":"guest_self_report");
    return result;
}
int main(int argc,char **argv) {
    const char *device=NULL;int control=0,optin=0,unmapped=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--posix-control"))control=1;
        else if(!strcmp(argv[i],"--allow-experimental"))optin=1;
        else if(!strcmp(argv[i],"--unmapped-only"))unmapped=1;
        else if(!strcmp(argv[i],"--device")&&i+1<argc)device=argv[++i];
        else {fprintf(stderr,"invalid argument\n");return 2;}
    }
    if((control&&(!device&&!optin&&!unmapped))||(!control&&device&&optin)) {
        signal(SIGPIPE,SIG_IGN);alarm(30);return run(device,control,unmapped);
    }
    fprintf(stderr,"Usage: %s --posix-control\n       %s --device PATH --allow-experimental [--unmapped-only]\n",argv[0],argv[0]);
    return 2;
}
