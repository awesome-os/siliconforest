// SPDX-License-Identifier: MIT
// libhbcrash.dylib: a diagnostic for programs that die in the guest. Inserted with
// DYLD_INSERT_LIBRARIES, it reports a fatal signal on stderr: the faulting address, the
// program counter, and a frame-pointer backtrace with the image and nearest symbol of each
// frame. The system has no crash reporter or debugger yet; this is the stand-in. It changes
// nothing else about the program.
#include <dlfcn.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ucontext.h>
#include <unistd.h>

static void frame(int n, uintptr_t pc)
{
    char line[512];
    Dl_info info;
    memset(&info, 0, sizeof info);
    if (pc && dladdr((void *)pc, &info) && info.dli_fname) {
        const char *base = strrchr(info.dli_fname, '/');
        snprintf(line, sizeof line, "hbcrash: #%d 0x%lx %s+0x%lx %s+0x%lx\n", n, (unsigned long)pc,
                 base ? base + 1 : info.dli_fname, (unsigned long)(pc - (uintptr_t)info.dli_fbase),
                 info.dli_sname ? info.dli_sname : "?", info.dli_saddr ? (unsigned long)(pc - (uintptr_t)info.dli_saddr) : 0ul);
    } else {
        snprintf(line, sizeof line, "hbcrash: #%d 0x%lx (no image)\n", n, (unsigned long)pc);
    }
    write(2, line, strlen(line));
}

static void handler(int sig, siginfo_t *si, void *ctx)
{
    ucontext_t *uc = ctx;
    char line[256];
    uintptr_t pc = (uintptr_t)__darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss);
    uintptr_t lr = (uintptr_t)__darwin_arm_thread_state64_get_lr(uc->uc_mcontext->__ss);
    uintptr_t fp = (uintptr_t)__darwin_arm_thread_state64_get_fp(uc->uc_mcontext->__ss);
    snprintf(line, sizeof line, "hbcrash: signal %d code %d address 0x%lx pc 0x%lx lr 0x%lx x0 0x%llx x1 0x%llx\n", sig, si->si_code,
             (unsigned long)si->si_addr, (unsigned long)pc, (unsigned long)lr,
             (unsigned long long)uc->uc_mcontext->__ss.__x[0], (unsigned long long)uc->uc_mcontext->__ss.__x[1]);
    write(2, line, strlen(line));
    frame(0, pc);
    frame(1, lr);
    for (int i = 2; i < 24 && fp && !(fp & 7); i++) {
        uintptr_t *f = (uintptr_t *)fp;
        uintptr_t next = f[0], ret = f[1];
        if (!ret)
            break;
        frame(i, ret);
        if (next <= fp)
            break;
        fp = next;
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

__attribute__((constructor)) static void install(void)
{
    static char stack[65536];
    stack_t ss = { .ss_sp = stack, .ss_size = sizeof stack, .ss_flags = 0 };
    struct sigaction sa;
    sigaltstack(&ss, 0);
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigaction(SIGSEGV, &sa, 0);
    sigaction(SIGBUS, &sa, 0);
    sigaction(SIGILL, &sa, 0);
    sigaction(SIGTRAP, &sa, 0);
    sigaction(SIGFPE, &sa, 0);
}
