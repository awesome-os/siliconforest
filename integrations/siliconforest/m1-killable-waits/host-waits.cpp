// SPDX-License-Identifier: MIT
// Host harness: the ACTUAL LinuxKPI wait/signal bodies (linux_add_to_sleepqueue,
// linux_schedule_timeout, linux_signal_pending, linux_fatal_signal_pending from
// linux_schedule.c; sleepq_* from sf_lkpi_kern.c; sfos_wait_sleep and, when patched,
// sfos_signal_pending from sf_fbsd_os.cpp) over a fake XNU thread that models
// THREAD_ABORTSAFE: thread_abort (task termination) interrupts every abort-safe wait
// until the thread leaves the kernel; thread_abort_safely (a posted signal) interrupts
// one. The wait loops are the shapes of drm_syncobj_array_wait_timeout
// (drm_syncobj.c:1177-1217) and dma_fence_default_wait (dma-fence.c:378-391).
#include <cassert>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstring>

// ---- FreeBSD/LinuxKPI side ---------------------------------------------------------
#define EWOULDBLOCK 35
#define EINTR 4
#define ERESTARTSYS 512
#define TASK_RUNNING 0x0000
#define TASK_INTERRUPTIBLE 0x0001
#define TASK_UNINTERRUPTIBLE 0x0002
#define TASK_NORMAL (TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)
#define TASK_WAKING 0x0100
#define TASK_PARKED 0x0200
#define MAX_SCHEDULE_TIMEOUT 0x7fffffffL
#define SLEEPQ_SLEEP 0
#define SLEEPQ_INTERRUPTIBLE 0x100
#define SF_LINUXKPI 1
#define MPASS(x) assert(x)
#define DROP_GIANT()
#define PICKUP_GIANT()
#define hz 1000
struct atomic_t { int counter; };
static int atomic_read(atomic_t *a) { return a->counter; }
struct thread { int td_pd_wait_state; long long td_pd_timeout_us; };
struct task_struct { struct thread *task_thread; atomic_t state; int bsd_interrupt_value; };
struct lock_object;
static thread g_td; static task_struct g_task = { &g_td, {0}, 0 };
static thread other_td; static task_struct other_task = { &other_td, {0}, 0 };
#define curthread (&g_td)
#define current (&g_task)
#define set_task_state(t, x) ((t)->state.counter = (x))
static unsigned long jiffies;
static void linux_schedule_save_interrupt_value(task_struct *t, int v) { t->bsd_interrupt_value = v; }

// ---- fake XNU -----------------------------------------------------------------------
typedef int wait_interrupt_t; typedef int wait_result_t; typedef const void *event_t;
typedef struct proc *proc_t; typedef struct xthread *thread_t;
#define THREAD_UNINT 0
#define THREAD_ABORTSAFE 2
#define THREAD_AWAKENED 0
#define THREAD_TIMED_OUT 1
#define THREAD_INTERRUPTED 2
#define THREAD_CONTINUE_NULL nullptr
#define NSEC_PER_USEC 1000
#define FALSE 0
#define TRUE 1
typedef int boolean_t;
struct proc { int exiting; }; struct xthread { int x; };
static proc user_proc, kern_proc; static xthread self_thread;
static proc_t kernproc = &kern_proc;
static proc_t cur_proc = &user_proc;
static struct {
	bool abort_sticky;      // thread_abort: task_terminate_internal of an exiting task
	bool abort_safely;      // thread_abort_safely: a signal was posted to this thread
	unsigned siglist, sigmask;
	bool preempt_off;
	int wake_after;          // a "fence" signals the waiter after this many sleeps (0: never)
	int sleeps, issignal_calls, spins;
	wait_interrupt_t level; long long timeout_us; bool waiting;
} X;
static jmp_buf hang;
enum { HANG_NONE, HANG_FOREVER, HANG_SPIN };
static proc_t current_proc() { return cur_proc; }
static int proc_exiting(proc_t p) { return p->exiting; }
static thread_t current_thread() { return &self_thread; }
static int thread_issignal(proc_t, thread_t, unsigned mask) { X.issignal_calls++; assert(!X.preempt_off); return X.siglist & ~X.sigmask & mask; }
static boolean_t preemption_enabled() { return !X.preempt_off; }
static boolean_t ml_get_interrupts_enabled() { return !X.preempt_off; }
static boolean_t ml_at_interrupt_context() { return FALSE; }
static boolean_t ml_set_interrupts_enabled(boolean_t) { return TRUE; }
struct lck_spin_t { int held; }; static lck_spin_t wait_lock_obj; static lck_spin_t *sWaitLock = &wait_lock_obj;
static void lck_spin_lock(lck_spin_t *l) { assert(!l->held); l->held = 1; }
static void lck_spin_unlock(lck_spin_t *l) { assert(l->held); l->held = 0; }
static void sfos_xnu_init() {}
static void assert_wait(event_t, wait_interrupt_t level) { X.level = level; X.timeout_us = 0; X.waiting = true; }
static void assert_wait_timeout(event_t, wait_interrupt_t level, uint32_t us, int) { X.level = level; X.timeout_us = us; X.waiting = true; }
static wait_result_t thread_block(void *) {
	assert(X.waiting); X.waiting = false; X.sleeps++;
	if (X.sleeps > 20000) longjmp(hang, HANG_SPIN);
	if (X.level == THREAD_ABORTSAFE && X.abort_sticky) return THREAD_INTERRUPTED;
	if (X.level == THREAD_ABORTSAFE && X.abort_safely) { X.abort_safely = false; return THREAD_INTERRUPTED; }
	if (X.wake_after) return THREAD_AWAKENED;   // a waker (spurious until the fence signals)
	if (X.timeout_us) { jiffies += (unsigned long)(X.timeout_us / 1000) + 1; return THREAD_TIMED_OUT; }
	longjmp(hang, HANG_FOREVER);   // nothing will ever wake this sleep
}
#define SFOS_WAIT_OK 0
#define SFOS_WAIT_TIMEOUT 1
#define SFOS_WAIT_INTR 2
static int sfos_wait_lock() { lck_spin_lock(sWaitLock); return 1; }
static void sfos_wait_unlock(int) { lck_spin_unlock(sWaitLock); }
static int sfos_wait_sleep(const void *event, int64_t timeout_us, int interruptible, int *state);
static void sleepq_add(const void *, struct lock_object *, const char *, int, int);
static void sleepq_set_timeout(const void *, int);
static int sleepq_wait_sig(const void *, int); static int sleepq_timedwait_sig(const void *, int);
static void sleepq_wait(const void *, int); static int sleepq_timedwait(const void *, int);
static void sleepq_lock(const void *); static void sleepq_release(const void *);
static int linux_jiffies_timeout_to_ticks(long timeout) { if (timeout < 1) return 1; if (timeout == MAX_SCHEDULE_TIMEOUT) return 0; return (int)timeout; }
extern "C" int sfos_signal_pending(int fatal);
extern "C" int sfos_task_exiting(void);
#include SF_WAITS_SOURCE
#ifndef SF_PATCHED
extern "C" int sfos_signal_pending(int) { return 0; }
#endif

// ---- the Linux loops ----------------------------------------------------------------
static bool fence_signaled() { return X.wake_after && X.sleeps >= X.wake_after; }
// drm_syncobj_array_wait_timeout(): signal_pending() before each schedule_timeout()
static long syncobj_wait(long timeout) {
	do {
		set_task_state(current, TASK_INTERRUPTIBLE);
		if (fence_signaled()) break;
		if (timeout == 0) { timeout = -62; break; }
		if (linux_signal_pending(current)) { timeout = -ERESTARTSYS; break; }
		timeout = linux_schedule_timeout(timeout);
	} while (1);
	set_task_state(current, TASK_RUNNING);
	return timeout;
}
// dma_fence_default_wait(): signal_pending() after each schedule_timeout()
static long fence_default_wait(bool intr, long timeout) {
	long rv = timeout ? timeout : 1;
	while (!fence_signaled() && rv > 0) {
		set_task_state(current, intr ? TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE);
		rv = linux_schedule_timeout(rv);
		if (rv > 0 && intr && linux_signal_pending(current)) rv = -ERESTARTSYS;
	}
	set_task_state(current, TASK_RUNNING);
	return rv;
}

static void reset() { memset(&X, 0, sizeof X); user_proc.exiting = 0; cur_proc = &user_proc; g_task.state.counter = 0; jiffies = 1000; }
static int run(long (*f)(long), long arg, long *ret) {
	int h = setjmp(hang);
	if (h) return h;
	*ret = f(arg);
	return HANG_NONE;
}
static long syncobj_forever(long) { return syncobj_wait(MAX_SCHEDULE_TIMEOUT); }
static long fence_intr_forever(long) { return fence_default_wait(true, MAX_SCHEDULE_TIMEOUT); }
static long fence_unint_forever(long) { return fence_default_wait(false, MAX_SCHEDULE_TIMEOUT); }
static long syncobj_100(long) { return syncobj_wait(100); }

static int failures;
static void expect(const char *name, int hang_kind, long ret, int want_hang, long want_ret) {
	bool ok = hang_kind == want_hang && (want_hang != HANG_NONE || ret == want_ret);
	static const char *hn[] = { "returned", "SLEEPS-FOREVER", "SPINS" };
	printf("%-58s %s %s", name, ok ? "PASS" : "FAIL", hn[hang_kind]);
	if (hang_kind == HANG_NONE) printf(" %ld", ret);
	printf(" (sleeps %d, issignal %d)\n", X.sleeps, X.issignal_calls);
	if (!ok) failures++;
}

int main() {
#ifdef SF_PATCHED
	const bool fixed = true;
#else
	const bool fixed = false;
#endif
	long r = 0; int h;
	// 1. exit/SIGKILL: P_LEXIT set, task_terminate aborts the waiting thread (sticky)
	reset(); user_proc.exiting = 1; X.abort_sticky = true;
	h = run(syncobj_forever, 0, &r);
	expect("exit: syncobj wait UINT64_MAX (Indium/Firefox helper)", h, r, fixed ? HANG_NONE : HANG_SPIN, -ERESTARTSYS);
	reset(); user_proc.exiting = 1; X.abort_sticky = true;
	h = run(fence_intr_forever, 0, &r);
	expect("exit: dma_fence_wait(intr=true)", h, r, fixed ? HANG_NONE : HANG_SPIN, -ERESTARTSYS);
	// 2. a caught/default-terminate signal posted to the thread (abort safely, once)
	reset(); X.abort_safely = true; X.siglist = 1u << (15 - 1);
	h = run(syncobj_forever, 0, &r);
	expect("SIGTERM: syncobj wait UINT64_MAX", h, r, fixed ? HANG_NONE : HANG_FOREVER, -ERESTARTSYS);
	// 3. the signal is blocked by the thread's mask: Linux keeps waiting too
	reset(); X.abort_safely = true; X.siglist = X.sigmask = 1u << (15 - 1);
	h = run(syncobj_forever, 0, &r);
	expect("blocked SIGTERM: wait continues (as Linux)", h, r, HANG_FOREVER, 0);
	// 4. kernel thread: no signals, unchanged
	reset(); cur_proc = kernproc; X.siglist = ~0u;
	h = run(syncobj_forever, 0, &r);
	expect("kernel thread: wait continues", h, r, HANG_FOREVER, 0);
	// 5. preemption off (a real spin lock held): no proc mutex; exit still seen
	reset(); X.preempt_off = true; X.abort_safely = true; X.siglist = 1u << (15 - 1);
	h = run(syncobj_forever, 0, &r);
	expect("preemption off + SIGTERM: no proc lock taken", h, r, HANG_FOREVER, 0);
	reset(); X.preempt_off = true; user_proc.exiting = 1; X.abort_sticky = true;
	h = run(syncobj_forever, 0, &r);
	expect("preemption off + exit: still ends", h, r, fixed ? HANG_NONE : HANG_SPIN, -ERESTARTSYS);
	// 6. normal completion and timeout: unchanged
	reset(); X.wake_after = 3;
	h = run(syncobj_forever, 0, &r);
	expect("fence signalled after 3 sleeps: success", h, r, HANG_NONE, MAX_SCHEDULE_TIMEOUT);
	reset();
	h = run(syncobj_100, 0, &r);
	expect("finite timeout, nothing pending: -ETIME", h, r, HANG_NONE, -62);
	// 7. uninterruptible fence wait stays uninterruptible (Linux D state; no early free)
	reset(); user_proc.exiting = 1; X.abort_sticky = true;
	h = run(fence_unint_forever, 0, &r);
	expect("exit: dma_fence_wait(intr=false) stays uninterruptible", h, r, HANG_FOREVER, 0);
	// 8. another task's signal_pending is not answered for the caller
	reset(); user_proc.exiting = 1;
	bool other = linux_signal_pending(&other_task);
	printf("%-58s %s\n", "signal_pending(other task) is false", !other ? "PASS" : "FAIL"); if (other) failures++;
	// 9. fatal: SIGKILL posted (not yet exiting) vs SIGTERM
	reset(); X.siglist = 1u << (9 - 1);
	bool fk = linux_fatal_signal_pending(current);
	reset(); X.siglist = 1u << (15 - 1);
	bool ft = linux_fatal_signal_pending(current);
	bool fok = fixed ? (fk && !ft) : (!fk && !ft);
	printf("%-58s %s (SIGKILL %d, SIGTERM %d)\n", "fatal_signal_pending: SIGKILL yes, SIGTERM no", fok ? "PASS" : "FAIL", fk, ft);
	if (!fok) failures++;
	printf("%s: %d failure(s)\n", fixed ? "PATCHED" : "FROZEN", failures);
	return failures ? 1 : 0;
}
