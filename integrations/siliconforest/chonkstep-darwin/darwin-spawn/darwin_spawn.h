/* Private original managed-spawn ABI. Implementation/runtime pending. */
#ifndef CHONK_DARWIN_SPAWN_H
#define CHONK_DARWIN_SPAWN_H
#include <stddef.h>
#include <stdint.h>

#define CHONK_SPAWN_VERSION 1u
#define CHONK_SPAWN_MAX_ARGS 32u
#define CHONK_SPAWN_MAX_ENV 32u
#define CHONK_SPAWN_MAX_STRING 1024u
#define CHONK_SPAWN_MAX_TOTAL 32768u

/* All pointers/arrays are caller-owned, valid through the call, and strings
 * are bounded NUL-terminated. argv[argc] and envp[envc] are accessible NULLs.
 * argv[0] equals the absolute executable. No PATH/shell fallback.
 * source_fds maps to exactly child 0/1/2/3, the last a private status channel.
 * Caller prevents selected source close/reuse for the full call. */
struct chonk_spawn_request_v1 {
    uint32_t version;
    uint32_t argc;
    uint32_t envc;
    uint32_t reserved;
    const char *executable;
    const char *const *argv;
    const char *const *envp;
    int32_t source_fds[4];
};

/* Return 0 only for successful spawn AND all local cleanup. Error is a positive
 * errno value, with stage and independent cleanup errors retained here.
 * ALWAYS inspect child_created and pid even when the function returns error:
 * a spawned child's PID transfers exactly once despite local cleanup failure.
 * primary_error is setup/spawn, never overwritten by local cleanup.
 * requested_flags/observed_flags are facts about checked SDK attr calls only,
 * not a runtime exec whitelist proof. No retry or fallback without the flag. */
struct chonk_spawn_result_v1 {
    uint32_t version;
    uint32_t stage;
    int32_t pid;
    int32_t primary_error;
    int32_t spawn_error;
    int32_t attr_destroy_error;
    int32_t actions_destroy_error;
    int32_t fd_close_error;
    uint32_t fd_close_failures;
    uint32_t requested_flags;
    uint32_t observed_flags;
    uint32_t child_created;
    uint32_t duplicate_count;
    uint32_t reserved;
};

enum chonk_spawn_stage {
    CHONK_SPAWN_VALIDATE=1, CHONK_SPAWN_DUPLICATE=2,
    CHONK_SPAWN_ATTR_INIT=3, CHONK_SPAWN_ACTIONS_INIT=4,
    CHONK_SPAWN_SIGNALS=5, CHONK_SPAWN_GROUP=6,
    CHONK_SPAWN_FLAGS=7, CHONK_SPAWN_ACTIONS=8,
    CHONK_SPAWN_EXEC=9, CHONK_SPAWN_FINISHED=10
};

#ifdef __cplusplus
extern "C" {
#endif
int chonk_darwin_spawn_v1(const struct chonk_spawn_request_v1 *,
                          struct chonk_spawn_result_v1 *);
#ifdef __cplusplus
}
#define CHONK_SPAWN_ASSERT(c) static_assert((c), #c)
#else
#define CHONK_SPAWN_ASSERT(c) _Static_assert((c), #c)
#endif
CHONK_SPAWN_ASSERT(sizeof(void *)==8);
CHONK_SPAWN_ASSERT(sizeof(struct chonk_spawn_request_v1)==56);
CHONK_SPAWN_ASSERT(offsetof(struct chonk_spawn_request_v1,executable)==16);
CHONK_SPAWN_ASSERT(offsetof(struct chonk_spawn_request_v1,source_fds)==40);
CHONK_SPAWN_ASSERT(sizeof(struct chonk_spawn_result_v1)==56);
CHONK_SPAWN_ASSERT(offsetof(struct chonk_spawn_result_v1,pid)==8);
CHONK_SPAWN_ASSERT(offsetof(struct chonk_spawn_result_v1,child_created)==44);
#undef CHONK_SPAWN_ASSERT
#endif
