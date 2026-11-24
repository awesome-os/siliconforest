#!/usr/bin/env python3
"""Offline differential tests against frozen backend; no network or devices."""
import argparse
import hashlib
import importlib.util
import ipaddress
import json
from pathlib import Path
import random
import struct
import subprocess
import sys
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent
BACKEND=HERE.parents[1]/"m1-network-egress"
EXPECTED="8d23b0071024511cf7773b807e54c4be0f50818829ba64e515ddb3562bd2e9f0"


def run(binary):
    receipt=BACKEND/"source-validation.json"
    assert hashlib.sha256(receipt.read_bytes()).hexdigest()==EXPECTED
    manifest=json.loads(receipt.read_text())
    for name,digest in manifest["files_sha256"].items():
        assert hashlib.sha256((BACKEND/name).read_bytes()).hexdigest()==digest
    spec=importlib.util.spec_from_file_location("frozen_egress_policy",BACKEND/"host/policy.py")
    policy=importlib.util.module_from_spec(spec);spec.loader.exec_module(policy)
    fixture=HERE/"validation/fixtures"
    q=(fixture/"dns-query.bin").read_bytes();reply=(fixture/"dns-reply.bin").read_bytes()
    http=(fixture/"http-response.bin").read_bytes()
    jobs=[];expected=[]

    def dns(data,query=q,source=b"\x0a\0\2\3",port=53):
        jobs.append("D "+(query.hex() or "-")+" "+source.hex()+" "+str(port)+" "+(data.hex() or "-"))
        try:
            assert len(query)==29 and query[2:]==q[2:] and source==b"\x0a\0\2\3" and port==53
            addresses,ttl=policy.dns_answer(data,query)
            expected.append(("D",True,len(addresses),ttl,addresses[0].hex()))
        except(Exception,AssertionError):expected.append(("D",False))

    def response(data,eof):
        jobs.append("H "+str(int(eof))+" "+(data.hex() or "-"))
        try:
            result=policy.http_response(data,eof)
            expected.append(("H",True,result,eof))
        except Exception:expected.append(("H",False))

    dns(reply)
    for n in range(len(reply)):dns(reply[:n])
    for i in range(len(reply)):
        for bit in range(8):
            changed=bytearray(reply);changed[i]^=1<<bit;dns(bytes(changed))
    dns(reply+b"\0")
    dns(reply,source=b"\x0a\0\2\2");dns(reply,port=54)
    for i in range(29):
        changed=bytearray(q);changed[i]^=1;dns(reply,query=bytes(changed))
    # Explicit private/shared/exception boundaries and zero/max TTL mutations.
    for ip in ("0.0.0.0","10.0.2.2","100.64.0.1","127.0.0.1","169.254.1.2",
               "172.16.0.1","192.0.0.8","192.0.0.9","192.0.0.10","192.0.2.1",
               "192.168.0.1","198.18.0.1","198.51.100.1","203.0.113.1","224.0.0.1","240.0.0.1"):
        changed=bytearray(reply);changed[41:45]=ipaddress.IPv4Address(ip).packed;dns(bytes(changed))
    for ttl in (0,1,37,59,60,61,0xffffffff):
        changed=bytearray(reply);changed[35:39]=struct.pack("!I",ttl);dns(bytes(changed))
    def rr(owner,kind,data,ttl=60):
        return owner+struct.pack("!HHIH",kind,1,ttl,len(data))+data
    def message(records):
        return q[:2]+struct.pack("!5H",0x8180,1,len(records),0,0)+q[12:]+b"".join(records)
    for steps in (7,8):
        owner=b"\xc0\x0c";records=[]
        for hop in range(steps):
            target=bytes([1,ord('a')+hop,0]);records.append(rr(owner,5,target));owner=target
        records.append(rr(owner,1,bytes([93,184,216,34])));dns(message(records))
    for length in (253,254):
        target=b"".join(bytes([size])+b'a'*size for size in (63,63,63,length-192))+b"\0"
        dns(message([rr(b"\xc0\x0c",5,target),rr(b"\xc0\x29",1,bytes([93,184,216,34]))]))
    # Real saved chunked reply, every prefix at both EOF states and each byte
    # mutation, plus both nonchunked forms and malformed framing/limits.
    for n in range(len(http)+1):response(http[:n],False);response(http[:n],True)
    for i in range(len(http)):
        changed=bytearray(http);changed[i]^=1;response(bytes(changed),True)
    head=b"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n"
    basic=head+b"Content-Length: 14\r\n\r\nExample Domain"
    for data in (basic,head+b"\r\nExample Domain",basic+b"x",
                 head+b"Content-Length: 999999\r\n\r\n",
                 head+b"Content-Length: 14\r\ncontent-length: 14\r\n\r\nExample Domain",
                 head+b"Transfer-Encoding: chunked\r\nContent-Length: 14\r\n\r\n",
                 head+b"Transfer-Encoding: chunked\r\n\r\nffffffff\r\n",
                 head+b"Transfer-Encoding: chunked\r\n\r\ne\r\nExample Domain\r\n0\r\nX: y\r\n\r\n",
                 b"x"*65537):
        response(data,False);response(data,True)
    process=subprocess.run([str(binary),"--oracle"],input="\n".join(jobs)+"\n",text=True,
                           capture_output=True,timeout=30)
    assert process.returncode==0,process.stderr
    actual=process.stdout.splitlines();assert len(actual)==len(expected)
    for i,(line,wanted) in enumerate(zip(actual,expected)):
        values=line.split();assert values[0]==wanted[0]
        status=int(values[1])
        if not wanted[1]:
            assert status>1,(i,wanted,line,jobs[i][:150])
            if wanted[0]=="D":assert values[2:]==["0","0","00000000"]
            else:assert all(int(x)==0 for x in values[2:])
        elif wanted[0]=="D":
            assert status==0 and int(values[2])==wanted[2] and int(values[3])==wanted[3] and values[4]==wanted[4],(i,wanted,line)
        else:
            result,eof=wanted[2:]
            if result is None:assert status==1 and all(int(x)==0 for x in values[2:]),(i,wanted,line)
            else:
                assert status==(0 if eof else 1) and int(values[3])==1 and int(values[4])==int(eof),(i,wanted,line)
                assert int(values[5])==result["header_bytes"] and int(values[6])==result["body_bytes"] and int(values[7])==1,(i,wanted,line)
    # Exact Python3.14 ipaddress semantics, including shared100.64/10 and the
    # two192.0.0/24 exceptions. Numeric table facts are fixed in original C.
    addresses=set()
    c=ipaddress.IPv4Address._constants
    networks=list(c._private_networks)+list(c._private_networks_exceptions)+[c._public_network,c._multicast_network]
    for network in networks:
        for v in (int(network.network_address),int(network.broadcast_address)):
            addresses.update(x for x in (v-1,v,v+1) if 0<=x<=0xffffffff)
    rng=random.Random(0x44534e53)
    addresses.update(rng.getrandbits(32) for _ in range(10000));addresses=sorted(addresses)
    process=subprocess.run([str(binary),"--classify"],input="".join(f"{x:08x}\n" for x in addresses),
                           text=True,capture_output=True,timeout=10)
    assert process.returncode==0,process.stderr
    output=process.stdout.splitlines();assert len(output)==len(addresses)
    for x,value in zip(addresses,output):
        assert int(value)==int(policy.global_address(x.to_bytes(4,"big"))),str(ipaddress.IPv4Address(x))
    return {"status":"PASS_OFFLINE_BACKEND_EQUIVALENCE_ONLY","dns_http_cases":len(jobs),
        "global_ipv4_cases":len(addresses),"backend_receipt_sha256":EXPECTED,
        "python_ipaddress_path":ipaddress.__file__,"python_ipaddress_sha256":hashlib.sha256(Path(ipaddress.__file__).read_bytes()).hexdigest(),
        "python_version":sys.version,"scope":"Saved host bytes and synthetic inputs; no network/Darwin/target execution"}


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("--binary",required=True,type=Path)
    a=p.parse_args();print(json.dumps(run(a.binary.resolve()),indent=2))
