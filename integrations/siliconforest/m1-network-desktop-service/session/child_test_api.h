/* Host fault adapter: source-bound tests only, never part of Darwin binary. */
pid_t test_waitpid(pid_t,int *,int);
int test_kill(pid_t,int);
#define waitpid test_waitpid
#define kill test_kill
