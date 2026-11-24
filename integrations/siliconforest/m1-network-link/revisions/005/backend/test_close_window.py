"""Actual old/new backends+production004helper+Ccodec+QEMU, synthetic guest.

Controlled serial-credit pacing delays final CLOSE drain; each individual ACK
remains prompt. No source edits, devices, SSH, target or external traffic.
"""
from pathlib import Path
from collections import deque
import argparse
import hashlib
import importlib.util
import json
import os
import subprocess
import sys
import time
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[5]
TRANSPORT=ROOT/'integrations/puredarwin/m1-network-transport'
HELPER=HERE.parents[1]/'004/host'
spec=importlib.util.spec_from_file_location('frozen004_bridge_test',HELPER/'test_bridge_backend.py')
oldtest=importlib.util.module_from_spec(spec);spec.loader.exec_module(oldtest)


class CloseIntent(oldtest.CloseIntent):
    def recv(self,_n):
        deadline=time.monotonic()+18
        while not self.trial.bridge.transport_complete:
            assert time.monotonic()<deadline,'bounded delayed close completion'
            self.trial.pump();time.sleep(.002)
        return b''


class DelayedTrial(oldtest.BridgeTrial):
    def __init__(self,library,directory,backend,delay):
        self.delay=delay;self.close_seen=None;self.tail_chunks=self.tail_issued=0
        self.close_finished=None;self.interval=0;self.last_issue=None
        self.nonce=os.urandom(24).hex();self.records=directory/'records'
        self.stdout=open(directory/'process.log','wb')
        self.proc=subprocess.Popen([sys.executable,'-I',str(backend),
            '--nonce',self.nonce,'--codec-library',str(library),'--records',str(self.records),
            '--listen-port','0','--tcp-echo-port','0','--udp-echo-port','0',
            '--seconds','60','--accept-seconds','8'],stdin=subprocess.DEVNULL,
            stdout=self.stdout,stderr=subprocess.STDOUT)
        self.sock=self.codec=self.ready=None
        deadline=time.monotonic()+5
        while time.monotonic()<deadline and self.proc.poll() is None:
            try:
                self.ready=json.loads((self.records/'ready.json').read_text());break
            except(FileNotFoundError,json.JSONDecodeError):time.sleep(.01)
        if self.ready is None:
            self.cleanup();raise AssertionError('backend readiness missing')
        self.codec=oldtest.Trial.__init__.__globals__['Codec'](library,self.nonce,1)
        self.pending=deque()

    def connect(self):
        super().connect();self.sock=CloseIntent(self)

    def pump(self):
        b=self.bridge;chunk=b.tick();assert not b.error,b.error
        now=time.monotonic()
        if b.host.closed and self.close_seen is None:
            self.close_seen=now
            # Keep the production helper unchanged. Only this synthetic serial
            # consumer's scheduling stretches the remaining encoded CLOSE drain.
            self.tail_chunks=sum((len(item[0])-(b.pending_offset if i==0 else 0)+191)//192
                                 for i,item in enumerate(b.pending))
            assert self.tail_chunks>1
            self.interval=self.delay/self.tail_chunks
        eligible=(self.close_seen is None or
                  now>=self.close_seen+(self.tail_issued+1)*self.interval)
        if chunk and eligible:
            assert not self.tty and 1<=len(chunk)<=192
            b.authorize(chunk);self.actual_chunks.append(chunk);self.tty.extend(chunk)
            self.tty_high_water=max(self.tty_high_water,len(self.tty))
            if self.close_seen is not None:
                self.tail_issued+=1;self.last_issue=now
        if self.tty:
            data=bytes(self.tty[:117]);del self.tty[:len(data)];decoded=bytearray()
            for byte in data:
                if byte==32:
                    self.line_used+=1;assert self.line_used<=2
                else:
                    assert byte==10 and self.line_used in (1,2)
                    self.value=(self.value<<1)|(self.line_used-1);self.line_used=0;self.bit_count+=1
                    if self.bit_count==8:
                        decoded.append(self.value);self.value=self.bit_count=0
            self.codec.feed(bytes(decoded),lambda *args:self.pending.append(args))
            self.console_bytes+=len(data)
            b.feed_console(f'DL26_LINK_ACK_{self.nonce} bytes={self.console_bytes}\r\r\n'.encode())
            assert b.error is None,b.error
        if self.close_seen is not None and b.host.closed and b.data_bytes==b.acked_bytes and not b.pending:
            self.close_finished=time.monotonic()

    def receive(self):
        deadline=time.monotonic()+18
        while not self.pending:
            assert time.monotonic()<deadline,'delayed frame receive deadline'
            self.pump();time.sleep(.002)
        return self.pending.popleft()


def case(library,directory,backend,delay,success):
    directory.mkdir(mode=0o700)
    t=DelayedTrial(library,directory,backend,delay);caught=None;backend_result=None
    try:
        try:
            backend_result=oldtest.roundtrip(t)
        except BaseException as error:
            caught=repr(error)
        rc=t.proc.wait(timeout=5)
        actual=json.loads((t.records/'result.json').read_text())
        assert actual['cleanup']['qemu_reaped'] and actual['cleanup']['qemu_returncode']==0
        assert not actual['cleanup']['errors'] and not actual['cleanup'].get('kill_sent')
        assert actual['echoes']=={'tcp':True,'udp':True}
        if success:
            assert caught is None,caught
            assert rc==0 and actual['complete'] and backend_result['complete']
            assert t.bridge.transport_complete and t.bridge.acked_bytes==t.bridge.data_bytes
            assert actual['close_eof_seconds']==15 and actual['active_idle_seconds']==10
            assert actual['active_seconds']==60
            assert t.close_finished-t.close_seen>=delay
        else:
            assert caught is not None and rc==1 and not actual['complete']
            assert 'closed peer EOF' in actual['first_error'],actual['first_error']
            assert not t.bridge.transport_complete
        report={'backend_path':str(backend),'backend_sha256':hashlib.sha256(backend.read_bytes()).hexdigest(),
            'intended_close_drain_seconds':delay,'expected_success':success,'actual_backend_rc':rc,
            'caught_fixture_error':caught,'backend':actual,'bridge':t.bridge.report(),
            'tail_chunks':t.tail_chunks,'tail_chunks_issued':t.tail_issued,
            'actual_close_drain_seconds':t.close_finished-t.close_seen if t.close_finished else None,
            'simulated_tty_high_water':t.tty_high_water,
            'scope':'Actual Linux controlled backend/helper/Ccodec/QEMU with synthetic guest IP/ACK only'}
        (directory/'case-result.json').write_text(json.dumps(report,indent=2)+'\n')
        return report
    finally:t.cleanup()


def run(library,output):
    output.mkdir(mode=0o700)
    old=case(library,output/'old2-delay5',TRANSPORT/'host/backend.py',5,False)
    new=case(library,output/'new15-delay5',HERE/'backend.py',5,True)
    idle=case(library,output/'new15-delay11',HERE/'backend.py',11,True)
    return {'status':'PASS_005_CLOSE_WINDOW_REGRESSION_HOST_ONLY',
        'production004helper_sha256':hashlib.sha256((HELPER/'line_bridge.py').read_bytes()).hexdigest(),
        'cases':[old,new,idle],
        'not_claimed':['Darwin console/utun clean shutdown','M1/M5','External networking','Hardware recovery']}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--library',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
    a=p.parse_args();r=run(a.library.resolve(),a.output.resolve())
    (a.output/'regression.json').write_text(json.dumps(r,indent=2)+'\n')
    print(json.dumps({'status':r['status'],'cases':[{'expected_success':x['expected_success'],
        'delay':x['intended_close_drain_seconds'],'actual_backend_rc':x['actual_backend_rc'],
        'actual_close_drain_seconds':x['actual_close_drain_seconds'],
        'first_error':x['backend']['first_error'],'qemu_rc':x['backend']['cleanup']['qemu_returncode']}
        for x in r['cases']]},indent=2))
