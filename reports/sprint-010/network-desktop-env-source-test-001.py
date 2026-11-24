#!/usr/bin/env python3
"""Host-only ENV/foreground-child proof fixtures; no terminal implementation/network."""
import errno
import hashlib
import json
import os
from pathlib import Path
import pty
import re
import select
import signal
import subprocess
import tempfile
import time

ZSH = '/nix/store/mxc65l88fllgzrf0gkh54nr9r96mki9y-zsh-5.9.1/bin/zsh'
CLIENT = r'''
#define _DEFAULT_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
static const char *path;
static void *worker(void *unused) {
    (void)unused;
    for (int i=0;i<24;++i) { if(open(path,O_RDONLY)<0) _exit(90); usleep(1000); }
    return NULL;
}
int main(int argc,char **argv) {
    if(argc!=2) return 91;
    for(int fd=3;fd<1024;++fd) if(fcntl(fd,F_GETFD)>=0) return 92;
    path=argv[1]; pthread_t thread;
    if(pthread_create(&thread,NULL,worker,NULL)) return 93;
    if(pthread_join(thread,NULL)) return 94;
    printf("CLIENT pid=%ld ppid=%ld sid=%ld pgid=%ld fg=%ld extra=%d\n",
      (long)getpid(),(long)getppid(),(long)getsid(0),(long)getpgrp(),(long)tcgetpgrp(0),fcntl(3,F_GETFD)>=0);
    fflush(stdout);
    /* Deliberate non-CLOEXEC fds left open: process exit, no second exec. */
    _exit(7);
}
'''

def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def scenario(temp, name, envfile, expect, privileged=False):
    child, master = pty.fork()
    if child == 0:
        for fd in range(3, 1024):
            try: os.close(fd)
            except OSError: pass
        env = {'TERM':'dumb','LC_ALL':'C','PATH':'/bin:/usr/bin',
               'HOME':str(temp),'ENV':str(envfile),'ZDOTDIR':str(temp/'absent'),
               'HISTFILE':'/dev/null','PS1':'ENV_TEST_PROMPT> ','PS2':'> '}
        os.execve(ZSH,['sh','-i','+m']+(['-p'] if privileged else []),env)
    data = bytearray(); deadline = time.monotonic()+8
    try:
        while time.monotonic()<deadline and b'ENV_TEST_PROMPT> ' not in data:
            ready,_,_=select.select([master],[],[],0.05)
            if ready:
                try: part=os.read(master,4096)
                except OSError as e:
                    if e.errno==errno.EIO: break
                    raise
                if not part: break
                data.extend(part)
        text=data.decode('ascii',errors='replace')
        assert 'ENV_TEST_PROMPT> ' in text, (name,text)
        if expect=='client':
            match=re.search(r'CLIENT pid=(\d+) ppid=(\d+) sid=(\d+) pgid=(\d+) fg=(\d+) extra=(\d+)',text)
            assert match, text
            client,ppid,sid,pgid,fg,extra=map(int,match.groups())
            assert client!=child and ppid==sid==pgid==fg==child and extra==1,text
            assert f'AFTER shell={child} status=7' in text,text
            assert text.index('CLIENT ')<text.index('AFTER ')<text.index('ENV_TEST_PROMPT> '),text
            assert not Path(f'/proc/{client}').exists(), 'Client was not reaped before prompt'
            targets=[]
            for fd in Path(f'/proc/{child}/fd').iterdir():
                try: targets.append(os.readlink(fd))
                except FileNotFoundError: pass
            assert str(temp/'sentinel') not in targets,targets
            assert str(envfile) not in targets,targets
            evidence={'distinct_client_pid':True,'same_session_and_foreground_group':True,
                'child_reaped_before_prompt':True,'child_noncloexec_descriptors_not_in_shell':True,
                'script_input_closed_before_prompt':True,'exit_status':7}
        else:
            assert 'CLIENT ' not in text and 'AFTER ' not in text,text
            assert ('COMPILED_OVERRIDE' in text)==(expect=='compiled'),text
            evidence={'client_not_run':True,'prompt_alone_not_success':True,
                'compiled_sibling_overrode_text':expect=='compiled'}
        return {'name':name,'passed':True,**evidence}
    finally:
        os.killpg(child,signal.SIGKILL); os.waitpid(child,0); os.close(master)

def main():
    results=[]
    with tempfile.TemporaryDirectory(prefix='env-source-fixture-') as d:
        temp=Path(d); (temp/'client.c').write_text(CLIENT); (temp/'sentinel').write_text('host-only')
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-pthread',str(temp/'client.c'),'-o',str(temp/'client')],check=True)
        script=temp/'startup'; script.write_text(f"{temp/'client'} {temp/'sentinel'}\nr=$?\nprintf 'AFTER shell=%s status=%s\\n' \"$$\" \"$r\"\n")
        results.append(scenario(temp,'foreground_client_exit',script,'client'))
        results.append(scenario(temp,'ENV_devnull',Path('/dev/null'),'absent'))
        results.append(scenario(temp,'missing_startup_is_not_client_success',temp/'missing','absent'))
        results.append(scenario(temp,'privileged_flag_skips_ENV',script,'absent',True))
        script.write_text("printf 'COMPILED_OVERRIDE\\n'\n")
        subprocess.run([ZSH,'-fc','zcompile "$1"','fixture',str(script)],check=True)
        script.write_text(f"{temp/'client'} {temp/'sentinel'}\nr=$?\nprintf 'AFTER shell=%s status=%s\\n' \"$$\" \"$r\"\n")
        os.utime(script,(1,1))
        results.append(scenario(temp,'adjacent_zwc_must_be_absent',script,'compiled'))
    print(json.dumps({'scope':'Linux same-version zsh host fixture, no Darwin/network/hardware proof',
        'host_zsh':ZSH,'host_zsh_sha256':sha(ZSH),'tests':results,'test_count':len(results),
        'PTY_input_bytes_injected':0,'client_threads':1,'deliberately_open_client_FDs':24,
        'Darwin_wait_patch_executed':False},indent=2))
if __name__=='__main__': main()
