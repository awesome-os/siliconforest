// SPDX-License-Identifier: MIT
// sf-catch <program> [arguments...]: run a program and, when it takes a Mach exception (bad
// access, bad instruction, arithmetic, breakpoint), print where from outside the process:
// the exception, the registers, the frame-pointer chain with each address named by the image
// it lies in. Works where an in-process handler cannot: before the program's initializers have
// run, with a wrecked stack, in a process of another ABI (arm64e). The exception is then left
// to the kernel, so the program ends as it would have.
#include <errno.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/dyld_images.h>
#include <mach-o/loader.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#pragma pack(push, 4)
struct request {
    mach_msg_header_t head; mach_msg_body_t body; mach_msg_port_descriptor_t thread, task;
    NDR_record_t ndr; exception_type_t exception; mach_msg_type_number_t count; int64_t code[2];
    char trailer[64];
};
struct reply { mach_msg_header_t head; NDR_record_t ndr; kern_return_t result; };
#pragma pack(pop)

struct image { uint64_t base, end; char path[256]; };
static struct image images[512];
static int nimages;

static int rd(task_t task, uint64_t addr, void *buf, size_t len)
{
    mach_vm_size_t got = 0;
    return mach_vm_read_overwrite(task, addr, len, (mach_vm_address_t)buf, &got) == KERN_SUCCESS && got == len;
}

static void add_image(task_t task, uint64_t base, uint64_t pathaddr, const char *fallback)
{
    struct image *im = &images[nimages];
    struct mach_header_64 mh;
    if (nimages >= 512 || !rd(task, base, &mh, sizeof(mh)) || mh.magic != MH_MAGIC_64) return;
    im->base = base; im->end = base;
    snprintf(im->path, sizeof(im->path), "%s", fallback);
    if (pathaddr) { char p[256] = ""; for (int n = 255; n > 0 && !rd(task, pathaddr, p, n); n /= 2) { } p[255] = 0; if (p[0]) snprintf(im->path, sizeof(im->path), "%s", p); }
    uint64_t at = base + sizeof(mh), slide = 0; int haveslide = 0;
    for (uint32_t i = 0; i < mh.ncmds && i < 400; i++) {
        struct segment_command_64 sc;
        if (!rd(task, at, &sc, sizeof(struct load_command))) break;
        if (sc.cmd == LC_SEGMENT_64 && rd(task, at, &sc, sizeof(sc))) {
            if (!haveslide && strcmp(sc.segname, "__TEXT") == 0) { slide = base - sc.vmaddr; haveslide = 1; }
            if (strcmp(sc.segname, "__PAGEZERO") != 0 && sc.vmaddr + slide + sc.vmsize > im->end) im->end = sc.vmaddr + slide + sc.vmsize;
        }
        at += sc.cmdsize;
    }
    nimages++;
}

static void load_images(task_t task)
{
    struct task_dyld_info di; mach_msg_type_number_t n = TASK_DYLD_INFO_COUNT;
    struct dyld_all_image_infos all;
    nimages = 0;
    if (task_info(task, TASK_DYLD_INFO, (task_info_t)&di, &n) != KERN_SUCCESS || !rd(task, di.all_image_info_addr, &all, 48)) return;
    add_image(task, (uint64_t)all.dyldImageLoadAddress, 0, "dyld");
    for (uint32_t i = 0; i < all.infoArrayCount && i < 500; i++) {
        struct dyld_image_info info;
        if (!rd(task, (uint64_t)all.infoArray + i * sizeof(info), &info, sizeof(info))) break;
        add_image(task, (uint64_t)info.imageLoadAddress, (uint64_t)info.imageFilePath, "?");
    }
}

static void name(uint64_t addr, char *out, size_t len)
{
    uint64_t plain = addr & 0x0000007fffffffffULL;        // without pointer-authentication or tag bits
    for (int i = 0; i < nimages; i++)
        if (plain >= images[i].base && plain < images[i].end) {
            const char *leaf = strrchr(images[i].path, '/');
            snprintf(out, len, "%s+0x%llx%s", leaf ? leaf + 1 : images[i].path, (unsigned long long)(plain - images[i].base), plain != addr ? " (HIGH BITS SET)" : "");
            return;
        }
    snprintf(out, len, "?%s", plain != addr ? " (HIGH BITS SET)" : "");
}

static const char *excname(int e)
{
    switch (e) { case EXC_BAD_ACCESS: return "EXC_BAD_ACCESS"; case EXC_BAD_INSTRUCTION: return "EXC_BAD_INSTRUCTION";
    case EXC_ARITHMETIC: return "EXC_ARITHMETIC"; case EXC_BREAKPOINT: return "EXC_BREAKPOINT"; case EXC_SOFTWARE: return "EXC_SOFTWARE";
    case EXC_CRASH: return "EXC_CRASH"; case EXC_GUARD: return "EXC_GUARD"; default: return "exception"; }
}

static void report(struct request *rq)
{
    task_t task = rq->task.name; thread_t thread = rq->thread.name;
    arm_thread_state64_t ts; mach_msg_type_number_t n = ARM_THREAD_STATE64_COUNT;
    char nm[320];
    fprintf(stderr, "\nSFCATCH %s (%d) code 0x%llx subcode 0x%llx\n", excname(rq->exception), rq->exception, (unsigned long long)rq->code[0], (unsigned long long)rq->code[1]);
    load_images(task);
    if (thread_get_state(thread, ARM_THREAD_STATE64, (thread_state_t)&ts, &n) != KERN_SUCCESS) { fprintf(stderr, "SFCATCH no thread state\n"); return; }
    uint64_t *r = (uint64_t *)&ts;       // x0..x28, fp, lr, sp, pc, cpsr: the layout of both ABIs
    uint64_t fp = r[29], lr = r[30], sp = r[31], pc = r[32];
    for (int i = 0; i < 29; i += 4)
        fprintf(stderr, "SFCATCH x%-2d %016llx  x%-2d %016llx  x%-2d %016llx  x%-2d %016llx\n", i, (unsigned long long)r[i], i + 1, (unsigned long long)r[i + 1],
            i + 2, (unsigned long long)r[i + 2], i + 3, (unsigned long long)r[i + 3]);
    fprintf(stderr, "SFCATCH fp %016llx lr %016llx sp %016llx pc %016llx\n", (unsigned long long)fp, (unsigned long long)lr, (unsigned long long)sp, (unsigned long long)pc);
    uint32_t insn[4] = {0};
    if (rd(task, (pc & 0x7fffffffffULL) - 8, insn, sizeof(insn))) fprintf(stderr, "SFCATCH code at pc-8: %08x %08x [%08x] %08x\n", insn[0], insn[1], insn[2], insn[3]);
    name(pc, nm, sizeof(nm)); fprintf(stderr, "SFCATCH  #0 %016llx %s\n", (unsigned long long)pc, nm);
    name(lr, nm, sizeof(nm)); fprintf(stderr, "SFCATCH  lr %016llx %s\n", (unsigned long long)lr, nm);
    for (int depth = 1; depth < 48 && fp; depth++) {
        uint64_t frame[2];
        if (!rd(task, fp & 0x7fffffffffULL, frame, sizeof(frame))) break;
        name(frame[1], nm, sizeof(nm));
        fprintf(stderr, "SFCATCH #%-2d %016llx %s\n", depth, (unsigned long long)frame[1], nm);
        if (frame[0] <= fp) break;
        fp = frame[0];
    }
    // SFCATCH_DUMP="19 22": eight words of memory at the addresses in those registers, each named
    const char *dump = getenv("SFCATCH_DUMP");
    while (dump != NULL && *dump) {
        char *end; long reg = strtol(dump, &end, 10);
        if (end == dump) break;
        dump = end;
        if (reg < 0 || reg > 30) continue;
        uint64_t words[8];
        if (!rd(task, r[reg] & 0x7fffffffffULL, words, sizeof(words))) { fprintf(stderr, "SFCATCH [x%ld] unreadable\n", reg); continue; }
        for (int i = 0; i < 8; i++) { name(words[i], nm, sizeof(nm)); fprintf(stderr, "SFCATCH [x%ld+%d] %016llx %s\n", reg, i * 8, (unsigned long long)words[i], nm); }
    }
    if (getenv("SFCATCH_IMAGES") != NULL)       // the whole image list only when asked for
        for (int i = 0; i < nimages; i++) fprintf(stderr, "SFCATCH image %012llx-%012llx %s\n", (unsigned long long)images[i].base, (unsigned long long)images[i].end, images[i].path);
}

int main(int argc, char **argv)
{
    mach_port_t port;
    if (argc < 2) { fprintf(stderr, "usage: sf-catch <program> [arguments...]\n"); return 2; }
    if (mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port) != KERN_SUCCESS ||
        mach_port_insert_right(mach_task_self(), port, port, MACH_MSG_TYPE_MAKE_SEND) != KERN_SUCCESS) { fprintf(stderr, "sf-catch: no port\n"); return 2; }
    exception_mask_t mask = EXC_MASK_BAD_ACCESS | EXC_MASK_BAD_INSTRUCTION | EXC_MASK_ARITHMETIC | EXC_MASK_BREAKPOINT;
    if (task_set_exception_ports(mach_task_self(), mask, port, (exception_behavior_t)(EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES), ARM_THREAD_STATE64) != KERN_SUCCESS) {
        fprintf(stderr, "sf-catch: task_set_exception_ports failed\n"); return 2; }
    pid_t pid = fork();
    if (pid == 0) { execvp(argv[1], argv + 1); fprintf(stderr, "sf-catch: %s: %s\n", argv[1], strerror(errno)); _exit(127); }
    // the parent itself must not keep the handler for its own faults
    task_set_exception_ports(mach_task_self(), mask, MACH_PORT_NULL, EXCEPTION_DEFAULT, 0);
    for (;;) {
        struct request rq; int status;
        kern_return_t kr = mach_msg(&rq.head, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof(rq), port, 200, MACH_PORT_NULL);
        if (kr == KERN_SUCCESS) {
            report(&rq);
            struct reply rp; memset(&rp, 0, sizeof(rp));
            rp.head.msgh_bits = MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(rq.head.msgh_bits), 0);
            rp.head.msgh_size = sizeof(rp); rp.head.msgh_remote_port = rq.head.msgh_remote_port; rp.head.msgh_id = rq.head.msgh_id + 100;
            rp.ndr = rq.ndr; rp.result = KERN_FAILURE;        // not handled: the kernel goes on to the signal
            mach_msg(&rp.head, MACH_SEND_MSG, sizeof(rp), 0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
        }
        if (waitpid(pid, &status, WNOHANG) == pid) {
            if (WIFSIGNALED(status)) { fprintf(stderr, "SFCATCH ended by signal %d\n", WTERMSIG(status)); return 128 + WTERMSIG(status); }
            return WEXITSTATUS(status);
        }
    }
}
