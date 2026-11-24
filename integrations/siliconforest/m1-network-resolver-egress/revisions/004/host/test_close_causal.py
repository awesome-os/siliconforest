#!/usr/bin/env python3
"""New DL29 credit helper + frozen native-resolver backend, host-only.

Synthetic guest packets and ACKs, actual host networking. No default route or interface is simulated as evidence. No Darwin/M1, SSH or routes.
One selected answer, one public TCP flow, no endpoint retry/scan or redirect.
"""
import argparse
from collections import deque
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import time
import importlib.util
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
ROOT=Path(__file__).resolve().parents[6]
BACKENDHOST=Path(__file__).resolve().parent
sys.path.insert(0,str(BACKENDHOST))
from bindings import Codec
from policy import Policy, GUEST, DNS, QUESTION, checksum, parse_packet
PROFILE='';CLOSE_SECONDS=0;ACTIVE_SECONDS=0;CASE='';helper=None;HELPER=None;HELPER_SHA_BEFORE=None

def configure(case):
    global BACKENDHOST,PROFILE,CLOSE_SECONDS,ACTIVE_SECONDS,CASE,helper,HELPER,HELPER_SHA_BEFORE
    CASE=case
    if case=='old15':
        BACKENDHOST=ROOT/'integrations/puredarwin/m1-network-resolver-egress/revisions/003/host'
        PROFILE='native-resolver-example-http-v1';CLOSE_SECONDS=15;ACTIVE_SECONDS=90
        HELPER=ROOT/'integrations/puredarwin/m1-network-resolver-service/host/line_bridge.py'
        expected='bdb063da457fc7e22ace5d5abaf8a256d407b508b62ddf2f78988dadd72f4438'
    elif case=='new40':
        BACKENDHOST=Path(__file__).resolve().parent
        PROFILE='native-resolver-example-http-drain-v2';CLOSE_SECONDS=40;ACTIVE_SECONDS=120
        HELPER=ROOT/'integrations/puredarwin/m1-network-resolver-service/revisions/002/host/line_bridge.py'
        expected='1098fa52eb9e22793f3b0c161b02821c0241f71e199e9afd11aa1ac274be975a'
    else:raise ValueError('explicit causal case required')
    raw=HELPER.read_bytes();HELPER_SHA_BEFORE=hashlib.sha256(raw).hexdigest();assert HELPER_SHA_BEFORE==expected
    import types
    helper=types.ModuleType('bound_DL29_close_causal');exec(compile(raw,str(HELPER),'exec'),helper.__dict__)


class CreditSocket:
    """Synthetic guest raw console decoder, real production credit Bridge."""
    def __init__(self,library,nonce,port):
        self.nonce=nonce;self.bridge=helper.Bridge(nonce,port)
        self.bridge.feed_console(f'DL29_BEGIN_{nonce} profile={PROFILE} backend={PROFILE}\r\r\n'.encode())
        assert not self.bridge.ready and self.bridge.guest_result is None and self.bridge.socket is None
        self.bridge.feed_console(f'DL29_READY_{nonce} interface=utun0 mtu=1500\r\r\n'.encode())
        self.codec=Codec(library,nonce,1)
        self.tty=bytearray();self.decoded=bytearray();self.whitespace=bytearray()
        self.line_used=self.bit_count=self.value=self.consumed=self.high_water=0
        self.due=None;self.timeout=2;self.closed=False
        self.received_tcp_payloads=[];self.received_dns_frames=0;self.max_ip_bytes=0;self.latencies=[]
        self.issued_at=None;self.started=time.monotonic();self.on_pump=None;self.pace=.1;self.last_pre_tick_state=None
    def settimeout(self,n):self.timeout=n
    def _send_frame(self,raw):
        assert len(raw)==56+int.from_bytes(raw[12:14],'big')
        line=f'DL29_TX_{self.nonce} {raw.hex()}\r\r\n'.encode()
        for at in range(0,len(line),17):self.bridge.feed_console(line[at:at+17])
    def sendall(self,raw):
        self._send_frame(raw);self.pump()
    def frame(self,kind,seq,payload):
        if kind==2:
            self.max_ip_bytes=max(self.max_ip_bytes,len(payload))
            parsed=parse_packet(payload)
            if parsed[0]==6:self.received_tcp_payloads.append(len(parsed[6]))
            if parsed[0]==17:self.received_dns_frames+=1
    def pump(self):
        if self.on_pump is not None:self.on_pump()
        self.last_pre_tick_state={'at':time.monotonic(),'pending_frames':len(self.bridge.pending),'pending_offset':self.bridge.pending_offset,'issued_bytes':self.bridge.data_bytes,'ACKed_bytes':self.bridge.acked_bytes}
        chunk=self.bridge.tick()
        if self.bridge.error:raise RuntimeError(self.bridge.error)
        if chunk:
            assert not self.tty and 1<=len(chunk)<=192
            self.bridge.authorize(chunk);self.tty.extend(chunk);self.whitespace.extend(chunk)
            self.high_water=max(self.high_water,len(self.tty));self.issued_at=time.monotonic()
            self.due=self.issued_at+self.pace
        if self.tty and time.monotonic()>=self.due:
            raw=bytes(self.tty);self.tty.clear();decoded=bytearray()
            for byte in raw:
                if byte==32:
                    self.line_used+=1;assert self.line_used<=2
                else:
                    assert byte==10 and self.line_used in (1,2)
                    self.value=(self.value<<1)|(self.line_used-1);self.line_used=0;self.bit_count+=1
                    if self.bit_count==8:
                        decoded.append(self.value);self.value=self.bit_count=0
            self.codec.feed(bytes(decoded),self.frame)
            self.decoded.extend(decoded);self.consumed+=len(raw)
            self.latencies.append(time.monotonic()-self.issued_at)
            ack=f'DL29_ACK_{self.nonce} bytes={self.consumed}\r\r\n'.encode()
            self.bridge.feed_console(ack)
            if self.bridge.error:raise RuntimeError(self.bridge.error)
    def recv(self,n):
        until=time.monotonic()+self.timeout
        while not self.decoded:
            if self.bridge.transport_complete:return b''
            if time.monotonic()>=until:raise TimeoutError('synthetic console receive timeout')
            self.pump();time.sleep(.002)
        data=bytes(self.decoded[:n]);del self.decoded[:len(data)];return data
    def shutdown(self,how):
        assert how==socket.SHUT_WR  # production Bridge owns real half-close
    def close(self):
        if not self.closed:
            self.closed=True;self.bridge.cancel();self.codec.close()
    def report(self):
        return {'production_dl28_helper_sha256_before':HELPER_SHA_BEFORE,
            'production_dl28_helper_sha256_after':hashlib.sha256(HELPER.read_bytes()).hexdigest(),
            'bridge':self.bridge.report(),'simulated_tty_capacity':1024,'simulated_tty_high_water':self.high_water,
            'synthetic_guest_ACK':True,'synthetic_wrapper_BEGIN_before_READY':True,'initial_ack_pacing_seconds':.1,'closing_ack_pacing_seconds':self.pace,'last_pre_tick_state':self.last_pre_tick_state,
            'actual_ack_latency_max':max(self.latencies) if self.latencies else None,
            'observed_tcp_payload_max':max(self.received_tcp_payloads) if self.received_tcp_payloads else None,
            'mss_bound_observed':max(self.received_tcp_payloads)<=256 if self.received_tcp_payloads else None,
            'observed_max_ip_bytes':self.max_ip_bytes,'advertised_tcp_mss':256,
            'partial_line':self.line_used,'partial_bits':self.bit_count,
            'whole_elapsed_seconds':time.monotonic()-self.started,
            'whitespace_bytes':len(self.whitespace),'whitespace_sha256':hashlib.sha256(self.whitespace).hexdigest()}


def packet(src, dst, proto, body):
    header = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20+len(body), 1, 0x4000,
                         64, proto, 0, src, dst)
    return header[:10] + struct.pack("!H", checksum(header)) + header[12:] + body


def udp(src, dst, sport, dport, payload):
    body = struct.pack("!4H", sport, dport, len(payload)+8, 0) + payload
    pseudo = src + dst + struct.pack("!BBH", 0, 17, len(body))
    value = checksum(pseudo+body) or 65535
    body = body[:6] + struct.pack("!H", value) + body[8:]
    return packet(src, dst, 17, body)


def tcp(src, dst, sport, dport, seq, ack, flags, payload=b""):
    options=b'\x02\x04\x05\xb4' if flags&2 else b''
    body = struct.pack("!HHIIBBHHH", sport, dport, seq, ack, ((20+len(options))//4)<<4,
                       flags, 32768, 0, 0) + options + payload
    pseudo = src + dst + struct.pack("!BBH", 0, 6, len(body))
    body = body[:16] + struct.pack("!H", checksum(pseudo+body)) + body[18:]
    return packet(src, dst, 6, body)


def run(library, output):
    output.mkdir(mode=0o700)
    nonce, proc, codec, sock = os.urandom(24).hex(), None, None, None
    result = {"status":"FAIL_HOST_ONLY_PUBLIC_TEST", "nonce":nonce}
    log = os.fdopen(os.open(output/"process.log", os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_CLOEXEC, 0o600), "wb", buffering=0)
    records = output/"backend"
    try:
        proc = subprocess.Popen([sys.executable, "-I", str(BACKENDHOST/"backend.py"),
            "--profile", PROFILE, "--nonce", nonce,
            "--codec-library", str(library), "--codec-sha256", hashlib.sha256(library.read_bytes()).hexdigest(),
            "--records", str(records), "--listen-port", "0", "--accept-seconds", "8", "--seconds", str(ACTIVE_SECONDS)],
            stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 5
        ready = None
        while time.monotonic() < deadline and proc.poll() is None:
            try:
                ready = json.loads((records/"ready.json").read_text()); break
            except (FileNotFoundError, json.JSONDecodeError):
                time.sleep(0.01)
        if ready is None:
            raise RuntimeError("backend readiness missing")
        policy, codec, pending = Policy(nonce), Codec(library, nonce, 1), deque()
        sock = CreditSocket(library,nonce,ready['listen_port'])
        deadline = time.monotonic() + 105

        def send(kind, data=b""):
            if time.monotonic() >= deadline:
                raise TimeoutError("whole public fixture deadline")
            if kind == 2:
                policy.outgoing(data)
            sock.settimeout(min(2, max(0.01, deadline-time.monotonic())))
            sock.sendall(codec.encode(kind, data))

        def receive():
            while not pending:
                if time.monotonic() >= deadline:
                    raise TimeoutError("whole public fixture deadline")
                sock.settimeout(min(2, deadline-time.monotonic()))
                data = sock.recv(4096)
                if not data:
                    raise RuntimeError("backend EOF; inspect closed failure record")
                codec.feed(data, lambda *x:pending.append(x))
            kind, seq, data = pending.popleft()
            if kind == 2:
                policy.incoming(data)
                if policy.contiguous>2048:raise ValueError('synthetic client HTTP2048 cap')
            return kind, seq, data

        send(1); assert receive()[:2] == (1, 1)
        query = struct.pack('!H',1+int.from_bytes(os.urandom(2),'big')%65535) + struct.pack("!5H", 0x0100, 1, 0, 0, 0) + QUESTION
        dns = udp(GUEST, DNS, 43001, 53, query)
        retry={'sent':0,'first_sent':None,'events':[]}
        send(2,dns);retry['first_sent']=time.monotonic()
        def retry_once():
            if retry['sent']<2 and time.monotonic()-retry['first_sent']>=1.05*(retry['sent']+1):
                retry['sent']+=1;stamp=time.monotonic()
                observed={'retry':retry['sent'],'queued_at':stamp,
                    'gap_from_first_seconds':stamp-retry['first_sent'],
                    'first_dns_frame_complete':sock.received_dns_frames>0,
                    'issued_whitespace_bytes':sock.bridge.data_bytes,
                    'acked_whitespace_bytes':sock.bridge.acked_bytes,
                    'pending_frame_offset':sock.bridge.pending_offset,
                    'pending_frames':len(sock.bridge.pending)}
                retry['events'].append(observed)
                policy.outgoing(dns)
                sock._send_frame(codec.encode(2,dns))
        sock.on_pump=retry_once
        while len(policy.reply_events)<3:
            kind,seq,data=receive()
            assert kind==2 and parse_packet(data)[0]==17
        sock.on_pump=None
        assert retry['sent']==2 and policy.dns_attempts==3 and policy.dns_duplicates==2
        first_event=policy.reply_events[0]
        first_ttls=first_event['rr_ttls'];last_ttls=policy.reply_events[-1]['rr_ttls']
        first_groups=first_event['comparison_sorted_ttl_groups']
        last_groups=policy.reply_events[-1]['comparison_sorted_ttl_groups']
        assert any(y<x for a,b in zip(first_groups,last_groups) for x,y in zip(a,b)),('actual upstream TTL did not fall',first_ttls,last_ttls)
        assert all(e['comparison_identity_sha256']==first_event['comparison_identity_sha256'] for e in policy.reply_events)
        assert all(e['comparison_mode']=='a_only_answer_multiset' for e in policy.reply_events)
        assert all(e['original_lease_expires_elapsed_seconds']==first_event['original_lease_expires_elapsed_seconds'] for e in policy.reply_events)
        gaps=[retry['events'][0]['gap_from_first_seconds'],retry['events'][1]['queued_at']-retry['events'][0]['queued_at']]
        assert all(1.0<=gap<1.5 for gap in gaps)
        result['actual_query_retry_gaps_seconds']=gaps
        result['actual_first_reply_rr_ttls']=first_ttls
        result['actual_duplicate_reply_rr_ttls']=[e['rr_ttls'] for e in policy.reply_events[1:]]
        result['actual_answer_order_rotated']=any(e['answer_order_changed_from_first'] for e in policy.reply_events)
        result['actual_retry_observation']=retry
        result['first_retry_during_first_reply_credit']=not retry['events'][0]['first_dns_frame_complete']
        assert policy.allowed_addresses and policy.endpoint is None
        endpoint, sport, initial = policy.allowed_addresses[-1], 43002, 30000

        def send_tcp(sequence, ack, flags, body=b""):
            send(2, tcp(GUEST, endpoint, sport, 80, sequence & 0xffffffff,
                        ack & 0xffffffff, flags, body))

        for attempt in range(3):
            send_tcp(initial, 0, 0xc2)
            try:
                kind, seq, data = receive()
                assert kind == 2 and parse_packet(data)[0] == 6
                break
            except TimeoutError:
                if attempt == 2:
                    raise
        assert policy.server_isn is not None
        send_tcp(initial+1, policy.server_isn+1, 0x10)
        send_tcp(initial+1, policy.server_isn+1, 0x18, policy.request)
        request_retries = 0
        while not policy.complete():
            try:
                kind, seq, data = receive()
                assert kind == 2
                _proto, _src, _dst, _sp, _dp, body, payload = parse_packet(data)
                if payload or body[13] & 1:
                    ack = policy.server_isn + 1 + policy.contiguous
                    if policy.fin_offset == policy.contiguous:
                        ack += 1
                    send_tcp(initial+1+len(policy.request), ack, 0x10)
            except TimeoutError:
                request_retries += 1
                if request_retries > 2:
                    raise
                send_tcp(initial+1, policy.server_isn+1, 0x18, policy.request)
        final_ack = policy.server_isn + 2 + policy.contiguous
        send_tcp(initial+1+len(policy.request), final_ack, 0x11)
        # Application EOF is complete; the real owner does not wait for TCP
        # FINACK before queueing DLNET CLOSE. Preserve all queued duplicates.
        sock.pump()
        pending_wire=sum(len(wire) for wire,_receipt in sock.bridge.pending)-sock.bridge.pending_offset
        outstanding=sock.bridge.data_bytes-sock.bridge.acked_bytes
        late_large=[r for _wire,r in sock.bridge.pending if r['type']==2 and r['frame_bytes']>=1000]
        assert late_large, 'required actual queued TCP response retransmission not observed'
        # Deterministic congestion from actual queued bytes, not fabricated
        # packets. Reserve6000 wire bytes for actual FINACK/CLOSE tail.
        sock.pace=min(.4,max(.1,22*192/(pending_wire+outstanding+6000)))
        result['close_queue_observation']={'pending_wire_bytes':pending_wire,'outstanding_bytes':outstanding,
            'pending_frames':len(sock.bridge.pending),'late_large_frame_receipts':late_large,
            'closing_credit_pace_seconds':sock.pace,'planned_seconds_for_known_queue_plus6000':(pending_wire+outstanding+6000)/192*sock.pace}
        result['guest_CLOSE_queued_monotonic']=time.monotonic()
        send(3);sock.shutdown(socket.SHUT_WR)
        while True:
            kind,seq,data=receive()
            if kind==3:break
            assert kind==2 and parse_packet(data)[0]==6
            # No fabricated ACK or packet after guest CLOSE. Real duplicates
            # are parsed and credited byte-for-byte until host CLOSE.
        assert sock.recv(1)==b''
        result['target_credit_and_EOF_complete_monotonic']=time.monotonic()
        result['actual_close_drain_seconds']=result['target_credit_and_EOF_complete_monotonic']-result['guest_CLOSE_queued_monotonic']
        assert 15<result['actual_close_drain_seconds']<40
        codec.end()
        rc = proc.wait(5)
        backend = json.loads((records/"result.json").read_text())
        if rc != 0 or not backend["complete"]:
            raise RuntimeError("backend incomplete/failed")
        for name, binding in backend["closed_records"].items():
            data = (records/name).read_bytes()
            assert len(data) == binding["bytes"] and hashlib.sha256(data).hexdigest() == binding["sha256"]
        assert backend["queued_bytes_client_qemu"] == [0,0]
        assert backend['backend_policy_revision']==4 and backend['dns_policy_revision']==2
        assert backend['egress']['initial_tcp_flags']==0xc2
        assert backend['profile']==PROFILE and backend['close_eof_seconds']==CLOSE_SECONDS
        assert sock.bridge.transport_complete and sock.bridge.data_bytes==sock.bridge.acked_bytes==sock.consumed
        assert not sock.tty and not sock.line_used and not sock.bit_count
        assert backend['active_idle_seconds']==30 and backend['active_seconds']==ACTIVE_SECONDS
        assert 'DNS_phase_boundary' not in sock.bridge.report()
        assert sock.bridge.report()['public_profile']==PROFILE
        assert sock.bridge.report()['backend_profile']==PROFILE
        assert sock.bridge.guest_result is None  # no fake Darwin RESULT/cleanup
        assert backend["stream_bytes_written_client_qemu"] == [
            (records/"relay-tx.bin").stat().st_size,(records/"ethernet-tx.bin").stat().st_size]
        result.update(status="PASS_ACTUAL_QUEUED_TCP_DUPLICATE_CLOSE40_FULL_CREDIT_EOF", backend_returncode=rc,
                      egress=policy.summary(), client_fin_sent_no_FINACK_wait=True,
                      scope="Source-fact synthetic native-DNS bytes via new DL29 credit helper and real Linux slirp; no Darwin, mdnsd, libc getaddrinfo or route execution")
    except BaseException as error:
        result["error"] = repr(error)
    finally:
        if sock is not None:
            result['serial_credit']=sock.report()
            (output/'ingress-whitespace.bin').write_bytes(bytes(sock.whitespace))
            sock.close()
        if codec is not None:
            codec.close()
        if proc is not None:
            if proc.poll() is None:
                try:
                    proc.wait(2)
                except subprocess.TimeoutExpired:
                    proc.terminate()
                    try:
                        proc.wait(4)
                    except subprocess.TimeoutExpired:
                        proc.kill(); proc.wait(2); result["forced_backend_kill"] = True
            result["actual_backend_returncode"] = proc.returncode
        log.close()
        if (records/"result.json").exists():
            backend = json.loads((records/"result.json").read_text())
            result["closed_backend"] = backend
            if not backend["complete"] or backend["cleanup"]["errors"]:
                result["status"] = "FAIL_HOST_ONLY_PUBLIC_TEST"
        result['helper_sha256_before']=HELPER_SHA_BEFORE
        result['helper_sha256_after']=hashlib.sha256(HELPER.read_bytes()).hexdigest()
        result['synthetic_interface_cleanup_or_Darwin_RESULT_claimed']=False
        result['helper_public_profile_is_legacy_transport_label_only']=False
        result['backend_profile']=PROFILE
        result['causal_case']=CASE
        if result['helper_sha256_after']!=HELPER_SHA_BEFORE:
            result['status']='FAIL_HELPER_CHANGED_DURING_TEST'
        if CASE=='old15':
            actual=result.get('closed_backend',{})
            assert not actual.get('complete') and actual.get('egress',{}).get('complete'), actual
            assert 'closed peer EOF' in actual.get('first_error',''),actual.get('first_error')
            assert actual['guest_close'] and actual['host_close'] and not actual['peer_eof']
            assert result['serial_credit']['bridge']['acked_bytes']<result['serial_credit']['bridge']['data_bytes'] or not result['serial_credit']['bridge']['transport_complete']
            result['status']='PASS_CAUSAL_NEGATIVE_OLD15_FAILED_WITH_VALID_HTTP_AND_UNDRAINED_CREDIT'
            if 'guest_CLOSE_queued_monotonic' in result:
                result['failure_observed_after_guest_CLOSE_seconds']=time.monotonic()-result['guest_CLOSE_queued_monotonic']
        result["files_sha256"] = {str(p.relative_to(output)):hashlib.sha256(p.read_bytes()).hexdigest()
                                  for p in sorted(output.rglob('*')) if p.is_file()}
        with os.fdopen(os.open(output/"test-result.json", os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_CLOEXEC, 0o600),"w") as report:
            report.write(json.dumps(result,indent=2)+"\n");report.flush();os.fsync(report.fileno())
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case",required=True,choices=["old15","new40"])
    parser.add_argument("--library", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--allow-public-example-http", action="store_true", required=True)
    args = parser.parse_args()
    configure(args.case)
    result = run(args.library.resolve(), args.output.resolve())
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result["status"].startswith("PASS") else 1)
