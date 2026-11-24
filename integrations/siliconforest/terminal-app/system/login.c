// SPDX-License-Identifier: MIT
// /usr/bin/login: begin a session for a user on a terminal, in the forms a terminal emulator
// uses (Apple's Terminal runs `login -pf <user>` for a new window and
// `login -pfl <user> /bin/bash -c "exec -la <name> <command>"` for a command):
//   login [-p] [-q] [-l] -f <user> [<program> [<arguments>...]]
//     -f   the caller has authenticated the user: only root, or the user itself, may say so
//     -p   keep the caller's environment (without it only TERM is kept)
//     -l   not a login session: the program's argv[0] gets no leading hyphen and the
//          directory stays (with it absent: the user's home directory, "-zsh")
//     -q   quiet (nothing is printed here in any case)
//   The user's shell from the user database (getpwnam) is run unless a program is given.
//   HOME, SHELL, USER and LOGNAME are set; PATH is set where the caller has none.
// Not here: password logins (there is no password database to check: without -f it refuses),
// -h, utmpx and lastlog records ("Last login:" is not printed), /etc/nologin, the message of the day.
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

static pid_t shellPid;
static void passOn(int sig) { if (shellPid > 0) kill(shellPid, sig); }

int main(int argc, char **argv)
{
    int keep = 0, pre = 0, notlogin = 0, c;
    while ((c = getopt(argc, argv, "pqflh:")) != -1) {
        switch (c) {
        case 'p': keep = 1; break;
        case 'f': pre = 1; break;
        case 'l': notlogin = 1; break;
        case 'q': break;
        default: fprintf(stderr, "usage: login [-pql] -f user [program [arguments...]]\n"); return 1;
        }
    }
    argc -= optind; argv += optind;
    if (!pre || argc < 1) { fprintf(stderr, "login: only pre-authenticated logins are supported here (login -f user)\n"); return 1; }
    struct passwd *pw = getpwnam(argv[0]);
    if (pw == NULL) { fprintf(stderr, "login: no user %s\n", argv[0]); return 1; }
    if (getuid() != 0 && getuid() != pw->pw_uid) { fprintf(stderr, "login: -f is for root or for %s\n", pw->pw_name); return 1; }
    const char *shell = (pw->pw_shell && pw->pw_shell[0]) ? pw->pw_shell : "/bin/sh";
    if (getuid() == 0 && pw->pw_uid != 0) {
        if (initgroups(pw->pw_name, pw->pw_gid) != 0 || setgid(pw->pw_gid) != 0 || setuid(pw->pw_uid) != 0) {
            fprintf(stderr, "login: cannot become %s: %s\n", pw->pw_name, strerror(errno)); return 1;
        }
    }
    setlogin(pw->pw_name);
    if (!keep) {
        static char *empty[] = { NULL };
        const char *term = getenv("TERM"); char *saved = term ? strdup(term) : NULL;
        environ = empty;
        if (saved) setenv("TERM", saved, 1);
    }
    setenv("HOME", pw->pw_dir, 1); setenv("SHELL", shell, 1);
    setenv("USER", pw->pw_name, 1); setenv("LOGNAME", pw->pw_name, 1);
    setenv("PATH", "/usr/local/bin:/opt/homebrew/bin:/opt/homebrew/sbin:/usr/bin:/bin:/usr/sbin:/sbin", 0);
    if (!notlogin && chdir(pw->pw_dir) != 0) (void)chdir("/");

    const char *prog = argc > 1 ? argv[1] : shell;
    const char *leaf = strrchr(prog, '/'); leaf = leaf ? leaf + 1 : prog;
    char **child = calloc((size_t)argc + 2, sizeof(char *));
    char *zero = malloc(strlen(leaf) + 2);
    if (child == NULL || zero == NULL) return 1;
    sprintf(zero, "%s%s", notlogin ? "" : "-", leaf);
    child[0] = zero;
    for (int i = 2; i < argc; i++) child[i - 1] = argv[i];
    /* As macOS's login, this process stays the program's parent until it ends: Apple's Terminal
     * finds the shell as the child of the process it started, sends it SIGHUP when the window or
     * the application closes, and waits for that process (login) to end. With login replaced by
     * the shell nothing got the signal and Terminal waited 60 s before its SIGKILL went to pid 0. */
    pid_t pid = fork();
    if (pid < 0) { fprintf(stderr, "login: fork: %s\n", strerror(errno)); return 1; }
    if (pid == 0) {
        execv(prog, child);
        fprintf(stderr, "login: %s: %s\n", prog, strerror(errno));
        _exit(1);
    }
    // the session's signals are the program's: a hangup of the terminal or a TERM sent to login
    // is passed on; interrupts typed at the terminal reach the program's process group directly
    shellPid = pid;
    signal(SIGINT, SIG_IGN); signal(SIGQUIT, SIG_IGN); signal(SIGTSTP, SIG_IGN);
    signal(SIGHUP, passOn); signal(SIGTERM, passOn);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return WEXITSTATUS(status);
}
