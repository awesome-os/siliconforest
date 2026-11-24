"""Exercise the actual readiness adapter on pipes/sockets, without Darwin."""
from pathlib import Path
import subprocess
import tempfile

source = Path(__file__).with_name('net_link.c').read_text()
start = source.index('static int wait_ready(')
end = source.index('\nint main(', start)
adapter = source[start:end]
assert 'int rc = wait_ready(f);' in source
assert 'poll(f,' not in source
head = r'''
#define _DEFAULT_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#ifdef NDEBUG
#error assertions required
#endif
'''
body = r'''
static volatile sig_atomic_t signalled;
static void alarmed(int value) {(void)value; signalled=1;}
static void reset(struct pollfd f[5]) {
  for(int i=0;i<5;i++) f[i]=(struct pollfd){-1,0,0x7fff};
}
int main(void) {
  struct pollfd f[5]; int p[2]; assert(pipe(p)==0); reset(f);
  f[0]=(struct pollfd){p[0],POLLIN,0};
  f[1]=(struct pollfd){p[1],POLLOUT,0};
  assert(wait_ready(f)>0); assert(f[0].revents==0);
  assert(f[1].revents==POLLOUT); assert(f[2].revents==0);
  assert(write(p[1],"a",1)==1); assert(wait_ready(f)>0);
  assert(f[0].revents==POLLIN && f[1].revents==POLLOUT);
  char b; assert(read(p[0],&b,1)==1 && b=='a');
  f[1].events=0; assert(wait_ready(f)==0);
  assert(f[0].revents==0 && f[1].revents==0);
  close(p[1]); assert(wait_ready(f)>0);
  assert(f[0].revents==POLLIN); assert(read(p[0],&b,1)==0);
  close(p[0]); reset(f); f[4]=(struct pollfd){FD_SETSIZE,POLLOUT,0};
  assert(wait_ready(f)==-1 && errno==EINVAL);
  reset(f); f[0]=(struct pollfd){p[0],POLLIN,0};
  assert(wait_ready(f)==-1 && errno==EBADF);
  int s[2]; assert(socketpair(AF_UNIX,SOCK_STREAM,0,s)==0);
  reset(f); f[2]=(struct pollfd){s[0],POLLIN|POLLOUT,0};
  f[3]=(struct pollfd){s[1],POLLIN,0}; assert(wait_ready(f)>0);
  assert(f[2].revents==POLLOUT && f[3].revents==0);
  assert(write(s[0],"b",1)==1); assert(wait_ready(f)>0);
  assert(f[3].revents==POLLIN); assert(read(s[1],&b,1)==1 && b=='b');
  assert(shutdown(s[0],SHUT_WR)==0); assert(wait_ready(f)>0);
  assert(f[3].revents==POLLIN && read(s[1],&b,1)==0);
  close(s[0]); close(s[1]); reset(f);
  struct sigaction action; memset(&action,0,sizeof(action));
  action.sa_handler=alarmed; sigemptyset(&action.sa_mask);
  assert(sigaction(SIGALRM,&action,NULL)==0);
  struct itimerval timer={.it_value={0,1000}};
  assert(setitimer(ITIMER_REAL,&timer,NULL)==0);
  assert(wait_ready(f)==-1 && errno==EINTR && signalled);
  assert(wait_ready(f)==0);
  puts("actual select adapter: read/write, quiet/disabled, EOF, fd cap, EBADF, socket, EINTR PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='darwin-link-select-') as tmp:
    p=Path(tmp)/'check.c'; out=Path(tmp)/'check'
    p.write_text(head+adapter+body)
    cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-Wpedantic',
         '-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(p),'-o',str(out)]
    subprocess.run(cmd,check=True)
    subprocess.run([str(out)],check=True,timeout=5)
    for old,new in [('f[i].revents |= POLLOUT','f[i].revents |= 0'),
                    ('f[i].revents |= POLLIN','f[i].revents |= 0'),
                    ('fd >= FD_SETSIZE','fd > FD_SETSIZE')]:
        p.write_text(head+adapter.replace(old,new)+body)
        subprocess.run(cmd,check=True)
        result=subprocess.run([str(out)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=5)
        assert result.returncode != 0, 'mutation survived: '+old
    print('3 readiness/FD-bound mutants rejected')
