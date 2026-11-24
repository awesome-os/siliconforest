"""Host-only tests; all physical-looking values are synthetic metadata."""
from dataclasses import replace
import random
import struct
import unittest

import facts_wire as w


def fixture():
    identity = w.Expected(bytes(range(1, 25)), tuple(bytes([i]) * 32 for i in range(1, 7)))
    dram = w.Range(w.DRAM_BASE, 0x400000000)
    envelope = w.Range(w.DRAM_BASE + 0x20000000, 0x10000000)
    boot = w.Range(envelope.base + 0x100000, 0x40000)
    words = [0x100000, 0x100007, 1, 0, 0, 0, 0x100100, 0x100103, 2, 0, 0, 0]
    mcc = struct.pack("<96I", *(words * 8))
    records = (
        w.Record(1, 0, 0, 0, w.Range(w.DRAM_BASE | (words[0] << 12), 0x8000)),
        w.Record(1, 2, 0, 0, w.Range(w.DRAM_BASE | (words[6] << 12), 0x4000)),
        w.Record(2, 0, 0x40000000, 0x12345678, w.Range(w.DRAM_BASE + 0x300100000, 4096)),
        w.Record(3, 0, 0, 0, w.Range(w.DRAM_BASE + 0x300000000, 0x200000)),
        w.Record(4, 0, 0, 0, w.Range(0x690000000, 0x1000)),
        w.Record(5, 0, 0, 0, w.Range(envelope.base + 0x80000, 0x4000)),
        w.Record(6, 0, 0, 0, boot),
    )
    sources = tuple(w.Source(tag, 2, sum(r.tag == tag for r in records), bytes([tag + 16]) * 32)
                    for tag in range(1, 7))
    return w.Snapshot(identity, dram, envelope, boot, (envelope,), sources, mcc, records)


def with_records(facts, records):
    records = tuple(records)
    sources = tuple(replace(s, count=sum(r.tag == s.tag for r in records),
                            status=2 if any(r.tag == s.tag for r in records) else 1)
                    for s in facts.sources)
    return replace(facts, sources=sources, records=records)


class WireTests(unittest.TestCase):
    def test_roundtrip_and_declared_scope(self):
        f = fixture()
        raw = w.encode(f, f.identity)
        observed = w.decode(raw, f.identity)
        self.assertEqual(observed.facts, f)
        self.assertEqual(observed.status, "STRUCTURALLY_VALID_FACTS_NOT_POLICY")
        self.assertEqual(observed.exclusion_union, (f.retained_boot,))
        self.assertEqual([slot for slot, _ in observed.mcc_slots], [0, 2])
        self.assertEqual(len(raw), w.HEADER + w.RECORD_BYTES * len(f.records))
        # This digest is explicitly a declared label, not a recomputed raw-source hash.
        changed = bytearray(raw)
        changed[496 + 8] ^= 1
        self.assertNotEqual(w.decode(bytes(changed), f.identity).facts.sources, f.sources)

    def test_strict_framing_and_every_reserved_byte(self):
        f = fixture()
        raw = w.encode(f, f.identity)
        reserved = list(range(52, 64)) + list(range(280, 320)) + list(range(1168, 1280))
        reserved += list(range(384, 496))  # Seven unused original-RAM slots.
        for source in range(6):
            reserved += list(range(496 + source * 48 + 40, 496 + source * 48 + 48))
        for offset in reserved:
            changed = bytearray(raw)
            changed[offset] = 1
            with self.subTest(offset=offset), self.assertRaises(w.Refused):
                w.decode(bytes(changed), f.identity)
        for offset in range(8, 52, 4):
            changed = bytearray(raw)
            changed[offset] ^= 0x80
            with self.subTest(field=offset), self.assertRaises(w.Refused):
                w.decode(bytes(changed), f.identity)
        for bad in (raw[:-1], raw + b"\0", raw[:80], bytearray(raw), b"" * 0):
            with self.assertRaises(w.Refused):
                w.decode(bad, f.identity)
        for offset in list(range(64, 88)) + [88 + n * 32 for n in range(6)]:
            changed = bytearray(raw)
            changed[offset] ^= 0x80
            with self.assertRaises(w.Refused):
                w.decode(bytes(changed), f.identity)

    def test_source_count_order_and_real_capacity(self):
        f = fixture()
        for bad in (
            replace(f, sources=f.sources[:-1]),
            replace(f, sources=(replace(f.sources[0], status=0),) + f.sources[1:]),
            replace(f, sources=(replace(f.sources[0], count=1),) + f.sources[1:]),
            replace(f, sources=(replace(f.sources[0], declared_sha256=b"\0" * 32),) + f.sources[1:]),
            replace(f, records=f.records[::-1]),
            replace(f, records=f.records[:2] + (f.records[3], f.records[2]) + f.records[4:]),
            replace(f, records=(f.records[0], f.records[0]) + f.records[2:]),
            replace(f, records=(replace(f.records[0], flags=1),) + f.records[1:]),
            replace(f, identity=replace(f.identity, nonce=b"\0" * 24)),
            replace(f, records=list(f.records)),
            replace(f, dram=w.Range(True, 16384)),
        ):
            with self.assertRaises(w.Refused):
                w.encode(bad, f.identity)
        others = [r for r in f.records if r.tag != w.PMAP_IO]
        pmap = [w.Record(2, i, 0, i, w.Range(0x600000000 + i * 4096, 4096)) for i in range(250)]
        full = with_records(f, sorted(others + pmap, key=lambda r: (r.tag, r.ordinal)))
        self.assertEqual(len(full.records), 256)
        self.assertEqual(len(w.encode(full, f.identity)), w.MAX_BYTES)
        extra = w.Record(2, 250, 0, 0, w.Range(0x600100000, 4096))
        overflow = with_records(f, sorted(list(full.records) + [extra], key=lambda r: (r.tag, r.ordinal)))
        with self.assertRaises(w.Refused):
            w.encode(overflow, f.identity)
        empty = with_records(f, [r for r in f.records if r.tag != w.PMAP_IO])
        self.assertEqual(w.decode(w.encode(empty, f.identity), f.identity).facts.sources[1].status, 1)

    def test_mcc_full_bytes_and_slot_provenance(self):
        f = fixture()
        for offset in range(384):
            changed = bytearray(f.mcc_raw)
            changed[offset] ^= 1
            with self.assertRaises(w.Refused):
                w.encode(replace(f, mcc_raw=bytes(changed)), f.identity)
        with self.assertRaises(w.Refused):
            w.encode(replace(f, mcc_raw=f.mcc_raw[:80]), f.identity)
        altered = (replace(f.records[0], span=w.Range(f.records[0].span.base, 4096)),) + f.records[1:]
        with self.assertRaises(w.Refused):
            w.encode(replace(f, records=altered), f.identity)

    def test_original_boundaries_and_full_exclusion_bounds(self):
        f = fixture()
        split = f.envelope.base + 0x8000000
        ram = (w.Range(f.envelope.base, split - f.envelope.base),
               w.Range(split, f.envelope.base + f.envelope.bytes - split))
        adjacent = replace(f, ram=ram)
        self.assertEqual(w.decode(w.encode(adjacent, f.identity), f.identity).facts.ram, ram)
        crossing = w.Range(split - w.CPU_PAGE, w.CPU_PAGE * 2)
        records = tuple(replace(r, span=crossing) if r.tag == 6 else r for r in adjacent.records)
        with self.assertRaises(w.Refused):
            w.encode(replace(adjacent, retained_boot=crossing, records=records), f.identity)
        full = w.Range(f.envelope.base - 4096, 8192)
        records = tuple(replace(r, span=full) if r.tag == 2 else r for r in f.records)
        observed = w.decode(w.encode(replace(f, records=records), f.identity), f.identity)
        self.assertEqual(observed.exclusion_union[0], full)  # No inward clipping.
        for span in (w.Range(w.PA_LIMIT - 1, 2), w.Range(0, 0), w.Range(-1, 1)):
            records = tuple(replace(r, span=span) if r.tag == 2 else r for r in f.records)
            with self.assertRaises(w.Refused):
                w.encode(replace(f, records=records), f.identity)

    def test_union_eight_nine_and_source_only_remap(self):
        f = fixture()
        others = [r for r in f.records if r.tag != 2]
        for count in (7, 8):
            pmap = [w.Record(2, i, 0, 0, w.Range(f.envelope.base + i * 0x8000, 4096))
                    for i in range(count)]
            facts = with_records(f, sorted(others + pmap, key=lambda r: (r.tag, r.ordinal)))
            if count == 7:
                try:
                    observed = w.decode(w.encode(facts, f.identity), f.identity)
                except w.Refused as error:
                    self.fail("Valid exact-eight union was refused: " + str(error))
                self.assertEqual(len(observed.exclusion_union), 8)
            else:
                with self.assertRaises(w.Refused):
                    w.encode(facts, f.identity)
        # A remap that covers the whole envelope is retained but is not physical authority.
        records = tuple(replace(r, span=f.envelope) if r.tag == 5 else r for r in f.records)
        observed = w.decode(w.encode(replace(f, records=records), f.identity), f.identity)
        self.assertEqual(observed.exclusion_union, (f.retained_boot,))

    def test_seeded_interval_set_oracle(self):
        f = fixture()
        rng = random.Random(0x4378)
        others = [r for r in f.records if r.tag != 2]
        for _ in range(1024):
            cells = set()
            pmap = []
            for i in range(rng.randrange(8)):
                start, size = rng.randrange(24), rng.randrange(1, 8)
                cells.update(range(start, start + size))
                pmap.append(w.Record(2, i, 0, 0, w.Range(f.envelope.base + start * 4096, size * 4096)))
            facts = with_records(f, sorted(others + pmap, key=lambda r: (r.tag, r.ordinal)))
            observed = w.decode(w.encode(facts, f.identity), f.identity)
            seen = set()
            for span in observed.exclusion_union:
                if span != f.retained_boot:
                    seen.update(range((span.base - f.envelope.base) // 4096,
                                      (span.base + span.bytes - f.envelope.base) // 4096))
            self.assertEqual(seen, cells)


if __name__ == "__main__":
    unittest.main()
