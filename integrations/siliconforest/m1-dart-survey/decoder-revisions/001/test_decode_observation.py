import hashlib
import struct
import unittest
import decode_observation as d

N = "0123456789abcdef" * 3

def raw(success=True):
    b = bytearray(764)
    struct.pack_into("<6I", b, 0, 1, 88 if success else 0, 889 if success else 1,
                     5 if success else 0, 0 if success else 0x1b8, 0 if success else 1)
    if success:
        struct.pack_into("<4I", b, 24, 255, 255, 255, 255)
        struct.pack_into("<5I", b, 40, 15, 1, 1, 1, 1)
        for i, off in enumerate(d.OFFSETS):
            struct.pack_into("<2I", b, 60 + 8*i, off, 14 << 24 if i == 0 else 0)
    return bytes(b)

def record(b, key=d.KEY):
    lines = [f"DL13_DART_RECORD_BEGIN {N} {key} bytes={len(b)} chunk_bytes=64"]
    for i, at in enumerate(range(0, len(b), 64)):
        lines.append(f"DL13_DART_RECORD_HEX {N} {key} index={i} {b[at:at+64].hex()}")
    lines.append(f"DL13_DART_RECORD_END {N} {key} bytes={len(b)} chunks=12 sha256={hashlib.sha256(b).hexdigest()}")
    return "\n".join(lines)

def transcript(success=True, instance=True):
    b = raw(success)
    text = record(b) + "\n"
    if instance:
        text += record(b,d.INSTANCE) + "\n"
    s=d.observation(b)
    text += (f"DL13_DART_VERDICT {N} verdict={'SAMPLED' if success else 'REFUSED'} "
             f"result={s['result']} samples={s['sample_count']} reads={s['reads']} "
             f"region={s['failure_region']} offset={s['failure_offset']} "
             f"instance={'equal' if instance else 'absent'} initialization_proven=false\n")
    return text.encode()

class Decoder(unittest.TestCase):
    def test_complete_and_refusal_raw_records(self):
        for success in [True,False]:
            for instance in [True,False]:
                r=d.decode(transcript(success,instance),N)
                self.assertEqual(r['records'][d.KEY],raw(success))
                self.assertEqual(r['summary']['result'],0 if success else 1)
    def test_integrity_order_count_nonce_and_scope(self):
        base=transcript()
        cases=[base.replace(b'index=1 ',b'index=2 ',1),
               base.replace(b'index=1 ',b'index=01 ',1),
               base.replace(b'sha256=',b'sha256=0',1),
               base.replace(b'bytes=764',b'bytes=763',1),
               base.replace(N.encode(),b'f'*48,1),
               base.replace(b'initialization_proven=false',b'initialization_proven=true'),
               base+base,
               base.replace(b'verdict=SAMPLED',b'verdict=REFUSED'),
               base.replace(b'reads=889',b'reads=888'),
               base.replace(b'instance=equal',b'instance=absent')]
        for i,b in enumerate(cases):
            with self.subTest(i=i),self.assertRaises(ValueError):d.decode(b,N)
    def test_truncated_and_missing(self):
        for b in [b'',transcript()[:200],transcript().split(b'DL13_DART_RECORD_END')[0]]:
            with self.assertRaises(ValueError):d.decode(b,N)
    def test_instance_mismatch_and_missing_error(self):
        b=raw();other=raw(False)
        text=transcript().decode()
        wrong=text.replace(record(b,d.INSTANCE),record(other,d.INSTANCE))
        with self.assertRaises(ValueError):d.decode(wrong.encode(),N)
        with self.assertRaises(ValueError):
            d.decode((f"DL13_DART_COPY {N} {d.KEY} status=no_property detail=1\n").encode(),N)
    def test_snapshot_constraints(self):
        for off,val in [(0,2),(4,89),(8,1025),(12,6),(20,3),(60,4),(24,0),(40,0)]:
            b=bytearray(raw());struct.pack_into("<I",b,off,val)
            with self.subTest(off=off,val=val),self.assertRaises(ValueError):d.observation(bytes(b))
    def test_refusal_does_not_require_coherent_unread_guard_fields(self):
        b=bytearray(raw(False));struct.pack_into("<I",b,40,0xdeadbeef)
        self.assertEqual(d.observation(b)['result'],1)
        # A legitimate unsupported first sample and an early board refusal.
        b=bytearray(764);struct.pack_into("<6I",b,0,1,1,10,4,0,2)
        self.assertEqual(d.observation(b)['result'],2)
        b=bytearray(764);struct.pack_into("<6I",b,0,1,0,0,5,0,0xfffffffe)
        self.assertEqual(d.observation(b)['result'],-2)

if __name__ == '__main__':unittest.main()
