/* Explicit fake API for host policy tests, never a target implementation. */
#ifndef CHONK_SPAWN_TEST_FACADE_H
#define CHONK_SPAWN_TEST_FACADE_H
#ifndef CHONK_SPAWN_TEST_API
#error test facade requires explicit test build
#endif
#define POSIX_SPAWN_CLOEXEC_DEFAULT 0x4000
int csp_fake_fcntl(int,int,...);
int csp_fake_close(int);
int csp_fake_sigemptyset(sigset_t *);
int csp_fake_sigaddset(sigset_t *,int);
int csp_fake_attr_init(posix_spawnattr_t *);
int csp_fake_actions_init(posix_spawn_file_actions_t *);
int csp_fake_setdefault(posix_spawnattr_t *,const sigset_t *);
int csp_fake_setmask(posix_spawnattr_t *,const sigset_t *);
int csp_fake_setgroup(posix_spawnattr_t *,pid_t);
int csp_fake_setflags(posix_spawnattr_t *,short);
int csp_fake_getflags(const posix_spawnattr_t *,short *);
int csp_fake_adddup2(posix_spawn_file_actions_t *,int,int);
int csp_fake_spawn(pid_t *,const char *,const posix_spawn_file_actions_t *,
                   const posix_spawnattr_t *,char *const [],char *const []);
int csp_fake_attr_destroy(posix_spawnattr_t *);
int csp_fake_actions_destroy(posix_spawn_file_actions_t *);
#define fcntl csp_fake_fcntl
#define close csp_fake_close
#define sigemptyset csp_fake_sigemptyset
#define sigaddset csp_fake_sigaddset
#define posix_spawnattr_init csp_fake_attr_init
#define posix_spawn_file_actions_init csp_fake_actions_init
#define posix_spawnattr_setsigdefault csp_fake_setdefault
#define posix_spawnattr_setsigmask csp_fake_setmask
#define posix_spawnattr_setpgroup csp_fake_setgroup
#define posix_spawnattr_setflags csp_fake_setflags
#define posix_spawnattr_getflags csp_fake_getflags
#define posix_spawn_file_actions_adddup2 csp_fake_adddup2
#define posix_spawn csp_fake_spawn
#define posix_spawnattr_destroy csp_fake_attr_destroy
#define posix_spawn_file_actions_destroy csp_fake_actions_destroy
#endif
