"""004 serial-credit simulation/unit tests. No Darwin/SSH/device access."""
from pathlib import Path
import hashlib
import socket
import struct
import subprocess
import sys
import unittest
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from line_bridge import (Bridge, Frames, HEADER, crc32c, whitespace, MAX_CHUNK_BYTES,
                         MAX_CHUNKS, MAX_ACKS, MAX_DATA_BYTES)
NONCE = '1234567890abcdef' * 3


def frame(kind, seq, payload=b'', direction=1, nonce=NONCE):
    raw = HEADER.pack(b'DLNET001', kind, direction, 0, len(payload), 56,
                      seq, bytes.fromhex(nonce), 0, 0) + payload
    return raw[:48] + struct.pack('!I', crc32c(raw)) + raw[52:]


def packet():
    raw = bytearray.fromhex('4500001400010000401100000a00020f0a000202')
    total = sum(int.from_bytes(raw[i:i+2], 'big') for i in range(0, 20, 2))
    while total >> 16:
        total = (total & 65535) + (total >> 16)
    raw[10:12] = ((~total)&65535).to_bytes(2, 'big')
    return bytes(raw)


class FakeSocket:
    def __init__(self):
        self.closed = self.shut = False
        self.input = []
        self.written = bytearray()
    def setblocking(self, value): assert value is False
    def set_inheritable(self, value): assert value is False
    def connect_ex(self, peer): self.peer = peer; return 0
    def send(self, data): self.written.extend(data); return len(data)
    def recv(self, _n, flags=0):
        if self.input: return self.input[0][:_n] if flags else self.input.pop(0)
        raise BlockingIOError()
    def shutdown(self, how):
        assert how == socket.SHUT_WR
        self.shut = True
    def close(self): self.closed = True


class FakeTTY:
    """Bounded1024-byte tty input; fake consumer ACKs only parsed reads."""
    def __init__(self, bridge, capacity=1024):
        self.bridge, self.capacity = bridge, capacity
        self.queued = bytearray()
        self.high_water = self.total = self.bit_count = self.value = 0
        self.line = bytearray()
        self.frames = Frames(NONCE, 2)
        self.received_frames = []
    def write(self, data):
        if len(self.queued) + len(data) > self.capacity:
            raise OverflowError('simulated tty1024 overflow')
        self.queued.extend(data)
        self.high_water = max(self.high_water, len(self.queued))
    def read_ack(self, maximum=192, split_marker=False):
        data = bytes(self.queued[:maximum]); del self.queued[:len(data)]
        assert data
        decoded = bytearray()
        for byte in data:
            if byte == 32:
                self.line.append(byte)
                assert len(self.line) <= 2
            elif byte == 10:
                assert len(self.line) in (1,2)
                self.value = (self.value << 1) | (len(self.line)-1)
                self.bit_count += 1; self.line.clear()
                if self.bit_count == 8:
                    decoded.append(self.value); self.bit_count = self.value = 0
            else:
                raise ValueError('fake guest alphabet')
        self.received_frames.extend(self.frames.feed(bytes(decoded)))
        self.total += len(data)
        marker = f'DL29_ACK_{NONCE} bytes={self.total}\r\r\n'.encode()
        if split_marker:
            for at in range(0,len(marker),3): self.bridge.feed_console(marker[at:at+3])
        else: self.bridge.feed_console(marker)
        return data


class Tests(unittest.TestCase):
    def bridge(self):
        sock, clock = FakeSocket(), [0.0]
        b = Bridge(NONCE,20000,clock=lambda:clock[0],socket_factory=lambda *_:sock)
        return b,sock,clock
    def ready(self,b):
        b.feed_console(f'kernel unrelated\nDL29_READY_{NONCE} interface=utun0 mtu=1500\r\r\n'.encode())
    def tx(self,b,raw): b.feed_console(f'DL29_TX_{NONCE} {raw.hex()}\r\r\n'.encode())
    def ack(self,b,n,nonce=NONCE): b.feed_console(f'DL29_ACK_{nonce} bytes={n}\r\r\n'.encode())
    def opened(self):
        b,s,t = self.bridge();self.ready(b);self.tx(b,frame(1,1))
        s.input.append(frame(1,1,direction=2))
        return b,s,t
    def drain(self,b,tty,t,read_size=192):
        for _ in range(4096):
            wire = b.tick();self.assertIsNone(b.error)
            if wire:
                self.assertLessEqual(len(wire),192)
                b.authorize(wire);tty.write(wire)
                self.assertIsNone(b.tick(), 'must wait for ACK')
            if tty.queued:
                tty.read_ack(read_size,split_marker=True)
                self.assertIsNone(b.error)
            t[0] += .001
            if not b.pending and b.outstanding_since is None: return
        self.fail('bounded simulated drain')

    def test_credit_chunks_avoid_tty1024_overflow(self):
        b,s,t = self.opened();tty = FakeTTY(b)
        raw = frame(1,1,direction=2)
        self.assertGreater(len(whitespace(raw)),1024)
        with self.assertRaises(OverflowError):tty.write(whitespace(raw))
        self.drain(b,tty,t)
        self.assertEqual(tty.high_water,192)
        self.assertEqual(tty.total,b.data_bytes)
        self.assertEqual(b.acked_bytes,b.data_bytes)
        self.assertEqual(len(b.issued),1)
        self.assertEqual(tty.received_frames[0][2],raw)
        self.assertEqual(b.report()['version'],4)

    def test_partial_reads_ack_but_do_not_grant_next_chunk(self):
        b,s,t = self.opened();tty=FakeTTY(b)
        wire=b.tick();b.authorize(wire);tty.write(wire)
        tty.read_ack(17)
        self.assertEqual(b.acked_bytes,17);self.assertEqual(b.data_bytes,192)
        self.assertIsNone(b.tick());self.assertEqual(len(tty.queued),175)
        self.assertEqual(b.outstanding_since,0)
        self.drain(b,tty,t,read_size=17)
        self.assertEqual(tty.frames.count,1)

    def test_retains_original_whole_frame_receipt_until_last_chunk(self):
        b,s,t=self.opened();tty=FakeTTY(b);wire=b.tick()
        encoded,receipt=b.pending[0];b.authorize(wire);tty.write(wire)
        self.assertEqual(b.pending[0],(encoded,receipt));self.assertEqual(b.pending_offset,192)
        self.assertEqual(b.issued,[])
        self.drain(b,tty,t)
        self.assertEqual(b.issued,[receipt]);self.assertEqual(b.pending_offset,0)
        self.assertEqual(receipt['wire_sha256'],hashlib.sha256(encoded).hexdigest())

    def test_chunk_ledger_lengths_hashes_and_cumulative_totals(self):
        b,s,t=self.opened();tty=FakeTTY(b);self.drain(b,tty,t)
        encoded=whitespace(frame(1,1,direction=2));total=0
        for item in b.chunk_ledger:
            self.assertLessEqual(item['length'],192)
            block=encoded[total:total+item['length']];total+=len(block)
            self.assertEqual(item,{'length':len(block),'sha256':hashlib.sha256(block).hexdigest(),'cumulativebytes':total})
        self.assertEqual(total,len(encoded));self.assertEqual(b.ack_ledger[-1],{'cumulativebytes':total})

    def test_exact_current_chunk_only_no_replay_or_early_credit(self):
        b,s,t=self.opened();wire=b.tick()
        for bad in (wire[:-1],wire+b' ',b'/bin/sh\n',bytearray(wire),b''):
            with self.assertRaises(ValueError):b.authorize(bad)
        b.authorize(wire)
        with self.assertRaises(ValueError):b.authorize(wire)
        self.ack(b,b.data_bytes)
        next_chunk=b.tick();self.assertTrue(next_chunk)
        # Prior bytes can coincidentally equal next chunk; authorization follows
        # exact current bytes/credit, so use a definite mismatch for refusal.
        with self.assertRaises(ValueError):b.authorize(next_chunk+b' ')

    def test_ack_before_issue_wrong_nonce_above_issued_and_duplicate(self):
        for mode in ('before','wrong','above','duplicate','regress','zero','leadingzero'):
            b,s,t=self.opened()
            if mode!='before':b.authorize(b.tick())
            if mode=='before':self.ack(b,1)
            elif mode=='wrong':self.ack(b,192,'a'*48)
            elif mode=='above':self.ack(b,193)
            elif mode=='duplicate':self.ack(b,17);self.ack(b,17)
            elif mode=='regress':self.ack(b,17);self.ack(b,16)
            elif mode=='zero':self.ack(b,0)
            else:b.feed_console(f'DL29_ACK_{NONCE} bytes=00192\n'.encode())
            self.assertIsNotNone(b.error,mode);self.assertTrue(b.cancelled,mode)
            self.assertIsNone(b.tick());self.assertEqual(b.pending_offset,0)

    def test_ack_timeout_is_fixed_on_partial_progress(self):
        b,s,t=self.opened();b.authorize(b.tick());t[0]=2.9;self.ack(b,17)
        self.assertEqual(b.outstanding_since,0)
        t[0]=3.001;self.assertIsNone(b.tick());self.assertIn('ACK deadline',b.error)
        b,s,t=self.opened();b.authorize(b.tick());t[0]=3.001;self.ack(b,192)
        self.assertIn('ACK deadline',b.error);self.assertEqual(b.acked_bytes,0)

    def test_ack_and_chunk_and_byte_caps(self):
        b,s,t=self.opened();b.authorize(b.tick());b.ack_ledger=[{}]*MAX_ACKS
        self.ack(b,1);self.assertIn('ledger cap',b.error)
        b,s,t=self.opened();wire=b.tick();b.chunk_ledger=[{}]*MAX_CHUNKS
        with self.assertRaises(ValueError):b.authorize(wire)
        b,s,t=self.opened();wire=b.tick();b.data_bytes=b.acked_bytes=MAX_DATA_BYTES
        with self.assertRaises(ValueError):b.authorize(wire)

    def test_close_waits_for_final_credit_then_shutdown_and_eof(self):
        b,s,t=self.opened();tty=FakeTTY(b)
        self.tx(b,frame(2,2,packet()))
        s.input += [frame(2,2,packet(),direction=2)]
        self.drain(b,tty,t)
        self.assertIsNone(b.error)
        self.tx(b,frame(3,3));s.input.append(frame(3,3,direction=2))
        final=False
        for _ in range(100):
            wire=b.tick();self.assertIsNone(b.error)
            if wire:
                b.authorize(wire);tty.write(wire)
                if not b.pending:
                    final=True;self.assertFalse(s.shut);self.assertFalse(b.report()['credit_complete'])
                    self.assertIsNone(b.tick());self.assertFalse(s.shut)
                tty.read_ack()
            t[0]+=.001
            if final:break
        self.assertTrue(final);b.tick();self.assertTrue(s.shut)
        s.input.append(b'');b.tick();self.assertTrue(b.transport_complete)
        self.assertTrue(b.report()['credit_complete']);self.assertEqual(len(b.issued),3)
        self.assertEqual(bytes(s.written),frame(1,1)+frame(2,2,packet())+frame(3,3))
        b.feed_console(self.result(b))
        self.assertIsNone(b.error);self.assertEqual(b.guest_result['console_bytes_in'],b.data_bytes)

    def result(self,b,**changes):
        values={'status':'PASS','resolver_ok':1,'http_ok':1,'frames_out':3,'frames_in':3,
            'packets_out':1,'packets_in':1,'error':'none','errno':0,
            'console_bytes_in':b.data_bytes,'partial_line':0,'partial_bits':0,'daemon_cleanup':1,'probe_pid':15,'mdns_pid':14,'resolver_addresses':2,'selected':1746147226,'cleanup_complete':1,'phases':1,'http_bytes':931,'if_error':0,'if_stage':7,'default_add_attempted':1,'route_error':0,'route_stage':5}
        values.update(changes)
        return (f'DL29_RESULT_{NONCE} '+' '.join(f'{k}={v}' for k,v in values.items())+'\r\r\n').encode()

    def test_boundary_and_foreign_experiment_markers_refused(self):
        for prefix in ('DL29_DNS_BOUNDARY_', 'DL27_READY_', 'DL26_READY_', 'DL29_UNKNOWN_'):
            b,s,t=self.bridge();self.ready(b)
            b.feed_console((prefix+NONCE+' frames=2 bytes=1\n').encode())
            self.assertIsNotNone(b.error,prefix)

    def test_diagnostics_are_bounded_raw_only_not_success(self):
        b,s,t=self.bridge()
        for total,offset,data in ((348,0,'ab'*64),(348,320,'00'*28),(0,0,'')):
            b.feed_console(f'DL29_OBSERVE_{NONCE} kind=baseline_ipv4_route total={total} offset={offset} hex={data}\r\r\n'.encode())
            self.assertIsNone(b.error)
        self.assertFalse(b.ready);self.assertIsNone(b.guest_result)
        for suffix in ('total=4097 offset=0 hex=','total=1 offset=1 hex=00','total=1 offset=0 hex=0','total=1 offset=0 hex=gg'):
            b,s,t=self.bridge()
            b.feed_console(f'DL29_OBSERVE_{NONCE} kind=default_add_request {suffix}\n'.encode())
            self.assertIsNotNone(b.error,suffix)

    def test_result_requires_both_close_and_actual_frame_counts(self):
        b,s,t=self.bridge();self.ready(b)
        b.feed_console(self.result(b))
        self.assertIn('CLOSE/frame accounting',b.error)

    def test_result_can_precede_relay_eof_but_report_waits_for_eof(self):
        b,s,t=self.opened();tty=FakeTTY(b);self.drain(b,tty,t)
        self.tx(b,frame(3,2));s.input.append(frame(3,2,direction=2))
        self.drain(b,tty,t)
        b.feed_console(self.result(b,frames_out=2,frames_in=2,packets_out=0,packets_in=0))
        self.assertIsNone(b.error);self.assertIsNotNone(b.guest_result)
        self.assertFalse(b.report()['transport_complete'])
        b.tick();self.assertTrue(s.shut)
        s.input.append(b'');b.tick()
        self.assertTrue(b.report()['transport_complete'])

    def wrapper(self,kind,nonce=NONCE):
        suffix={'BEGIN':' profile=native-resolver-example-http-v1 backend=native-resolver-example-http-v1','STATUS':' rc=0','RETURN':'','END':''}[kind]
        return f'DL29_{kind}_{nonce}{suffix}\r\r\n'.encode()

    def test_whole_wrapper_BEGIN_READY_OPEN_in_one_chunk(self):
        b,s,t=self.bridge()
        raw=frame(1,1)
        b.feed_console(self.wrapper('BEGIN')+f'DL29_READY_{NONCE} interface=utun0 mtu=1500\r\r\nDL29_TX_{NONCE} {raw.hex()}\r\r\n'.encode())
        self.assertIsNone(b.error);self.assertTrue(b.ready);self.assertEqual(b.guest.count,1)
        self.assertIsNone(b.guest_result);self.assertFalse(b.report()['transport_complete'])

    def test_RESULT_STATUS_RETURN_END_same_chunk_before_relay_EOF(self):
        b,s,t=self.opened();tty=FakeTTY(b);self.drain(b,tty,t)
        self.tx(b,frame(3,2));s.input.append(frame(3,2,direction=2));self.drain(b,tty,t)
        chunk=(self.result(b,frames_out=2,frames_in=2,packets_out=0,packets_in=0)+
               self.wrapper('STATUS')+self.wrapper('RETURN')+b'Darwin\r\narm64\r\n'+self.wrapper('END'))
        b.feed_console(chunk)
        self.assertIsNone(b.error);self.assertEqual(b.guest_result['status'],'PASS')
        self.assertFalse(b.report()['transport_complete'])
        b.tick();self.assertTrue(s.shut);s.input.append(b'');b.tick()
        self.assertTrue(b.report()['transport_complete'])

    def test_known_wrappers_are_only_diagnostics_even_STATUS_failure(self):
        b,s,t=self.bridge()
        b.feed_console(self.wrapper('BEGIN')+self.wrapper('STATUS').replace(b'rc=0',b'rc=1')+
                       self.wrapper('RETURN')+self.wrapper('END'))
        self.assertIsNone(b.error);self.assertFalse(b.ready);self.assertIsNone(b.socket)
        self.assertIsNone(b.guest_result);self.assertFalse(b.report()['transport_complete'])
        self.assertEqual((b.data_bytes,b.acked_bytes,len(b.guest_frames)),(0,0,0))

    def test_wrapper_wrong_nonce_profile_backend_rc_syntax_and_legacy_refused(self):
        bads=[self.wrapper(kind,'f'*48) for kind in ('BEGIN','STATUS','RETURN','END')]
        bads += [self.wrapper('BEGIN').replace(b'native-resolver-example-http-v1',b'wrong-profile'),
                 self.wrapper('BEGIN').replace(b'backend=native-resolver-example-http-v1',b'backend=other'),
                 self.wrapper('STATUS').replace(b'rc=0',b'rc=2'),
                 self.wrapper('STATUS').replace(b'rc=0',b'rc=01'),
                 self.wrapper('END').replace(b'\r\r\n',b' extra=1\n'),
                 self.wrapper('BEGIN').replace(b'DL29_',b'DL27_'),
                 self.wrapper('RETURN').replace(b'DL29_',b'DL26_'),
                 f'DL29_UNKNOWN_{NONCE}\n'.encode()]
        for raw in bads:
            b,s,t=self.bridge();b.feed_console(raw)
            self.assertIsNotNone(b.error,raw);self.assertFalse(b.ready);self.assertIsNone(b.socket)

    def test_result_strict_new_fields_and_credit_consistency(self):
        for changes in ({'console_bytes_in':131073},{'partial_line':3},{'partial_bits':8},
                        {'partial_line':1},{'partial_bits':1},{'errno':1},{'status':'FAILED'},
                        {'console_bytes_in':1},{'cleanup_complete':0},{'phases':2},{'http_bytes':2049},{'if_error':1},{'if_stage':5},{'default_add_attempted':0},{'route_error':1},{'route_stage':4}):
            b,s,t=self.bridge();self.ready(b);b.feed_console(self.result(b,**changes))
            self.assertIsNotNone(b.error,changes)
        b,s,t=self.opened();wire=b.tick();b.authorize(wire)
        b.feed_console(self.result(b));self.assertIn('credit',b.error)
        for suffix in ('',' extra=1',' console_bytes_in=0 partial_line=0'):
            b,s,t=self.bridge();self.ready(b)
            old=f'DL29_RESULT_{NONCE} status=PASS resolver_ok=1 http_ok=1 frames_out=1 frames_in=1 packets_out=0 packets_in=0 error=none errno=0'
            b.feed_console((old+suffix+'\n').encode());self.assertIsNotNone(b.error)

    def test_premature_eof_with_unacked_chunk_is_not_complete(self):
        b,s,t=self.opened();b.authorize(b.tick());s.input.append(b'');b.tick()
        self.assertIsNotNone(b.error);self.assertFalse(b.transport_complete)

    def test_trailing_CR_and_interior_CR_policy_preserved(self):
        for count in range(4):
            b,s,t=self.bridge()
            b.feed_console(f'DL29_READY_{NONCE} interface=utun0 mtu=1500'.encode()+b'\r'*count+b'\n')
            self.assertTrue(b.ready);self.assertIsNone(b.error)
        b,s,t=self.opened();b.authorize(b.tick())
        b.feed_console(f'DL29_ACK_{NONCE} bytes=19\r2\r\r\n'.encode());self.assertIsNotNone(b.error)

    def test_H1_error_is_sticky_even_after_valid_credit(self):
        b,s,t=self.opened();b.authorize(b.tick());self.ack(b,193)
        before=(b.data_bytes,len(b.chunk_ledger));self.ack(b,192)
        b.feed_console(b'raw diagnostic tail\n');self.assertIsNone(b.tick())
        self.assertEqual(before,(b.data_bytes,len(b.chunk_ledger)));self.assertFalse(b.report()['transport_complete'])

    def test_line_caps_ready_mutations_and_whole_deadlines(self):
        for raw in (b'x'*4097,b'x'*4097+b'\n',f'DL29_READY_{"a"*48} interface=utun0 mtu=1500\n'.encode()):
            b,s,t=self.bridge();b.feed_console(raw);self.assertIsNotNone(b.error)
        b,s,t=self.bridge();t[0]=106;b.tick();self.assertIn('readiness',b.error)
        b,s,t=self.bridge();self.ready(b);t[0]=76;b.tick();self.assertIn('whole link',b.error)

    def test_every_chunk_suffix_remains_only_shell_whitespace(self):
        encoded=whitespace(bytes(range(256)))
        for start in range(0,len(encoded),192):
            chunk=encoded[start:start+192]
            for offset in range(len(chunk)+1):
                self.assertTrue(all(c in (32,10) for c in chunk[offset:]))
        for offset in (0,1,2,17,191):
            p=subprocess.run(['/bin/sh'],input=encoded[offset:192],capture_output=True,timeout=2)
            self.assertEqual((p.returncode,p.stdout,p.stderr),(0,b'',b''))

    def test_new_resolver_RESULT_fields_and_numeric_canonicality(self):
        mutations=({'daemon_cleanup':0},{'probe_pid':0},{'probe_pid':1},{'probe_pid':2147483648},
            {'mdns_pid':1},{'mdns_pid':15},{'resolver_addresses':0},{'resolver_addresses':33},
            {'selected':0},{'selected':4294967296},{'resolver_ok':0},
            {'packets_out':99},{'packets_in':99})
        for values in mutations:
            b,s,t=self.bridge();self.ready(b);b.feed_console(self.result(b,**values))
            self.assertIsNotNone(b.error,values)
        for old,new in ((b'resolver_ok=1',b'dns_ok=1'),(b'probe_pid=15',b'probe_pid=015'),
                (b'selected=1746147226',b'selected=-1'),(b'mdns_pid=14',b'mdns_pid=+14')):
            b,s,t=self.bridge();self.ready(b);b.feed_console(self.result(b).replace(old,new))
            self.assertIsNotNone(b.error,(old,new))

    def test_result_packet_counts_use_actual_frames_not_only_claim(self):
        b,s,t=self.opened();tty=FakeTTY(b);self.drain(b,tty,t)
        self.tx(b,frame(3,2));s.input.append(frame(3,2,direction=2));self.drain(b,tty,t)
        b.feed_console(self.result(b,frames_out=2,frames_in=2,packets_out=1,packets_in=0))
        self.assertIn('CLOSE/frame accounting',b.error)

    def test_exact_daemon_diagnostic_caps_and_total_budget(self):
        for kind,total,offset,hexdata in (('mdns_child_stderr',65536,65535,'00'),
                ('probe_child_stderr',65536,65535,'00'),('mdns_child_stdout',8192,8191,'00'),
                ('probe_child_stdout',8192,8191,'00')):
            b,s,t=self.bridge()
            b.feed_console(f'DL29_OBSERVE_{NONCE} kind={kind} total={total} offset={offset} hex={hexdata}\n'.encode())
            self.assertIsNone(b.error,(kind,total))
            self.assertEqual(b.report()['diagnostic_bytes'],1)
        for kind,total in (('mdns_child_stderr',65537),('probe_child_stdout',8193),('unknown',4097)):
            b,s,t=self.bridge()
            b.feed_console(f'DL29_OBSERVE_{NONCE} kind={kind} total={total} offset=0 hex=00\n'.encode())
            self.assertIsNotNone(b.error,(kind,total))
        b,s,t=self.bridge();b.observation_bytes=512*1024
        b.feed_console(f'DL29_OBSERVE_{NONCE} kind=baseline total=1 offset=0 hex=00\n'.encode())
        self.assertIn('aggregate cap',b.error)
        b,s,t=self.bridge();b.observation_lines=8192
        b.feed_console(f'DL29_OBSERVE_{NONCE} kind=baseline total=0 offset=0 hex=\n'.encode())
        self.assertIn('aggregate cap',b.error)

    def test_DL28_namespace_never_accepted(self):
        for tag in ('READY','TX','ACK','RESULT','OBSERVE','BEGIN','STATUS','RETURN','END'):
            b,s,t=self.bridge()
            b.feed_console(f'DL28_{tag}_{NONCE}\n'.encode())
            self.assertIsNotNone(b.error,tag)

    def test_result_key_order_matches_root_C_serializer(self):
        import re
        root=Path(__file__).resolve().parents[4]
        text=(root/'integrations/puredarwin/m1-network-resolver-service/guest/net_resolver.c').read_text()
        fmt=re.search(r'"\\nDL29_RESULT_%s ([^"\n]+)"',text).group(1)
        keys=re.findall(r'([a-z_]+)=%',fmt)
        b,s,t=self.bridge()
        unit_keys=[p.split(b'=')[0].decode() for p in self.result(b).split()[1:]]
        self.assertEqual(keys,unit_keys)


if __name__=='__main__':unittest.main(verbosity=2)
