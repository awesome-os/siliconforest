/* Original pinned-SDK managed spawn implementation. Not runtime-qualified. */
#include "darwin_spawn.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef CHONK_SPAWN_TEST_API
#include "spawn_test_api.h"
#endif

#if !defined(POSIX_SPAWN_CLOEXEC_DEFAULT) || POSIX_SPAWN_CLOEXEC_DEFAULT != 0x4000
#error pinned Darwin CLOEXEC_DEFAULT required; no fallback
#endif
_Static_assert(sizeof(pid_t)==sizeof(int32_t), "pid ABI");

static size_t bounded_length(const char *s)
{
    size_t n;
    if (!s) return CHONK_SPAWN_MAX_STRING;
    for (n=0;n<CHONK_SPAWN_MAX_STRING;n++) if (!s[n]) return n;
    return CHONK_SPAWN_MAX_STRING;
}
static int validate(const struct chonk_spawn_request_v1 *q)
{
    uint32_t i;
    size_t total, n;
    if (!q || q->version!=CHONK_SPAWN_VERSION || q->reserved ||
        !q->argc || q->argc>CHONK_SPAWN_MAX_ARGS || q->envc>CHONK_SPAWN_MAX_ENV ||
        !q->executable || !q->argv || !q->envp) return EINVAL;
    n=bounded_length(q->executable);
    if (!n || n==CHONK_SPAWN_MAX_STRING || q->executable[0]!='/') return EINVAL;
    total=n+1;
    if (q->argv[q->argc] || q->envp[q->envc]) return EINVAL;
    for (i=0;i<q->argc;i++) {
        n=bounded_length(q->argv[i]);
        if (n==CHONK_SPAWN_MAX_STRING) return E2BIG;
        total+=n+1;
    }
    for (i=0;i<q->envc;i++) {
        n=bounded_length(q->envp[i]);
        if (n==CHONK_SPAWN_MAX_STRING) return E2BIG;
        total+=n+1;
    }
    if (total>CHONK_SPAWN_MAX_TOTAL) return E2BIG;
    if (strcmp(q->argv[0],q->executable)) return EINVAL;
    for (i=0;i<4;i++) if (q->source_fds[i]<0) return EBADF;
    return 0;
}
static int system_error(void) { return errno ? errno : EIO; }

int chonk_darwin_spawn_v1(const struct chonk_spawn_request_v1 *q,
                         struct chonk_spawn_result_v1 *r)
{
    posix_spawnattr_t attributes;
    posix_spawn_file_actions_t actions;
    sigset_t defaults, mask;
    char *argv[CHONK_SPAWN_MAX_ARGS+1], *envp[CHONK_SPAWN_MAX_ENV+1];
    int duplicates[4]={-1,-1,-1,-1};
    int attr_ready=0, actions_ready=0, error=0, cleanup_error=0, i;
    short observed=0;
    const short flags=POSIX_SPAWN_CLOEXEC_DEFAULT|POSIX_SPAWN_SETSIGDEF|
                      POSIX_SPAWN_SETSIGMASK|POSIX_SPAWN_SETPGROUP;
    const int signals[]={SIGTERM,SIGINT,SIGHUP,SIGPIPE,SIGCHLD};
    pid_t pid=0;
    if (!r) return EINVAL;
    memset(r,0,sizeof(*r));
    r->version=CHONK_SPAWN_VERSION; r->stage=CHONK_SPAWN_VALIDATE;
    error=validate(q);
    if (error) { r->primary_error=error; return error; }
    for (i=0;i<(int)q->argc;i++) argv[i]=(char *)q->argv[i];
    argv[q->argc]=NULL;
    for (i=0;i<(int)q->envc;i++) envp[i]=(char *)q->envp[i];
    envp[q->envc]=NULL;
    r->stage=CHONK_SPAWN_DUPLICATE;
    for (i=0;i<4;i++) {
        duplicates[i]=fcntl(q->source_fds[i],F_DUPFD_CLOEXEC,4);
        if (duplicates[i]<0) { error=system_error(); goto cleanup; }
        r->duplicate_count++;
        if (duplicates[i]<4) { error=EIO; goto cleanup; }
    }
    r->stage=CHONK_SPAWN_ATTR_INIT;
    error=posix_spawnattr_init(&attributes);
    if (error) goto cleanup;
    attr_ready=1;
    r->stage=CHONK_SPAWN_ACTIONS_INIT;
    error=posix_spawn_file_actions_init(&actions);
    if (error) goto cleanup;
    actions_ready=1;
    r->stage=CHONK_SPAWN_SIGNALS;
    if (sigemptyset(&defaults)<0 || sigemptyset(&mask)<0) {
        error=system_error(); goto cleanup;
    }
    for (i=0;i<(int)(sizeof(signals)/sizeof(signals[0]));i++) {
        if (sigaddset(&defaults,signals[i])<0) { error=system_error(); goto cleanup; }
    }
    error=posix_spawnattr_setsigdefault(&attributes,&defaults);
    if (error) goto cleanup;
    error=posix_spawnattr_setsigmask(&attributes,&mask);
    if (error) goto cleanup;
    r->stage=CHONK_SPAWN_GROUP;
    error=posix_spawnattr_setpgroup(&attributes,0);
    if (error) goto cleanup;
    r->stage=CHONK_SPAWN_FLAGS;
    r->requested_flags=(uint16_t)flags;
    /* Exactly one assigning setflags call. Never overwrite its close policy. */
    error=posix_spawnattr_setflags(&attributes,flags);
    if (error) goto cleanup;
    error=posix_spawnattr_getflags(&attributes,&observed);
    if (error) goto cleanup;
    r->observed_flags=(uint16_t)observed;
    if (observed!=flags) { error=EINVAL; goto cleanup; }
    r->stage=CHONK_SPAWN_ACTIONS;
    for (i=0;i<4;i++) {
        error=posix_spawn_file_actions_adddup2(&actions,duplicates[i],i);
        if (error) goto cleanup;
    }
    r->stage=CHONK_SPAWN_EXEC;
    error=posix_spawn(&pid,q->executable,&actions,&attributes,argv,envp);
    r->spawn_error=error;
    if (!error) {
        if (pid<=0) { error=EIO; }
        else { r->pid=(int32_t)pid; r->child_created=1; }
    }
cleanup:
    r->primary_error=error;
    if (actions_ready) {
        r->actions_destroy_error=posix_spawn_file_actions_destroy(&actions);
        if (r->actions_destroy_error && !cleanup_error) cleanup_error=r->actions_destroy_error;
    }
    if (attr_ready) {
        r->attr_destroy_error=posix_spawnattr_destroy(&attributes);
        if (r->attr_destroy_error && !cleanup_error) cleanup_error=r->attr_destroy_error;
    }
    for (i=3;i>=0;i--) if (duplicates[i]>=0) {
        /* A close failure may be ambiguous. Record it; never retry/reuse. */
        if (close(duplicates[i])<0) {
            int e=system_error();
            r->fd_close_failures++;
            if (!r->fd_close_error) r->fd_close_error=e;
            if (!cleanup_error) cleanup_error=e;
        }
    }
    if (!error && !cleanup_error) r->stage=CHONK_SPAWN_FINISHED;
    return error ? error : cleanup_error;
}
