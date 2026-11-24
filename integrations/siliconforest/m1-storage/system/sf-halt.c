// SPDX-License-Identifier: MIT
/*
 * sf-halt: stop the system and restart (or power off) the machine. This is what the
 * commands reboot, halt, poweroff and shutdown run, and what the desktop's session end runs.
 *
 *   sf-halt restart|poweroff [-m <line>] [-r] [-k] [-n]
 *     -m <line>   printed on the console on a line of its own once the storage controller has
 *                 stopped, just before the machine is reset (the bench's marker; also taken
 *                 from the environment as SF_HALT_MARKER, so that the bench can run the plain
 *                 command `reboot`)
 *     -r          report (SF_HALT_REPORT=1): the storage driver's counters around each step,
 *                 in the lines the bench's verdict reads (NV_WSTATS, NV_DOWNGRADE, NV_SEAL,
 *                 NV_BEFORE_STOP, NV_AFTER_STOP ...), and two writes tried after the downgrade
 *     -k          do not signal the other processes (they were stopped by the caller)
 *     -n          say what would be done, do nothing
 * A power-off is refused unless the boot arguments allow it (sfpoweroff=1): without that the
 * kernel would stop without switching the machine off.
 *
 * Order (each step is reported on the console as SF_HALT ...):
 *   1. detach from the caller's terminal and session (it is about to be killed), output to
 *      /dev/console;
 *   2. SIGTERM to every process, up to 5 s for them to go, SIGKILL to the rest (all but
 *      launchd and the console's own shell or login, see others());
 *   3. sync;
 *   4. the root filesystem downgraded to read-only (`mount -t ext4 -o update,rdonly <node> /`:
 *      with m1-storage/ext4-downgrade.patch that pushes all data, empties the journal, clears
 *      the needs-recovery flag and freezes the filesystem: it is then clean on the device
 *      although it stays mounted);
 *   5. where the root is on the write-fenced internal disk (SFAppleNVMeRW): kern.sf_nvme.seal=1
 *      (Flush, the write fence closed for good), kern.sf_nvme.stop=1 (queues deleted, the
 *      controller's normal shutdown notification, the coprocessor stopped) and a wait for
 *      "stopped";
 *   6. reboot(2) with RB_NOSYNC|RB_QUICK: the kernel's restart hook (SFAppleReboot.cpp) asks
 *      the SMC to restart or power off the machine, as Linux does.
 * After step 5 nothing can be read from the root, so everything this process needs after it
 * is run once before it (the rehearsal) to have its pages in memory. If the downgrade fails
 * the machine is still stopped (the journal is then replayed at the next mount, which is
 * safe); if the seal or the stop fails, nothing is reset: a reset with the controller
 * running is the one thing this tool exists to avoid, and the message says what to do.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static int controller_stopped;
static void console(void);
static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
	char line[400];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(line, sizeof(line) - 1, fmt, ap);
	va_end(ap);
	if (n < 0) return;
	if (n > (int)sizeof(line) - 2) n = sizeof(line) - 2;
	line[n++] = '\n';
	if (write(1, line, (size_t)n) < 0 && !controller_stopped) {
		/* the console was revoked under us (see console()); not after the stop: a path lookup may need the disk */
		console();
		(void)write(1, line, (size_t)n);
	}
}

/*
 * Output goes to /dev/console. When the session leader whose controlling terminal the
 * console is dies (the single-user shell, a login shell), the kernel revokes that terminal
 * and every descriptor open on it goes dead, ours too: the console is opened anew after
 * each round of signals.
 */
static void console(void)
{
	int fd = open("/dev/console", O_WRONLY | O_NOCTTY);

	if (fd < 0)
		return;
	dup2(fd, 1); dup2(fd, 2);
	if (fd > 2) close(fd);
}

static int get_text(const char *name, char *text, size_t size)
{
	size_t len = size - 1;

	text[0] = 0;
	if (sysctlbyname(name, text, &len, NULL, 0) != 0)
		return -1;
	text[len < size ? len : size - 1] = 0;
	return 0;
}

static int report_on;

/* the bench's counters line, twice (a kernel line can cut one on the console) */
static void wstats(const char *tag)
{
	char text[1300];
	int v = -1; size_t l = sizeof(v);

	if (!report_on)
		return;
	get_text("kern.sf_nvme.wstats", text, sizeof(text));
	say("NV_WSTATS %s %s", tag, text);
	say("NV_WSTATS %s %s", tag, text);
	sysctlbyname("kern.sf_nvme.gate_rejected", &v, &l, NULL, 0);
	say("NV_GATE_REJECTED %d", v);
}

/* sfnvme-test stopreport's report, same lines */
static void report(const char *when)
{
	static const char *const names[] = { "kern.sf_nvme.state", "kern.sf_nvme.stats", "kern.sf_nvme.fence", "kern.sf_nvme.part",
	    "kern.sf_nvme.media", "kern.sf_nvme.wstats", "kern.sf_nvme.wgate_selftest" };
	char text[1300];
	int v = -1; size_t l = sizeof(v);

	if (!report_on)
		return;
	for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		if (get_text(names[i], text, sizeof(text)) != 0)
			snprintf(text, sizeof(text), "(%s)", strerror(errno));
		say("NV_%s %s %s", when, names[i] + 13, text);
	}
	sysctlbyname("kern.sf_nvme.gate_rejected", &v, &l, NULL, 0);
	say("NV_GATE_REJECTED %d", v);
	v = -1; l = sizeof(v);
	sysctlbyname("kern.sf_nvme.write_refused", &v, &l, NULL, 0);
	say("NV_WRITE_REFUSED %d", v);
}

static int set_one(const char *name)
{
	int one = 1;

	return sysctlbyname(name, NULL, NULL, &one, sizeof(one));
}

/*
 * The other processes: every one but the kernel's, launchd and this one. The session leader
 * whose controlling terminal is the console (the single-user shell, a console login), with
 * its ancestors up to launchd, is not signalled and not counted: when it dies the kernel
 * revokes the console, the report of the rest
 * of the halt would be lost, and launchd starts the next thing on the console in the middle
 * of the halt. It ignores SIGTERM, writes nothing, and goes with the machine.
 * sig 0: count only. names: when not NULL, "pid(command) ..." of the processes counted.
 */
static int others_named(int sig, char *names, size_t size)
{
	int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
	static struct kinfo_proc list[2048];
	size_t len = sizeof(list);
	struct stat con;
	pid_t spared[16];
	int n = 0, nspared = 0, have_con = stat("/dev/console", &con) == 0;
	size_t count;

	if (sysctl(mib, 3, list, &len, NULL, 0) != 0)
		return -1;
	count = len / sizeof(list[0]);
	/* the console's session leader, and what stands between it and launchd */
	for (size_t i = 0; have_con && i < count; i++) {
		pid_t pid = list[i].kp_proc.p_pid;
		if (pid > 1 && (list[i].kp_eproc.e_flag & EPROC_SLEADER) && list[i].kp_eproc.e_tdev == con.st_rdev) {
			for (int depth = 0; pid > 1 && depth < 8 && nspared < 16; depth++) {
				pid_t parent = 0;
				spared[nspared++] = pid;
				for (size_t j = 0; j < count; j++)
					if (list[j].kp_proc.p_pid == pid)
						parent = list[j].kp_eproc.e_ppid;
				pid = parent;
			}
		}
	}
	for (size_t i = 0; i < count; i++) {
		pid_t pid = list[i].kp_proc.p_pid;
		int keep = pid <= 1 || pid == getpid();
		for (int k = 0; k < nspared; k++)
			keep |= spared[k] == pid;
		if (keep)
			continue;
		if (sig != 0)
			kill(pid, sig);
		if (names != NULL && size > 0) {
			size_t used = strlen(names);
			if (used + 1 < size)
				snprintf(names + used, size - used, "%s%d(%.16s)", used ? " " : "", (int)pid, list[i].kp_proc.p_comm);
		}
		n++;
	}
	return n;
}

static int others(int sig)
{
	return others_named(sig, NULL, 0);
}

/* The block device node of the root filesystem: "/dev/diskN[sM]" whose number is "/"'s. */
static int root_node(char *path, size_t size)
{
	struct stat root, st;
	struct dirent *e;
	DIR *d;
	int found = -1;

	if (stat("/", &root) != 0 || (d = opendir("/dev")) == NULL)
		return -1;
	while (found != 0 && (e = readdir(d)) != NULL) {
		if (strncmp(e->d_name, "disk", 4))
			continue;
		snprintf(path, size, "/dev/%s", e->d_name);
		if (stat(path, &st) == 0 && S_ISBLK(st.st_mode) && st.st_rdev == root.st_dev)
			found = 0;
	}
	closedir(d);
	return found;
}

static int run(char *const argv[])
{
	pid_t pid;
	int status = 0;

	if (posix_spawn(&pid, argv[0], NULL, NULL, argv, environ) != 0)
		return -1;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;
	return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}

static int wait_stopped(char *state, size_t size, int tenths)
{
	for (int i = 0; ; i++) {
		get_text("kern.sf_nvme.state", state, size);
		if (!strncmp(state, "stopped", 7))
			return 0;
		if (!strncmp(state, "failed", 6) || i >= tenths)
			return -1;
		usleep(100000);
	}
}

int main(int argc, char **argv)
{
	const char *marker = getenv("SF_HALT_MARKER");
	int poweroff, dry = 0, keep = 0, fenced, rw_ext4, rc, n, howto;
	char node[64] = "", state[240], text[1300], fdev[40] = "";
	struct statfs fs;
	volatile unsigned char sink = 0;

	if (argc < 2 || (strcmp(argv[1], "restart") && strcmp(argv[1], "poweroff"))) {
		fprintf(stderr, "usage: sf-halt restart|poweroff [-m <line>] [-k] [-n]\n");
		return 64;
	}
	poweroff = !strcmp(argv[1], "poweroff");
	for (int i = 2; i < argc; i++) {
		if (!strcmp(argv[i], "-n")) dry = 1;
		else if (!strcmp(argv[i], "-r")) report_on = 1;
		else if (!strcmp(argv[i], "-k")) keep = 1;
		else if (!strcmp(argv[i], "-m") && i + 1 < argc) marker = argv[++i];
		else { fprintf(stderr, "sf-halt: unknown argument %s\n", argv[i]); return 64; }
	}
	if (getenv("SF_HALT_REPORT") != NULL && !strcmp(getenv("SF_HALT_REPORT"), "1"))
		report_on = 1;
	if (poweroff && !dry) {
		get_text("kern.bootargs", text, sizeof(text));
		if (strstr(text, "sfpoweroff=1") == NULL) {
			fprintf(stderr, "sf-halt: power-off is not enabled in this boot (sfpoweroff=1 is not among the boot arguments); "
			    "nothing was stopped. `reboot` restarts the machine.\n");
			return 78;
		}
	}
	if (geteuid() != 0) {
		fprintf(stderr, "sf-halt: must be root\n");
		return 77;
	}

	/* what there is */
	fenced = get_text("kern.sf_nvme.wstats", text, sizeof(text)) == 0 &&
	    (strstr(text, "fence=open") != NULL || strstr(text, "fence=sealed") != NULL);
	if (fenced) {
		const char *p = strstr(text, " device=");
		if (p == NULL || sscanf(p, " device=%31s", fdev) != 1)
			fenced = 0;
	}
	rw_ext4 = statfs("/", &fs) == 0 && !strcmp(fs.f_fstypename, "ext4") && !(fs.f_flags & MNT_RDONLY);
	if (root_node(node, sizeof(node)) != 0)
		node[0] = 0;
	if (fenced && strcmp(node + 5, fdev)) {
		/* the fenced disk is in use but is not the root: its filesystem is someone else's to unmount */
		say("SF_HALT the write-fenced device %s is not the root (%s); it is sealed and stopped all the same", fdev, node);
	}
	get_text("kern.sf_nvme.state", state, sizeof(state));
	if (dry) {
		printf("SF_HALT plan: %s; root %s on %s (%s); write-fenced disk %s%s; controller %s\n", poweroff ? "power off" : "restart",
		    fs.f_fstypename, node[0] ? node : "?", rw_ext4 ? "read-write: downgrade" : "nothing to downgrade",
		    fenced ? "open: seal and stop " : "not in use", fenced ? fdev : "", state[0] ? state : "(no driver)");
		return 0;
	}

	/* 1: away from the caller */
	fflush(NULL);
	pid_t child = fork();
	if (child < 0) { perror("sf-halt: fork"); return 71; }
	if (child > 0) {
		/* the caller's shell gets its prompt back only if the halt fails */
		int status;
		waitpid(child, &status, 0);
		return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
	}
	setsid();
	for (int s = 1; s < NSIG; s++)
		if (s != SIGKILL && s != SIGSTOP && s != SIGCHLD && s != SIGSEGV && s != SIGBUS && s != SIGILL)
			signal(s, SIG_IGN);
	int fd;
	console();
	fd = open("/dev/null", O_RDONLY);
	if (fd >= 0) { dup2(fd, 0); if (fd > 2) close(fd); }
	(void)chdir("/");
	say("SF_HALT begin: %s; root %s on %s%s; write-fenced disk %s", poweroff ? "power off" : "restart", fs.f_fstypename,
	    node[0] ? node : "?", rw_ext4 ? " read-write" : "", fenced ? fdev : "not in use");

	/* the rehearsal: every call made after the controller has stopped, once, now */
	get_text("kern.sf_nvme.state", state, sizeof(state));
	get_text("kern.sf_nvme.wstats", text, sizeof(text));
	usleep(1000);
	say("SF_HALT rehearsal: controller %s", state[0] ? state : "(no driver)");
	report("REHEARSAL");
	{
		const unsigned char *p[] = { (const unsigned char *)(void *)&reboot, (const unsigned char *)(void *)&usleep,
		    (const unsigned char *)(void *)&sysctlbyname, (const unsigned char *)(void *)&write,
		    (const unsigned char *)(void *)&strerror, (const unsigned char *)(void *)&vsnprintf,
		    (const unsigned char *)(void *)&pause, (const unsigned char *)(void *)&strncmp };
		for (unsigned i = 0; i < sizeof(p) / sizeof(p[0]); i++)
			sink ^= p[i][0];
		(void)strerror(EBUSY); (void)strerror(EPERM); (void)strerror(EIO);
	}

	/* 2: the other processes */
	if (!keep) {
		rc = others(SIGTERM);
		say("SF_HALT processes: SIGTERM to %d", rc);
		for (n = 0; n < 50 && others(0) > 0; n++)
			usleep(100000);
		text[0] = 0;
		rc = others_named(0, text, 300);
		say("SF_HALT processes: %d left after %d ms%s%s", rc, n * 100, rc ? ": " : "", text);
		if (rc != 0) {
			others(SIGKILL);
			for (n = 0; n < 20 && others(0) > 0; n++)
				usleep(100000);
			text[0] = 0;
			rc = others_named(0, text, 300);
			say("SF_HALT processes: SIGKILL sent, %d left%s%s", rc, rc ? ": " : "", text);
		}
	}

	/* 3 */
	sync(); usleep(500000); sync();
	console();

	/* 4: the root clean on the device */
	wstats("before-downgrade");
	if (rw_ext4 && node[0]) {
		char *const mount_argv[] = { "/sbin/mount", "-t", "ext4", "-o", "update,rdonly", node, "/", NULL };
		rc = run(mount_argv);
		n = statfs("/", &fs) == 0 && (fs.f_flags & MNT_RDONLY);
		say("SF_HALT downgrade: mount rc=%d, root is now %s", rc, n ? "read-only" : "STILL READ-WRITE");
		if (rc != 0 || !n)
			say("SF_HALT the root could not be made clean; the journal is replayed at the next mount");
		if (report_on) {
			char *const dmesg_argv[] = { "/bin/sh", "-c", "/bin/dmesg 2>/dev/null | /bin/grep 'ext4: downgraded' | /bin/tail -1 | /bin/sed 's/^/NV_DMESG /'", NULL };
			say("NV_DOWNGRADE rc=%d ", rc);
			say("NV_MOUNT_DOWN %s on / (%s, %s)", node, fs.f_fstypename, n ? "read-only" : "read-write");
			fd = open("/usr/sf-halt-probe", O_WRONLY | O_CREAT | O_EXCL, 0600);
			say("NV_FSWRITE create rc=%d %s", fd < 0 ? 1 : 0, fd < 0 ? strerror(errno) : "CREATED");
			if (fd >= 0) { close(fd); unlink("/usr/sf-halt-probe"); }
			fd = open("/private/var/run/sf-boot.done", O_WRONLY | O_APPEND);
			rc = fd < 0 ? -1 : (int)write(fd, "x", 1);
			say("NV_FSWRITE append rc=%d %s", rc == 1 ? 0 : 1, rc == 1 ? "WRITTEN" : strerror(errno));
			if (fd >= 0) close(fd);
			(void)run(dmesg_argv);
		}
	} else {
		say("SF_HALT downgrade: not needed (%s%s)", fs.f_fstypename, rw_ext4 ? ", no device node found" : ", not read-write");
	}

	/* 5: the write fence closed, the controller stopped */
	if (fenced) {
		wstats("before-seal");
		rc = set_one("kern.sf_nvme.seal");
		say("SF_HALT seal rc=%d %s", rc, rc ? strerror(errno) : "");
		if (report_on) {
			int v = -1; size_t l = sizeof(v);
			sysctlbyname("kern.sf_nvme.seal", &v, &l, NULL, 0);
			say("NV_SEAL rc=%d seal=%d", rc, v);
			sync(); usleep(1500000);
			wstats("sealed");
			say("NV_MOUNT_END %s on / (%s, still mounted)", node, fs.f_fstypename);
			wstats("after");
		}
		get_text("kern.sf_nvme.wstats", text, sizeof(text));
		if (strstr(text, "fence=sealed") == NULL) {
			say("SF_HALT FAILED: the write fence could not be sealed; NOT resetting the machine. The controller stops by itself at its time to live, or reset from outside");
			for (;;) pause();
		}
		say("SF_HALT wstats %s", text);
		console();
		report("BEFORE_STOP");
		controller_stopped = 1;
		rc = set_one("kern.sf_nvme.stop");
		n = rc ? errno : 0;
		if (report_on) say("NV_STOPCALL rc=%d %s", rc, rc ? strerror(n) : "");
		rc = wait_stopped(state, sizeof(state), 400);
		say("SF_HALT stop errno=%d state=%s", n, state);
		if (report_on) say("NV_STATE %s", state);
		report("AFTER_STOP");
		if (rc != 0) {
			say("SF_HALT FAILED: the storage controller did not stop; NOT resetting the machine. Reset from outside once kern.sf_nvme.state says stopped");
			for (;;) pause();
		}
	} else if (state[0] && strncmp(state, "stopped", 7) && strncmp(state, "off", 3)) {
		/* the read-only driver, or a write build whose fence never opened: stop it as well */
		rc = set_one("kern.sf_nvme.stop");
		n = rc ? errno : 0;
		rc = wait_stopped(state, sizeof(state), 400);
		say("SF_HALT stop (no open fence) errno=%d state=%s", n, state);
	}

	/* 6 */
	howto = RB_NOSYNC | RB_QUICK | (poweroff ? RB_HALT : RB_AUTOBOOT);
	say("SF_HALT %s now (reboot howto 0x%x)", poweroff ? "powering off" : "restarting", howto);
	if (marker != NULL)
		say("\n%s", marker);
	usleep(700000);         /* the console is a 115200 baud UART on this machine */
	rc = reboot(howto);
	say("SF_HALT reboot returned %d (%s)", rc, strerror(errno));
	for (;;) pause();
}
