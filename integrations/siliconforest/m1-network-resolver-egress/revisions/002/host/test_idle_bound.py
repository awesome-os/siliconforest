"""Actual finite30s idle refusal after OPEN; no DNS/public IP packet sent."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import time
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parent))
from bindings import Codec


def run(library,output):
    output.mkdir(mode=0o700);nonce=os.urandom(24).hex();sock=codec=None
    log=open(output/'process.log','wb')
    proc=subprocess.Popen([sys.executable,'-I',str(Path(__file__).with_name('backend.py')),
        '--profile','native-resolver-example-http-v1','--nonce',nonce,'--codec-library',str(library),
        '--codec-sha256',hashlib.sha256(library.read_bytes()).hexdigest(),'--records',str(output/'backend'),
        '--listen-port','0','--accept-seconds','8','--seconds','90'],stdin=subprocess.DEVNULL,
        stdout=log,stderr=subprocess.STDOUT)
    try:
        ready=None;until=time.monotonic()+5
        while time.monotonic()<until and proc.poll() is None:
            try:ready=json.loads((output/'backend/ready.json').read_text());break
            except(FileNotFoundError,json.JSONDecodeError):time.sleep(.01)
        assert ready is not None
        codec=Codec(library,nonce,1)
        sock=socket.create_connection(('127.0.0.1',ready['listen_port']),timeout=2);sock.settimeout(2)
        sock.sendall(codec.encode(1));frames=[]
        while not frames:codec.feed(sock.recv(4096),lambda *x:frames.append(x))
        assert frames[0][:2]==(1,1)
        started=time.monotonic();rc=proc.wait(timeout=34);elapsed=time.monotonic()-started
        actual=json.loads((output/'backend/result.json').read_text())
        assert rc==1 and not actual['complete'] and 'idle channel' in actual['first_error']
        assert actual['active_idle_seconds']==30 and actual['close_eof_seconds']==15 and actual['active_seconds']==90
        assert 29.5<=elapsed<34
        assert actual['cleanup']['qemu_reaped'] and actual['cleanup']['qemu_returncode']==0
        assert not actual['cleanup'].get('kill_sent') and not actual['cleanup']['errors']
        raw=(output/'backend/ethernet-tx.bin').read_bytes();count=0
        while raw:
            size=struct.unpack_from('!I',raw)[0];frame=raw[4:4+size];raw=raw[4+size:]
            assert frame[12:14]==b'\x08\x06';count+=1
        assert count==2
        report={'status':'PASS_ACTUAL_30S_IDLE_BOUND_NO_PUBLIC_TRAFFIC','elapsed_idle_seconds':elapsed,
            'actual_backend_rc':rc,'backend':actual,'only_two_initial_ARPs':True,'target_execution':False}
        (output/'idle-result.json').write_text(json.dumps(report,indent=2)+'\n');return report
    finally:
        if sock:sock.close()
        if codec:codec.close()
        if proc.poll() is None:
            proc.terminate()
            try:proc.wait(4)
            except subprocess.TimeoutExpired:
                proc.kill();proc.wait(2);raise AssertionError('forced backendKILL')
        log.close()


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--library',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
    a=p.parse_args();r=run(a.library.resolve(),a.output.resolve())
    print(json.dumps({'status':r['status'],'elapsed_idle_seconds':r['elapsed_idle_seconds']},indent=2))
