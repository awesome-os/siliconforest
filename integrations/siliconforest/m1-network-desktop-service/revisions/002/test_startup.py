"""Host-only real SH-mode zsh PTY test of the fixed ENV script.
Two literal path substitutions confine the fixture; no target or network I/O.
"""
from pathlib import Path
import errno,hashlib,json,os,pty,re,select,signal,subprocess,tempfile,time
HERE=Path(__file__).resolve().parent
ZSH=Path('/nix/store/mxc65l88fllgzrf0gkh54nr9r96mki9y-zsh-5.9.1/bin/zsh')
NONCE='0123456789abcdef'*3
CLIENT=r'''
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc,char **argv){
 if(argc!=7)return 98;
 char path[1024];if(snprintf(path,sizeof(path),"%s/network-client.pid",argv[4])<0)return 99;
 FILE *f=fopen(path,"wx");if(!f)return 97;
 if(getenv("BAD_PID"))fputs("0077\n",f);else fprintf(f,"%ld\n",(long)getpid());
 if(fclose(f))return 96;
 printf("FIXTURE_CLIENT pid=%ld ppid=%ld sid=%ld pgid=%ld fg=%ld\n",(long)getpid(),(long)getppid(),(long)getsid(0),(long)getpgrp(),(long)tcgetpgrp(0));
 return getenv("FAIL_CLIENT")?7:0;
}
'''
def scenario(base,script,name,*,status=0,badpid=False,slot='0001',seconds='60',valid=True):
    instance=slot+NONCE[:44]
    directory=base/('session-'+NONCE)/instance
    directory.mkdir(parents=True,mode=0o700)
    child,master=pty.fork()
    if child==0:
        os.umask(0o077)
        for fd in range(3,1024):
            try:os.close(fd)
            except OSError:pass
        env={'TERM':'dumb','LC_ALL':'C','PATH':'/usr/bin:/bin','HOME':str(base),'ENV':str(script),
             'ZDOTDIR':str(base/'absent'),'HISTFILE':'/dev/null','PS1':'$ ','PS2':'> ',
             'DNDS_SESSION':NONCE,'DNDS_INSTANCE':instance,'DNDS_RECORDS':str(directory),'DNDS_SECONDS':seconds}
        if status:env['FAIL_CLIENT']='1'
        if badpid:env['BAD_PID']='1'
        os.execve(ZSH,['sh','-i','+m'],env)
    raw=bytearray();deadline=time.monotonic()+5
    try:
        while time.monotonic()<deadline:
            ready,_,_=select.select([master],[],[],.05)
            if ready:
                try:chunk=os.read(master,4096)
                except OSError as e:
                    if e.errno==errno.EIO:break
                    raise
                if not chunk:break
                raw.extend(chunk)
            if b'$ \x1b[?2004h' in raw:break
        assert b'$ \x1b[?2004h' in raw,(name,raw)
        normalized=bytes(raw).replace(b'\r',b'')
        path=directory/'network-shell-return.txt'
        if not valid:
            assert b'FIXTURE_CLIENT' not in raw and b'DL30_SHELL_RETURN' not in raw and not path.exists(),raw
            return {'name':name,'passed':True,'client_not_run':True,'prompt_alone_not_network_success':True}
        match=re.search(rb'FIXTURE_CLIENT pid=(\d+) ppid=(\d+) sid=(\d+) pgid=(\d+) fg=(\d+)\n',normalized)
        assert match,raw
        pid,ppid,sid,pgid,fg=map(int,match.groups())
        assert pid!=child and ppid==sid==pgid==fg==child,raw
        expected=(f'DL30_SHELL_RETURN session={NONCE} instance={instance} shell_pid={child} '
                  f'client_pid={0 if badpid else pid} status={status}\n').encode()
        assert path.read_bytes()==expected and normalized.count(expected)==1,raw
        assert normalized.index(match[0])<normalized.index(expected)<normalized.index(b'$ \x1b[?2004h'),raw
        assert not Path(f'/proc/{pid}').exists(),'client not reaped before shell prompt'
        assert path.stat().st_mode&0o7777==0o600
        return {'name':name,'passed':True,'normal_foreground_child_return_before_prompt':True,
                'source_bound_shell_return_file_equals_actual_PTY':True,'actual_child_reaped_before_prompt':True,
                'observed_client_exit_status':status,'invalid_pid_refused':badpid}
    finally:
        os.killpg(child,signal.SIGKILL);os.waitpid(child,0);os.close(master)
def main():
    assert ZSH.is_file()
    source=(HERE/'startup.env').read_text()
    results=[]
    with tempfile.TemporaryDirectory(prefix='dnds-env-test-') as temp:
        root=Path(temp);c=root/'client.c';c.write_text(CLIENT);exe=root/'client'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
        for index,options in enumerate(({},dict(status=7),dict(badpid=True),dict(slot='0003',valid=False),dict(seconds='61',valid=False))):
            base=root/str(index);base.mkdir();script=base/'startup.env'
            text=source.replace('/usr/bin/darwin-desktop-net-client',str(exe)).replace('/private/var/run/darwin-desktop-network/session-',str(base)+'/session-')
            assert source.count('/usr/bin/darwin-desktop-net-client')==1
            assert source.count('/private/var/run/darwin-desktop-network/session-')==1
            script.write_text(text);results.append(scenario(base,script,'case'+str(index),**options))
    print(json.dumps({'status':'PASS','scope':'Linux same-version SH-emulation fixture, no Darwin/network/hardware execution',
                      'production_script_sha256':hashlib.sha256(source.encode()).hexdigest(),
                      'literal_path_substitutions_only':2,'PTY_input_injected':0,'Darwin_wait_patch_executed':False,
                      'tests':results},indent=2))
if __name__=='__main__':main()
