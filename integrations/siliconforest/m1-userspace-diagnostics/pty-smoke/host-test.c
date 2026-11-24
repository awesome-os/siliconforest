/* Host checks for the finite PTY smoke. A Linux PTY session is HOST_ONLY.
 * It is never a Darwin PASS.
 */
#define _GNU_SOURCE
#include "transcript.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

static int g_fails;

static void expect(int cond, const char *name)
{
	if (!cond) {
		fprintf(stderr, "host-test fail: %s\n", name);
		g_fails++;
	}
}

static void test_nonce(void)
{
	char good[49];
	char bad[64];
	size_t i;

	for (i = 0; i < 48; i++) {
		good[i] = (i % 2) ? 'a' : '1';
	}
	good[48] = '\0';
	expect(nonce_hex48(good), "hex48");
	expect(!nonce_hex48(NULL), "null nonce");
	expect(!nonce_hex48(""), "empty nonce");
	memcpy(bad, good, 49);
	bad[47] = '\0';
	expect(!nonce_hex48(bad), "short nonce");
	memcpy(bad, good, 48);
	bad[48] = 'a';
	bad[49] = '\0';
	expect(!nonce_hex48(bad), "long nonce");
	memcpy(bad, good, 49);
	bad[3] = 'g';
	expect(!nonce_hex48(bad), "nonhex");
	bad[3] = 'A';
	expect(nonce_hex48(bad), "upper hex");
}

static void test_echo_and_parser(void)
{
	char nonce[49];
	char tx[64];
	char buf[1024];
	size_t tx_len = 0;
	size_t i;
	const char *script = smoke_script();
	int n;

	for (i = 0; i < 48; i++) {
		nonce[i] = 'a';
	}
	nonce[48] = '\0';
	expect(tx_halves(nonce, tx, sizeof tx, &tx_len), "tx halves");
	expect(tx_len == 50, "tx length");
	expect(!contains_span(tx, tx_len, nonce, 48), "joined absent from tx");
	expect(!contains_span(script, strlen(script), nonce, 48), "joined absent from script");
	expect(strcmp(script,
		"read a || exit 2; read b || exit 2; "
		"printf '%s%s\\n' \"$a\" \"$b\"; "
		"/bin/uname -s; /bin/uname -m; /bin/uname -r; exit 0") == 0,
		"script frozen");
	expect(strstr(script, "Darwin") == NULL, "script has no Darwin");
	expect(strstr(script, "arm64") == NULL, "script has no arm64");
	expect(strstr(script, " -i") == NULL, "script is not interactive");

	n = snprintf(buf, sizeof buf, "%s\n%s", script, tx);
	expect(n > 0 && (size_t)n < sizeof buf, "echo fixture fits");
	expect(!adjacent_uname(buf, (size_t)n, nonce), "echo is not a pass");

	n = snprintf(buf, sizeof buf,
		"%% \n%.24s\n%.24s\n%s\nDarwin\narm64\n25.0.0\n%% \n",
		nonce, nonce + 24, nonce);
	expect(n > 0, "good fixture");
	expect(adjacent_uname(buf, (size_t)n, nonce), "adjacent Darwin tuple");

	n = snprintf(buf, sizeof buf, "%s\r\nDarwin\r\narm64\r\n25.0.0\r\n", nonce);
	expect(adjacent_uname(buf, (size_t)n, nonce), "crlf tuple");

	n = snprintf(buf, sizeof buf, "%s\nDarwin\n\narm64\n25.0.0\n", nonce);
	expect(!adjacent_uname(buf, (size_t)n, nonce), "gap is not adjacent");

	n = snprintf(buf, sizeof buf, "%s\narm64\nDarwin\n25.0.0\n", nonce);
	expect(!adjacent_uname(buf, (size_t)n, nonce), "swapped names");

	n = snprintf(buf, sizeof buf, "%s\nDarwin\narm64\n%%\n", nonce);
	expect(!adjacent_uname(buf, (size_t)n, nonce), "prompt is not a release");

	n = snprintf(buf, sizeof buf, "%s\nLinux\nx86_64\n6.8.0\n", nonce);
	expect(!adjacent_uname(buf, (size_t)n, nonce), "Linux is not Darwin");

	n = snprintf(buf, sizeof buf, "%s\nDarwin\narm64\n25.0.0", nonce);
	expect(!adjacent_uname(buf, (size_t)n, nonce), "unterminated release");
}

static int reap_child(pid_t pid, int *status)
{
	for (;;) {
		pid_t got = waitpid(pid, status, 0);
		if (got == pid) {
			return 1;
		}
		if (got < 0 && errno == EINTR) {
			continue;
		}
		return 0;
	}
}

static void test_posix_pty(void)
{
	int master = -1;
	int slave = -1;
	pid_t pid;
	int status = 0;
	char *argv[4];

	if (openpty(&master, &slave, NULL, NULL, NULL) < 0) {
		expect(0, "host openpty");
		return;
	}
	pid = fork();
	if (pid < 0) {
		expect(0, "host fork");
		close(master);
		close(slave);
		return;
	}
	if (pid == 0) {
		if (setsid() < 0 || ioctl(slave, TIOCSCTTY, 0) < 0 ||
			dup2(slave, 0) < 0 || dup2(slave, 1) < 0 || dup2(slave, 2) < 0) {
			_exit(126);
		}
		if (slave > 2) {
			close(slave);
		}
		close(master);
		argv[0] = "sh";
		argv[1] = "-c";
		argv[2] = "exit 0";
		argv[3] = NULL;
		execv("/bin/sh", argv);
		_exit(127);
	}
	close(slave);
	expect(reap_child(pid, &status), "host reap");
	expect(WIFEXITED(status) && WEXITSTATUS(status) == 0, "host shell exit 0");
	close(master);

	if (openpty(&master, &slave, NULL, NULL, NULL) < 0) {
		expect(0, "host openpty kill");
		return;
	}
	pid = fork();
	if (pid == 0) {
		if (setsid() < 0 || ioctl(slave, TIOCSCTTY, 0) < 0 ||
			dup2(slave, 0) < 0 || dup2(slave, 1) < 0 || dup2(slave, 2) < 0) {
			_exit(126);
		}
		if (slave > 2) {
			close(slave);
		}
		close(master);
		argv[0] = "sh";
		argv[1] = "-c";
		argv[2] = "sleep 30";
		argv[3] = NULL;
		execv("/bin/sh", argv);
		_exit(127);
	}
	if (pid < 0) {
		expect(0, "host fork kill");
		close(master);
		close(slave);
		return;
	}
	close(slave);
	/* Master stays open. Shutdown is an explicit signal, not SIGHUP-on-close. */
	expect(master >= 0, "master still open");
	expect(kill(pid, SIGTERM) == 0, "explicit term");
	expect(reap_child(pid, &status), "kill reap");
	expect(WIFSIGNALED(status) || WIFEXITED(status), "kill produced a status");
	close(master);
}

int main(void)
{
	test_nonce();
	test_echo_and_parser();
	test_posix_pty();
	printf("{\"name\":\"PTY_SMOKE_HOST\",\"status\":\"%s\",\"darwin_pass\":0,"
		"\"parser_and_pty_failures\":%d,\"mode\":\"HOST_ONLY_NOT_DARWIN\"}\n",
		g_fails == 0 ? "HOST_ONLY" : "HOST_ONLY_FAIL", g_fails);
	return g_fails == 0 ? 0 : 1;
}
