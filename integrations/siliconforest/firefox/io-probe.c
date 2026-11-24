/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
int main(int argc,char **argv){
    if((argc!=3&&argc!=4)||strncmp(argv[2],"/private/var/db/sffx/",21))return 2;
    int policy=!strcmp(argv[1],"throttle")?IOPOL_THROTTLE:!strcmp(argv[1],"utility")?IOPOL_UTILITY:IOPOL_DEFAULT;
    int expected=argc==4?atoi(argv[3]):policy;
    int fd=open(argv[2],O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0){perror("create owned fixture");return 2;}
    int old=getiopolicy_np(IOPOL_TYPE_DISK,IOPOL_SCOPE_THREAD);
    if(old<0||setiopolicy_np(IOPOL_TYPE_DISK,IOPOL_SCOPE_THREAD,policy)<0){perror("iopolicy");close(fd);return 2;}
    int actual=getiopolicy_np(IOPOL_TYPE_DISK,IOPOL_SCOPE_THREAD);
    if(actual!=expected){setiopolicy_np(IOPOL_TYPE_DISK,IOPOL_SCOPE_THREAD,old);close(fd);fprintf(stderr,"policy mismatch wanted=%d actual=%d\n",expected,actual);return 1;}
    char bytes[16384];memset(bytes,0x5a,sizeof(bytes));
    const size_t pattern[]={7,0,0,1048,0,16384,0,512};
    double elapsed[32],start=now();size_t total=0;int completed=0,failed=0,saved_errno=0;
    for(int i=0;i<32;i++){
        size_t size=pattern[i%8];double begin=now();ssize_t count=write(fd,bytes,size);elapsed[i]=now()-begin;
        if(count!=(ssize_t)size){failed=1;saved_errno=errno;break;}total+=size;completed++;
    }
    double wall=now()-start;
    int restored=setiopolicy_np(IOPOL_TYPE_DISK,IOPOL_SCOPE_THREAD,old);
    struct stat st;int statok=fstat(fd,&st)==0&&(size_t)st.st_size==total;
    close(fd);if(!failed&&statok)unlink(argv[2]);
    printf("FXIO policy=%d old=%d actual=%d restored=%d writes=%d bytes=%zu wall=%.6f failed=%d errno=%d size_matches=%d\n",policy,old,actual,restored,completed,total,wall,failed,saved_errno,statok);
    for(int i=0;i<32&&!failed;i++)printf("FXIO sample=%d bytes=%zu seconds=%.9f\n",i,pattern[i%8],elapsed[i]);
    return failed||restored<0||!statok?1:0;
}
