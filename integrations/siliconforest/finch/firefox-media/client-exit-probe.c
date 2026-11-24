/* SPDX-License-Identifier: MIT */
/* Real PD client exits without cleanup, never starts an IOProc/output device.
 * Finite capacity recycling is not an acoustic or exact per-client leak proof. */
#include <CoreAudio/CoreAudio.h>
#include "SFAudioClient.h"
#include <dlfcn.h>
#include <spawn.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern char **environ;
static double seconds(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;
    return t.tv_sec+t.tv_nsec/1e9;
}
static int child(void) {
    const struct sf_audio_session *(*connect)(AudioObjectID)=dlsym(RTLD_DEFAULT,"SFAudioSessionForDevice");
    if(!connect)return 77;
    AudioDeviceID device=0;UInt32 size=sizeof(device);
    AudioObjectPropertyAddress a={kAudioHardwarePropertyDefaultOutputDevice,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    if(AudioObjectGetPropertyData(kAudioObjectSystemObject,&a,0,NULL,&size,&device)||size!=sizeof(device)||!device)return 1;
    const struct sf_audio_session *s=connect(device);
    if(!s||!s->ring||s->ring->version!=2||s->ring->running)return 1;
    printf("AUDIO_EXIT_CHILD connected=1 running=0 rate=%u capacity=%u\n",s->ring->sampleRate,s->ring->frames);
    fflush(stdout);
    /* No Close/Stop/Dispose or atexit: the OS must discard this task's rights. */
    _exit(0);
}
int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"--child"))return child();
    if(argc!=1||argv[0][0]!='/')return 2;
    const unsigned count=80;
    double start=seconds();if(start<0)return 1;
    for(unsigned i=0;i<count;i++) {
        if(seconds()-start>20){fprintf(stderr,"AUDIO_EXIT_FAIL total deadline\n");return 1;}
        char *args[]={argv[0],"--child",NULL};pid_t pid=0;int status=0;
        int e=posix_spawn(&pid,argv[0],NULL,NULL,args,environ);
        if(e){fprintf(stderr,"AUDIO_EXIT_FAIL spawn round=%u status=%d\n",i,e);return 1;}
        double launched=seconds();pid_t reaped=0;
        while(!(reaped=waitpid(pid,&status,WNOHANG))) {
            if(seconds()-launched>4||seconds()-start>24) {
                /* Only the unreaped child this invocation spawned. */
                kill(pid,SIGKILL);waitpid(pid,&status,0);
                fprintf(stderr,"AUDIO_EXIT_FAIL child deadline round=%u\n",i);return 1;
            }
            usleep(1000);
        }
        if(reaped!=pid||!WIFEXITED(status)||WEXITSTATUS(status)) {
            fprintf(stderr,"AUDIO_EXIT_FAIL round=%u wait-status=%d\n",i,status);return 1;
        }
        usleep(5000);
    }
    printf("AUDIO_EXIT_DONE abrupt-clients=%u elapsed=%.6f output-started=0 cleanup-in-child=0\n",count,seconds()-start);
    return 0;
}
