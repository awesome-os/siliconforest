#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
/* Finite guest PTY smoke. Not an interactive terminal and not a Wayland UI.
 * openpty is called with null termios and winsize; those settings are applied
 * and checked afterward. The child is a real session: setsid, TIOCSCTTY, dup
 * of stdio, then exec /bin/sh -c. PASS requires the joined nonce line to sit
 * immediately against Darwin, arm64, and one release token, and the child to
 * exit 0. Anything else, including a host uname, is FAIL.
 */
#include "transcript.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <util.h>

#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/wait.h>

#define DEADLINE_MS 10000
#define GRACE_MS 1000
#define OUT_CAP 65536
/* Bytes read from the master before drain_master returns to the deadline. */
#define DRAIN_CALL_MAX 8192
/* Inherited fds above this are not walked; a larger soft limit would stall the child. */
#define INHERITED_FD_CAP 10240

struct smoke {
	int master;
	int slave;
	int status_r;
	int status_w;
	pid_t child;
	int reaped;
	int signaled;
	int setsid_ok;
	int setup_ok;
	int setup_stage;
	int setup_errno;
	int exec_boundary;
	int normal_exit;
	int exit_code;
	int term_sig;
	char failure[16];
	char cleanup[20];
	char tx[64];
	size_t tx_len;
	size_t tx_off;
	char out[OUT_CAP];
	size_t out_len;
	int output_capped;
};

static void set_failure(struct smoke *s, const char *code)
{
	if (s->failure[0] == '\0') {
		snprintf(s->failure, sizeof s->failure, "%s", code);
	}
}

static void close_fd(int *fd)
{
	if (fd != NULL && *fd >= 0) {
		(void)close(*fd);
		*fd = -1;
	}
}

static int raise_fd(int fd)
{
	int flags;
	int raised;

	if (fd < 0) {
		return -1;
	}
	if (fd > 2) {
		flags = fcntl(fd, F_GETFD);
		if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
			return -1;
		}
		return fd;
	}
	raised = fcntl(fd, F_DUPFD_CLOEXEC, 3);
	if (raised < 0) {
		return -1;
	}
	(void)close(fd);
	return raised;
}

static int set_nonblock(int fd)
{
	int flags = fcntl(fd, F_GETFL);
	if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
		return -1;
	}
	return 0;
}

static long long monotonic_ms(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return -1;
	}
	return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static int append_uint(char *dst, char *end, unsigned value)
{
	char rev[16];
	int n = 0;
	int i;

	if (value == 0) {
		rev[n++] = '0';
	} else {
		while (value > 0 && n < (int)sizeof rev) {
			rev[n++] = (char)('0' + (value % 10u));
			value /= 10u;
		}
	}
	if (dst + n >= end) {
		return -1;
	}
	for (i = n - 1; i >= 0; i--) {
		*dst++ = rev[i];
	}
	return n;
}

static void write_setup(int fd, int stage, int err, int setsid_ok)
{
	char buf[64];
	char *p = buf;
	char *end = buf + sizeof buf;
	int n;
	const char *prefix = "SETUP ";

	memcpy(p, prefix, 6);
	p += 6;
	n = append_uint(p, end, (unsigned)stage);
	if (n < 0) {
		return;
	}
	p += n;
	if (p >= end) {
		return;
	}
	*p++ = ' ';
	n = append_uint(p, end, (unsigned)err);
	if (n < 0) {
		return;
	}
	p += n;
	if (p >= end) {
		return;
	}
	*p++ = ' ';
	n = append_uint(p, end, (unsigned)setsid_ok);
	if (n < 0) {
		return;
	}
	p += n;
	if (p >= end) {
		return;
	}
	*p++ = '\n';
	(void)write(fd, buf, (size_t)(p - buf));
}

static void reset_child_signals(void)
{
	int sigs[] = {SIGHUP, SIGINT, SIGQUIT, SIGTERM, SIGTSTP, SIGTTIN, SIGTTOU, SIGWINCH, SIGPIPE};
	sigset_t set;
	struct sigaction sa;
	size_t i;

	sigemptyset(&set);
	(void)sigprocmask(SIG_SETMASK, &set, NULL);
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = SIG_DFL;
	for (i = 0; i < sizeof sigs / sizeof sigs[0]; i++) {
		(void)sigaction(sigs[i], &sa, NULL);
	}
}

static int inherited_fd_bound(void)
{
	struct rlimit rl;

	/* Computed in the parent. getrlimit is not async-signal-safe after fork. */
	if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY &&
		rl.rlim_cur > (rlim_t)3 && rl.rlim_cur <= (rlim_t)INHERITED_FD_CAP) {
		return (int)rl.rlim_cur;
	}
	return INHERITED_FD_CAP;
}

static void close_inherited(int maxfd, int slave, int status_w)
{
	int fd;

	for (fd = 3; fd < maxfd; fd++) {
		if (fd == slave || fd == status_w) {
			continue;
		}
		(void)close(fd);
	}
}

static void child_main(int slave, int status_w, int maxfd)
{
	char *argv[4];
	char *envp[7];

	close_inherited(maxfd, slave, status_w);
	reset_child_signals();
	if (setsid() < 0) {
		write_setup(status_w, 1, errno, 0);
		_exit(126);
	}
	if (ioctl(slave, TIOCSCTTY, 0) < 0) {
		write_setup(status_w, 2, errno, 1);
		_exit(126);
	}
	if (dup2(slave, 0) < 0 || dup2(slave, 1) < 0 || dup2(slave, 2) < 0) {
		write_setup(status_w, 3, errno, 1);
		_exit(126);
	}
	if (slave > 2) {
		(void)close(slave);
	}
	if (!isatty(0) || getsid(0) != getpid() || getpgrp() != getpid() ||
		tcgetpgrp(0) != getpgrp()) {
		write_setup(status_w, 4, errno, 1);
		_exit(126);
	}
	write_setup(status_w, 0, 0, 1);
	argv[0] = "sh";
	argv[1] = "-c";
	argv[2] = (char *)smoke_script();
	argv[3] = NULL;
	envp[0] = "TERM=dumb";
	envp[1] = "PATH=/bin:/usr/bin";
	envp[2] = "HOME=/var/empty";
	envp[3] = "HISTFILE=/dev/null";
	envp[4] = "SAVEHIST=0";
	envp[5] = "LANG=C";
	envp[6] = NULL;
	execve("/bin/sh", argv, envp);
	write_setup(status_w, 5, errno, 1);
	_exit(127);
}

static int configure_slave(int slave)
{
	struct termios term;
	struct termios back;
	struct winsize ws;
	struct winsize got;

	if (tcgetattr(slave, &term) < 0) {
		return -1;
	}
	term.c_iflag |= ICRNL;
	term.c_oflag |= OPOST | ONLCR;
	term.c_lflag |= ISIG | ICANON | ECHO | ECHOE;
	term.c_cc[VEOF] = 4;
	term.c_cc[VERASE] = 0177;
	term.c_cc[VINTR] = 3;
	if (tcsetattr(slave, TCSANOW, &term) < 0) {
		return -1;
	}
	if (tcgetattr(slave, &back) < 0) {
		return -1;
	}
	if ((back.c_lflag & (ISIG | ICANON)) != (ISIG | ICANON)) {
		return -1;
	}
	memset(&ws, 0, sizeof ws);
	ws.ws_row = 24;
	ws.ws_col = 80;
	if (ioctl(slave, TIOCSWINSZ, &ws) < 0) {
		return -2;
	}
	memset(&got, 0, sizeof got);
	if (ioctl(slave, TIOCGWINSZ, &got) < 0 || got.ws_row != 24 || got.ws_col != 80) {
		return -2;
	}
	return 0;
}

static void note_status(struct smoke *s, int status)
{
	s->reaped = 1;
	s->child = -1;
	if (WIFEXITED(status)) {
		s->normal_exit = 1;
		s->exit_code = WEXITSTATUS(status);
		s->term_sig = 0;
	} else if (WIFSIGNALED(status)) {
		s->normal_exit = 0;
		s->exit_code = -1;
		s->term_sig = WTERMSIG(status);
	} else {
		s->normal_exit = 0;
		s->exit_code = -1;
	}
}

static int reap_now(struct smoke *s)
{
	int status = 0;
	pid_t got;

	if (s->reaped || s->child <= 1) {
		return s->reaped;
	}
	got = waitpid(s->child, &status, WNOHANG);
	if (got == s->child) {
		note_status(s, status);
		return 1;
	}
	if (got < 0 && errno == ECHILD) {
		s->reaped = 1;
		s->child = -1;
		return 1;
	}
	return 0;
}

static void note_cap(struct smoke *s)
{
	s->output_capped = 1;
	/* Cap disqualifies a tuple that already matched. Do not keep the deadline running. */
	if (s->failure[0] == '\0') {
		snprintf(s->failure, sizeof s->failure, "%s", "output_cap");
	}
}

static void drain_master(struct smoke *s)
{
	char buf[1024];
	size_t budget = DRAIN_CALL_MAX;

	if (s->master < 0 || s->output_capped) {
		return;
	}
	while (budget > 0) {
		size_t chunk = sizeof buf;
		ssize_t n;
		size_t got;
		size_t take;

		if (chunk > budget) {
			chunk = budget;
		}
		n = read(s->master, buf, chunk);
		if (n > 0) {
			got = (size_t)n;
			budget -= got;
			if (s->out_len >= OUT_CAP) {
				note_cap(s);
				return;
			}
			take = got;
			if (take > OUT_CAP - s->out_len) {
				take = OUT_CAP - s->out_len;
			}
			memcpy(s->out + s->out_len, buf, take);
			s->out_len += take;
			if (s->out_len >= OUT_CAP) {
				note_cap(s);
				return;
			}
			continue;
		}
		if (n == 0 || (n < 0 && errno != EINTR && errno != EAGAIN)) {
			return;
		}
		if (n < 0 && errno == EAGAIN) {
			return;
		}
	}
}

static void signal_owned(struct smoke *s, int sig)
{
	if (s->reaped || s->child <= 1) {
		return;
	}
	/* Carrier drop on master close does not send SIGHUP. Signal explicitly. */
	if (s->setsid_ok) {
		(void)kill(-s->child, sig);
	} else {
		(void)kill(s->child, sig);
	}
	s->signaled = 1;
}

static int reap_for(struct smoke *s, int budget_ms)
{
	long long start = monotonic_ms();

	if (start < 0) {
		return 0;
	}
	while (!reap_now(s)) {
		long long now = monotonic_ms();
		int slice;
		if (now < 0 || now - start >= budget_ms) {
			return 0;
		}
		drain_master(s);
		slice = (int)(budget_ms - (now - start));
		if (slice > 50) {
			slice = 50;
		}
		(void)poll(NULL, 0, slice);
	}
	drain_master(s);
	return 1;
}

static void cleanup(struct smoke *s)
{
	pid_t owned = s->child;

	if (!s->reaped && owned > 1) {
		signal_owned(s, SIGTERM);
		if (!reap_for(s, GRACE_MS) && !s->reaped) {
			s->child = owned;
			signal_owned(s, SIGKILL);
			(void)reap_for(s, GRACE_MS);
		}
	}
	close_fd(&s->master);
	close_fd(&s->slave);
	close_fd(&s->status_r);
	close_fd(&s->status_w);
	if (s->reaped) {
		snprintf(s->cleanup, sizeof s->cleanup, "%s",
			s->signaled ? "term_kill_reaped" : "reaped");
	} else if (owned > 1) {
		snprintf(s->cleanup, sizeof s->cleanup, "%s", "child_still_live");
		set_failure(s, "cleanup");
	} else {
		snprintf(s->cleanup, sizeof s->cleanup, "%s", "no_child");
	}
}

static int take_setup(struct smoke *s, char *acc, size_t *acc_len)
{
	size_t i;

	for (i = 0; i < *acc_len; i++) {
		int stage = 0;
		int err = 0;
		int setsid_ok = 0;
		if (acc[i] != '\n') {
			continue;
		}
		acc[i] = '\0';
		if (sscanf(acc, "SETUP %d %d %d", &stage, &err, &setsid_ok) == 3) {
			s->setup_stage = stage;
			s->setup_errno = err;
			if (setsid_ok) {
				s->setsid_ok = 1;
			}
			if (stage == 0) {
				s->setup_ok = 1;
			} else if (stage == 5) {
				set_failure(s, "exec");
			} else {
				set_failure(s, "setup");
			}
		}
		memmove(acc, acc + i + 1, *acc_len - i - 1);
		*acc_len -= i + 1;
		return 1;
	}
	return 0;
}

static void read_status(struct smoke *s, char *acc, size_t *acc_len)
{
	char buf[64];

	for (;;) {
		ssize_t n = read(s->status_r, buf, sizeof buf);
		if (n > 0) {
			size_t take = (size_t)n;
			if (take > (sizeof buf) || *acc_len + take >= 128) {
				set_failure(s, "setup");
				close_fd(&s->status_r);
				return;
			}
			memcpy(acc + *acc_len, buf, take);
			*acc_len += take;
			while (take_setup(s, acc, acc_len)) {
			}
			continue;
		}
		if (n < 0 && errno == EINTR) {
			continue;
		}
		if (n < 0 && errno == EAGAIN) {
			return;
		}
		/* EOF with SETUP 0 and no stage-5 record is the exec boundary. */
		if (n == 0 && s->setup_ok && s->setup_stage == 0) {
			s->exec_boundary = 1;
		}
		close_fd(&s->status_r);
		return;
	}
}

static int write_tx(struct smoke *s)
{
	while (s->tx_off < s->tx_len) {
		ssize_t n = write(s->master, s->tx + s->tx_off, s->tx_len - s->tx_off);
		if (n > 0) {
			s->tx_off += (size_t)n;
			continue;
		}
		if (n < 0 && errno == EINTR) {
			continue;
		}
		if (n < 0 && errno == EAGAIN) {
			return 0;
		}
		set_failure(s, "write");
		return -1;
	}
	return 1;
}

static void decide(struct smoke *s, const char *nonce)
{
	int adjacent;
	int in_tx;

	in_tx = contains_span(s->tx, s->tx_len, nonce, 48) ||
		contains_span(smoke_script(), strlen(smoke_script()), nonce, 48);
	adjacent = adjacent_uname(s->out, s->out_len, nonce);
	if (s->output_capped && s->failure[0] == '\0') {
		snprintf(s->failure, sizeof s->failure, "%s", "output_cap");
	}
	if (in_tx) {
		set_failure(s, "tx_guard");
	}
	if (!s->setup_ok) {
		set_failure(s, s->failure[0] ? s->failure : "setup");
	} else if (!s->output_capped && !adjacent) {
		set_failure(s, s->out_len == 0 ? "timeout" : "parse");
	} else if (!s->output_capped &&
		!(s->reaped && s->normal_exit && s->exit_code == 0 && !s->signaled)) {
		set_failure(s, "child_status");
	}
	if (s->failure[0] == '\0' && s->setup_ok && adjacent && !in_tx &&
		!s->output_capped && s->reaped && s->normal_exit && s->exit_code == 0 &&
		!s->signaled && strcmp(s->cleanup, "reaped") == 0) {
		snprintf(s->failure, sizeof s->failure, "%s", "none");
	} else if (s->failure[0] == '\0') {
		set_failure(s, "parse");
	}
}

static void emit_hex_line(const char *tag, const char *buf, size_t len)
{
	static const char hexd[] = "0123456789abcdef";
	size_t i;

	fputs(tag, stdout);
	for (i = 0; i < len; i++) {
		unsigned char c = (unsigned char)buf[i];
		putchar(hexd[c >> 4]);
		putchar(hexd[c & 0x0f]);
	}
	putchar('\n');
}

static void emit_json(const struct smoke *s, const char *nonce)
{
	int pass = strcmp(s->failure, "none") == 0;
	int adjacent = adjacent_uname(s->out, s->out_len, nonce);

	printf("{\"name\":\"PTY_SMOKE\",\"mode\":\"FINITE_SH_C\",\"interactive\":0,"
		"\"status\":\"%s\",\"failure\":\"%s\",\"cleanup\":\"%s\","
		"\"child_exit\":%d,\"normal_exit\":%d,\"term_sig\":%d,"
		"\"setup\":%d,\"setup_stage\":%d,\"setup_errno\":%d,\"exec_boundary\":%d,"
		"\"adjacent\":%d,\"joined_nonce_in_tx\":%d,\"output_bytes\":%zu,"
		"\"output_capped\":%d,\"signaled\":%d,\"deadline_ms\":%d}\n",
		pass ? "PASS" : "FAIL",
		s->failure,
		s->cleanup[0] ? s->cleanup : "no_child",
		s->normal_exit ? s->exit_code : -1,
		s->normal_exit,
		s->term_sig,
		s->setup_ok,
		s->setup_stage,
		s->setup_errno,
		s->exec_boundary,
		adjacent,
		contains_span(s->tx, s->tx_len, nonce, 48),
		s->out_len,
		s->output_capped,
		s->signaled,
		DEADLINE_MS);
	fflush(stdout);
	/* Hex after the classifier. The joined nonce is not printed as text. */
	emit_hex_line("rx_hex ", s->out, s->out_len);
	emit_hex_line("tx_hex ", s->tx, s->tx_len);
}

static int run(const char *nonce)
{
	struct smoke s;
	int pipes[2];
	int configured;
	long long start;
	char setup_acc[128];
	size_t setup_len = 0;
	int tx_done = 0;

	memset(&s, 0, sizeof s);
	s.master = -1;
	s.slave = -1;
	s.status_r = -1;
	s.status_w = -1;
	s.child = -1;
	s.exit_code = -1;
	s.setup_stage = -1;
	if (!tx_halves(nonce, s.tx, sizeof s.tx, &s.tx_len)) {
		set_failure(&s, "tx_guard");
		snprintf(s.cleanup, sizeof s.cleanup, "%s", "no_child");
		emit_json(&s, nonce);
		return 1;
	}
	if (openpty(&s.master, &s.slave, NULL, NULL, NULL) < 0) {
		set_failure(&s, "openpty");
		goto out;
	}
	{
		int master = raise_fd(s.master);
		if (master < 0) {
			set_failure(&s, "fd");
			goto out;
		}
		s.master = master;
		{
			int slave = raise_fd(s.slave);
			if (slave < 0) {
				set_failure(&s, "fd");
				goto out;
			}
			s.slave = slave;
		}
	}
	configured = configure_slave(s.slave);
	if (configured == -2) {
		set_failure(&s, "winsize");
		goto out;
	}
	if (configured != 0) {
		set_failure(&s, "termios");
		goto out;
	}
	if (set_nonblock(s.master) < 0) {
		set_failure(&s, "fd");
		goto out;
	}
	if (pipe(pipes) < 0) {
		set_failure(&s, "pipe");
		goto out;
	}
	s.status_r = pipes[0];
	s.status_w = pipes[1];
	{
		int rd = raise_fd(s.status_r);
		if (rd < 0) {
			set_failure(&s, "fd");
			goto out;
		}
		s.status_r = rd;
		{
			int wr = raise_fd(s.status_w);
			if (wr < 0) {
				set_failure(&s, "fd");
				goto out;
			}
			s.status_w = wr;
		}
	}
	if (set_nonblock(s.status_r) < 0) {
		set_failure(&s, "fd");
		goto out;
	}
	{
		struct sigaction sa;
		int fd_bound = inherited_fd_bound();

		memset(&sa, 0, sizeof sa);
		sa.sa_handler = SIG_DFL;
		sigemptyset(&sa.sa_mask);
		if (sigaction(SIGCHLD, &sa, NULL) < 0) {
			set_failure(&s, "sigchld");
			goto out;
		}
		s.child = fork();
		if (s.child < 0) {
			set_failure(&s, "fork");
			goto out;
		}
		if (s.child == 0) {
			int slave = s.slave;
			int status_w = s.status_w;

			close_fd(&s.master);
			close_fd(&s.status_r);
			child_main(slave, status_w, fd_bound);
		}
	}
	close_fd(&s.slave);
	close_fd(&s.status_w);
	start = monotonic_ms();
	if (start < 0) {
		set_failure(&s, "timeout");
		goto out;
	}
	while (monotonic_ms() - start < DEADLINE_MS && !s.reaped) {
		struct pollfd pf[2];
		int nfd = 0;
		long long now = monotonic_ms();
		int wait_ms;
		int pr;

		if (now < 0) {
			set_failure(&s, "timeout");
			break;
		}
		wait_ms = (int)(DEADLINE_MS - (now - start));
		if (wait_ms < 0) {
			break;
		}
		if (s.master >= 0) {
			pf[nfd].fd = s.master;
			pf[nfd].events = POLLIN;
			if (s.setup_ok && !tx_done) {
				pf[nfd].events = (short)(pf[nfd].events | POLLOUT);
			}
			pf[nfd].revents = 0;
			nfd++;
		}
		if (s.status_r >= 0) {
			pf[nfd].fd = s.status_r;
			pf[nfd].events = POLLIN;
			pf[nfd].revents = 0;
			nfd++;
		}
		pr = poll(pf, (nfds_t)nfd, wait_ms);
		if (pr < 0) {
			if (errno == EINTR) {
				continue;
			}
			set_failure(&s, "poll");
			break;
		}
		if (s.status_r >= 0) {
			read_status(&s, setup_acc, &setup_len);
		}
		if (s.failure[0] != '\0' && !s.setup_ok) {
			break;
		}
		if (s.setup_ok && !tx_done && s.master >= 0) {
			int wr = write_tx(&s);
			if (wr < 0) {
				break;
			}
			if (wr > 0) {
				tx_done = 1;
			}
		}
		drain_master(&s);
		if (s.output_capped) {
			break;
		}
		(void)reap_now(&s);
		if (s.setup_ok && adjacent_uname(s.out, s.out_len, nonce) && s.reaped) {
			break;
		}
	}
out:
	(void)reap_now(&s);
	if (s.reaped) {
		for (;;) {
			size_t before = s.out_len;

			drain_master(&s);
			if (s.output_capped || s.out_len == before) {
				break;
			}
		}
	} else {
		drain_master(&s);
	}
	if (!s.reaped) {
		cleanup(&s);
	} else {
		close_fd(&s.master);
		close_fd(&s.slave);
		close_fd(&s.status_r);
		close_fd(&s.status_w);
		snprintf(s.cleanup, sizeof s.cleanup, "%s",
			s.signaled ? "term_kill_reaped" : "reaped");
	}
	decide(&s, nonce);
	emit_json(&s, nonce);
	return strcmp(s.failure, "none") == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
	const char *nonce = NULL;
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--nonce") == 0 && i + 1 < argc) {
			nonce = argv[++i];
		}
	}
	if (!nonce_hex48(nonce)) {
		printf("{\"name\":\"PTY_SMOKE\",\"mode\":\"FINITE_SH_C\",\"interactive\":0,"
			"\"status\":\"FAIL\",\"failure\":\"bad_nonce\",\"cleanup\":\"no_child\","
			"\"child_exit\":-1,\"normal_exit\":0,\"term_sig\":0,\"setup\":0,"
			"\"setup_stage\":-1,\"setup_errno\":0,\"exec_boundary\":0,\"adjacent\":0,"
			"\"joined_nonce_in_tx\":0,\"output_bytes\":0,\"output_capped\":0,"
			"\"signaled\":0,\"deadline_ms\":%d}\n",
			DEADLINE_MS);
		fflush(stdout);
		emit_hex_line("rx_hex ", "", 0);
		emit_hex_line("tx_hex ", "", 0);
		return 1;
	}
	return run(nonce);
}
