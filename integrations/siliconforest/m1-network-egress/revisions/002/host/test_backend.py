#!/usr/bin/env python3
"""Real local QEMU lifecycle/egress-policy refusals; no public DNS/HTTP sent."""
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
from test_public import tcp,udp
from policy import GUEST,DNS,QUESTION


def run(library,output):
    output.mkdir(mode=0o700)
    reports=[]
    for case in ("tcp_before_dns","wrong_dns_question","close_without_egress","early_eof","accept_timeout","term"):
        folder=output/case;folder.mkdir(mode=0o700)
        nonce=os.urandom(24).hex();sock=codec=None
        log=os.fdopen(os.open(folder/"process.log",os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_CLOEXEC,0o600),"wb")
        proc=subprocess.Popen([sys.executable,"-I",str(Path(__file__).with_name("backend.py")),
            "--profile","example-http-ack-v2","--nonce",nonce,"--records",str(folder/"backend"),
            "--codec-library",str(library),"--codec-sha256",hashlib.sha256(library.read_bytes()).hexdigest(),
            "--listen-port","0","--seconds","8","--accept-seconds","1" if case=="accept_timeout" else "8"],
            stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
        try:
            ready=None;deadline=time.monotonic()+5
            while time.monotonic()<deadline and proc.poll() is None:
                try:
                    ready=json.loads((folder/"backend/ready.json").read_text());break
                except(FileNotFoundError,json.JSONDecodeError):time.sleep(.01)
            assert ready is not None,(folder/"process.log").read_text()
            if case=="term":proc.terminate()
            elif case!="accept_timeout":
                codec=Codec(library,nonce,1)
                sock=socket.create_connection(("127.0.0.1",ready["listen_port"]),timeout=2)
                if case=="early_eof":sock.shutdown(socket.SHUT_WR)
                else:
                    sock.sendall(codec.encode(1));sock.settimeout(2)
                    opened=[]
                    while not opened:codec.feed(sock.recv(4096),lambda *x:opened.append(x))
                    assert opened[0][0]==1
                    if case=="tcp_before_dns":
                        data=tcp(GUEST,socket.inet_aton("1.1.1.1"),43002,80,1,0,2)
                        sock.sendall(codec.encode(2,data))
                    elif case=="wrong_dns_question":
                        data=struct.pack("!6H",1,0x0100,1,0,0,0)+QUESTION.replace(b"example",b"invalid")
                        sock.sendall(codec.encode(2,udp(GUEST,DNS,43001,53,data)))
                    elif case=="close_without_egress":
                        sock.sendall(codec.encode(3));sock.shutdown(socket.SHUT_WR)
            assert proc.wait(timeout=5)==1,(folder/"process.log").read_text()
            result=json.loads((folder/"backend/result.json").read_text())
            assert not result["complete"] and result["first_error"]
            assert result["cleanup"]["qemu_reaped"] and result["cleanup"]["qemu_returncode"]==0
            assert not result["cleanup"].get("kill_sent") and not result["cleanup"]["errors"]
            expected={"tcp_before_dns":"before DNS","wrong_dns_question":"fixed DNS A question",
                "close_without_egress":"without correlated","early_eof":"truncated",
                "accept_timeout":"accept lifetime","term":"signal15"}[case]
            assert expected in result["first_error"],result["first_error"]
            # Only the initial two ARP requests may reach QEMU: negative test
            # data must never become external traffic or a host listener flow.
            raw=(folder/"backend/ethernet-tx.bin").read_bytes();count=0
            while raw:
                n=struct.unpack_from("!I",raw)[0];frame=raw[4:4+n];raw=raw[4+n:]
                assert len(frame)==n and frame[12:14]==b"\x08\x06";count+=1
            assert count==2
            reports.append({"case":case,"result":result,"only_two_arp_frames_sent":True})
        finally:
            if sock:sock.close()
            if codec:codec.close()
            if proc.poll() is None:
                proc.terminate()
                try:proc.wait(4)
                except subprocess.TimeoutExpired:
                    proc.kill();proc.wait(2);raise AssertionError("forced backend KILL")
            log.close()
    return {"status":"PASS_REAL_LINUX_QEMU_REFUSAL_AND_LIFECYCLE_ONLY","cases":reports,
            "public_packets_sent":False,"target_execution":False}


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--library",required=True,type=Path);p.add_argument("--output",required=True,type=Path)
    a=p.parse_args();print(json.dumps(run(a.library.resolve(),a.output.resolve()),indent=2))
