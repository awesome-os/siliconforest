/* SPDX-License-Identifier: MIT */
/* Own declarations of VLC's public C API; no Apple SDK or VLC internal layouts. */
#include <unistd.h>
#include <pthread.h>
#include <sys/wait.h>
#include <dispatch/dispatch.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdlib.h>
typedef struct libvlc_instance_t libvlc_instance_t;
libvlc_instance_t *libvlc_new(int,const char *const *);
void libvlc_release(libvlc_instance_t *);
const char *libvlc_errmsg(void);
const char *libvlc_get_version(void);
const char *libvlc_get_changeset(void);
void libvlc_set_exit_handler(libvlc_instance_t *,void (*)(void *),void *);
void libvlc_set_app_id(libvlc_instance_t *,const char *,const char *,const char *);
void libvlc_set_user_agent(libvlc_instance_t *,const char *,const char *);
int libvlc_add_intf(libvlc_instance_t *,const char *);
void libvlc_playlist_play(libvlc_instance_t *,int,int,const char *const *);
static char *FromCFString(CFStringRef string,CFStringEncoding encoding){
    CFIndex size=CFStringGetMaximumSizeForEncoding(CFStringGetLength(string),encoding)+1;
    char *result=malloc((size_t)size);
    if(result&&!CFStringGetCString(string,result,size,encoding)){free(result);result=NULL;}
    return result;
}
