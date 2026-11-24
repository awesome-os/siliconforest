"""Actual production004 Bridge+Ccodec+LinuxQEMU; synthetic guest ACK/IP only."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import sys
import time
sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[5]
sys.path.insert(0,str(HERE))
from line_bridge import Bridge
sys.path.insert(0,str(ROOT/'integrations/puredarwin/m1-network-transport/host'))
from test_backend import Trial,roundtrip


class CloseIntent:
    def __init__(self,trial): self.trial=trial
    def shutdown(self,_how): self.trial.close_intent=True
    def recv(self,_n):
        deadline=time.monotonic()+4
        while not self.trial.bridge.transport_complete:
            assert time.monotonic()<deadline,'bridge transport completion deadline'
            self.trial.pump();time.sleep(.002)
        return b''
    def close(self): self.trial.bridge.cancel()


class BridgeTrial(Trial):
    def connect(self):
        self.bridge=Bridge(self.nonce,self.ready['listen_port'])
        self.sock=CloseIntent(self);self.close_intent=False
        self.tty=bytearray();self.tty_high_water=0;self.console_bytes=0
        self.line_used=self.bit_count=self.value=0
        self.actual_chunks=[]
        self.bridge.feed_console(f'DL26_LINK_READY_{self.nonce} interface=utun0 mtu=1500\r\r\n'.encode())
    def send(self,kind,data=b''):
        raw=self.codec.encode(kind,data)
        line=f'DL26_TX_{self.nonce} {raw.hex()}\r\r\n'.encode()
        for at in range(0,len(line),17): self.bridge.feed_console(line[at:at+17])
        self.pump()
    def pump(self):
        chunk=self.bridge.tick();assert not self.bridge.error,self.bridge.error
        if chunk:
            assert not self.tty,'credit must mean previous tty bytes were read'
            assert 1<=len(chunk)<=192 and len(self.tty)+len(chunk)<=1024
            self.bridge.authorize(chunk)
            self.actual_chunks.append(chunk);self.tty.extend(chunk)
            self.tty_high_water=max(self.tty_high_water,len(self.tty))
        if self.tty:
            # Deliberately split every full chunk across reads/ACKs, with state
            # retained across incomplete bit-lines and bytes. C codec validates
            # real decoded DLNET bytes before the synthetic guest ACK is emitted.
            data=bytes(self.tty[:117]);del self.tty[:len(data)]
            decoded=bytearray()
            for byte in data:
                if byte==32:
                    self.line_used+=1;assert self.line_used<=2
                else:
                    assert byte==10 and self.line_used in (1,2)
                    self.value=(self.value<<1)|(self.line_used-1)
                    self.line_used=0;self.bit_count+=1
                    if self.bit_count==8:
                        decoded.append(self.value);self.value=self.bit_count=0
            self.codec.feed(bytes(decoded),lambda *args:self.pending.append(args))
            self.console_bytes+=len(data)
            ack=f'DL26_LINK_ACK_{self.nonce} bytes={self.console_bytes}\r\r\n'.encode()
            for at in range(0,len(ack),13):self.bridge.feed_console(ack[at:at+13])
            assert not self.bridge.error,self.bridge.error
    def receive(self):
        deadline=time.monotonic()+4
        while not self.pending:
            assert time.monotonic()<deadline,'bridge receive deadline'
            self.pump();time.sleep(.002)
        return self.pending.popleft()


def run(library,output):
    output.mkdir(mode=0o700)
    trial=BridgeTrial(library,output,seconds=15,accept_seconds=8)
    try:
        result=roundtrip(trial)
        b=trial.bridge
        assert b.transport_complete and b.acked_bytes==b.data_bytes==trial.console_bytes
        assert not trial.tty and not trial.line_used and not trial.bit_count
        assert len(b.issued)==b.host.count and not b.pending and not b.pending_offset
        assert trial.tty_high_water<=192
        joined=b''.join(trial.actual_chunks);count=0
        for raw,entry in zip(trial.actual_chunks,b.chunk_ledger):
            assert len(raw)<=192
            for offset in range(len(raw)+1):assert all(c in (32,10) for c in raw[offset:])
            count+=len(raw)
            assert entry=={'length':len(raw),'sha256':hashlib.sha256(raw).hexdigest(),'cumulativebytes':count}
        assert count==b.data_bytes and len(b.ack_ledger)>len(b.chunk_ledger)
        marker=(f'DL26_LINK_RESULT_{trial.nonce} status=PASS tcp_echo=1 udp_echo=1 '
            f'frames_out={b.guest.count} frames_in={b.host.count} '
            f'packets_out={sum(x["type"]==2 for x in b.guest_frames)} '
            f'packets_in={sum(x["type"]==2 for x in b.issued)} '
            f'error=none errno=0 console_bytes_in={trial.console_bytes} partial_line=0 partial_bits=0\r\r\n').encode()
        b.feed_console(marker);assert b.error is None,b.error
        report={'status':'PASS_004_CREDIT_BRIDGE_C_CODEC_QEMU_HOST_ONLY_SYNTHETIC_GUEST',
            'bridge':b.report(),'backend':result,'simulated_tty_capacity':1024,
            'simulated_tty_high_water':trial.tty_high_water,
            'synthetic_guest_ACK_and_RESULT':True,
            'whitespace_stream_bytes':len(joined),'whitespace_stream_sha256':hashlib.sha256(joined).hexdigest(),
            'not_claimed':['Darwin console/utun runtime','M1/M5','External networking','Physical input']}
        (output/'qualification.json').write_text(json.dumps(report,indent=2)+'\n')
        (output/'ingress-whitespace.bin').write_bytes(joined)
        return report
    finally:trial.cleanup()


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--library',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
    a=p.parse_args();r=run(a.library.resolve(),a.output.resolve())
    print(json.dumps({'status':r['status'],'data_bytes':r['bridge']['data_bytes'],
        'acked_bytes':r['bridge']['acked_bytes'],'frames_to_guest':len(r['bridge']['host_frames_issued']),
        'chunks':len(r['bridge']['chunk_ledger']),'acks':len(r['bridge']['ack_ledger']),
        'tty_high_water':r['simulated_tty_high_water'],'QEMU_returncode':r['backend']['cleanup']['qemu_returncode']},indent=2))
