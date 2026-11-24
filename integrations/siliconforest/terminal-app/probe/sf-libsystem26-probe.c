// SPDX-License-Identifier: MIT
// Runs only with a libSystem that has the names programs built against the macOS 26 SDK
// import (../../libsystem-default/patches/macos26-sdk-imports.patch): the installer's proof
// that a process gets the new library (../tools/sf_terminal.sh). Since the feature-flag reader
// (../../libsystem-default/patches/feature-flags.patch) it also needs os_feature_enabled to read
// the flag the tree installs (/System/Library/FeatureFlags/Domain/Terminal.plist: DirectColor on).
// Since memorystatus_get_level (../../libsystem-default/patches/memorystatus-level.patch, Activity
// Monitor's memory pressure) it imports that too, so the libSystem of before does not load it (the
// answer is printed, not judged: a kernel without system call 453 answers an error).
// Since the optimised build (../../libsystem-default/build-libsystem-opt.nix) it also requires that
// malloc, free, strlen, memmove, getenv and pthread_mutex_lock are optimised code: -O0 code begins by
// storing its first argument to the stack (str x0, [sp, #n] or stur x0, [x29, #-n]), as SiliconForest's
// Debug build of every libSystem component did. Status 3 when one of them does.
// Since patches/platform-memset-wide-stores.patch memset must be a leaf (it fills with its own stores;
// before, it set up a frame and called memset_pattern4, which called memmove for every 4 bytes):
// status 4 when memset's first instructions save the frame pointer and link register.
// Both checks only in the probe of the optimised tree (-DSF_REQUIRE_OPTIMISED, ../build-macos-apps-root.nix
// with SF_LIBSYSTEM=opt), so that the probe of SiliconForest's build still proves that build.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
static __attribute__((unused)) int unoptimised(const void *fn)
{
    const uint32_t *code = (const uint32_t *)fn;
    for (int i = 0; i < 8; i++)
        if ((code[i] & 0xFFC003FFu) == 0xF90003E0u || (code[i] & 0xFFE00FFFu) == 0xF80003A0u) return 1;
    return 0;
}
extern void *malloc_type_malloc(size_t size, unsigned long long type_id);
extern int os_variant_is_basesystem(const char *subsystem);
extern bool _os_feature_enabled_impl(const char *domain, const char *feature);
extern int memorystatus_get_level(uint64_t level);
int main(void)
{
    char *p = malloc_type_malloc(32, 0x1234);
    if (p == NULL) return 1;
    strcpy(p, "malloc_type_malloc");
    bool flag = _os_feature_enabled_impl("Terminal", "DirectColor");
    unsigned int level = 1000;
    int rc = memorystatus_get_level((uint64_t)(uintptr_t)&level);
    printf("sf-libsystem26-probe: %s answers, os_variant_is_basesystem=%d, feature Terminal/DirectColor=%d, memorystatus_get_level %d level %u\n",
           p, os_variant_is_basesystem("probe"), flag, rc, level);
    free(p);
#ifdef SF_REQUIRE_OPTIMISED
    const void *fns[] = { (const void *)&malloc, (const void *)&free, (const void *)&strlen, (const void *)&memmove,
                          (const void *)&getenv, (const void *)&pthread_mutex_lock };
    const char *names[] = { "malloc", "free", "strlen", "memmove", "getenv", "pthread_mutex_lock" };
    int slow = 0;
    for (int i = 0; i < 6; i++)
        if (unoptimised(fns[i])) { printf("sf-libsystem26-probe: %s is unoptimised (-O0) code\n", names[i]); slow = 1; }
    if (slow) return 3;
    const uint32_t *ms = (const uint32_t *)(const void *)&memset;
    for (int i = 0; i < 6; i++)
        if ((ms[i] & 0xFFC07FFFu) == 0xA9007BFDu || (ms[i] & 0xFFC07FFFu) == 0xA9807BFDu) {
            printf("sf-libsystem26-probe: memset calls out (it saves x29/x30): the fill of before\n");
            return 4;
        }
#endif
    if (!flag && access("/System/Library/FeatureFlags/Domain/Terminal.plist", R_OK) == 0) return 2;
    return 0;
}
