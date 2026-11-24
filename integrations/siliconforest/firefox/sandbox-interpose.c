/* SPDX-License-Identifier: MIT
 * Use the SF-U profile compiler in this process only. The replacement still
 * calls the real Sandbox.kext syscall and propagates every compiler/kernel
 * failure. No sandbox permission or successful result is fabricated here.
 */
#include <stdint.h>
#include <sandbox.h>

extern int sffx_sandbox_init(const char *, unsigned long, char **);
extern int sffx_sandbox_init_with_parameters(const char *, uint64_t, const char *const [], char **);
extern void sffx_sandbox_free_error(char *);

struct replacement { const void *replacement, *original; };
__attribute__((used, section("__DATA,__interpose")))
static const struct replacement replacements[] = {
    {(const void *)sffx_sandbox_init, (const void *)sandbox_init},
    {(const void *)sffx_sandbox_init_with_parameters, (const void *)sandbox_init_with_parameters},
    {(const void *)sffx_sandbox_free_error, (const void *)sandbox_free_error},
};
