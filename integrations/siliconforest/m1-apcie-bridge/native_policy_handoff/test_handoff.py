"""Synthetic host tests. No loader object, device or target writer is used."""
from dataclasses import replace
import hashlib
import importlib.util
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest

import handoff as h


def prop(key, value, template=False):
    raw = key.encode("ascii").ljust(32, b"\0") + struct.pack("<I", len(value) | (int(template) << 31))
    return raw + value + b"\0" * ((-len(value)) & 3)


def node(name, properties=(), children=()):
    properties = (prop("name", name.encode() + b"\0"),) + tuple(properties)
    return struct.pack("<II", len(properties), len(children)) + b"".join(properties) + b"".join(children)


def fixture(*, opaque_bytes=3, hazard=False, virt_base=0xfffffe0010000000,
            ba_offset=0x10000, descriptor_offset=0x20000, dram_size=None):
    w = h.facts
    dram = w.Range(w.DRAM_BASE, 0x400000000)
    envelope = w.Range(w.DRAM_BASE + 0x20000000, 0x10000000)
    boot = w.Range(envelope.base, 0x400000)
    allocation = h.Allocation(envelope.base + 0x20000, h.PAGE)
    mm = node("memory-map", (
        prop("DeviceTree", struct.pack("<QQ", envelope.base + descriptor_offset, allocation.bytes)),
        prop("BootArgs", struct.pack("<QQ", envelope.base + ba_offset, h.PAGE)),
        prop("unrelated", b"unchanged"),
    ))
    chosen = node("chosen", (
        prop("dram-base", struct.pack("<Q", dram.base)),
        prop("dram-size", struct.pack("<Q", dram.bytes if dram_size is None else dram_size)),
        prop("opaque", b"x" * opaque_bytes),
    ), (mm, node("other", (prop("unrelated", b"preserve"),))))
    base = node("device-tree", (prop("unrelated", b"outside chosen"),),
                (chosen, node("defaults", (prop("other", b"data"),))))
    records = []
    if hazard:
        records.append(w.Record(2, 0, 0, 0, w.Range(allocation.base, 4096)))
    records += [w.Record(3, 0, 0, 0, w.Range(w.DRAM_BASE + 0x300000000, 0x200000)),
                w.Record(4, 0, 0, 0, w.Range(0x690000000, 4096)),
                w.Record(5, 0, 0, 0, w.Range(envelope.base + 0x800000, h.PAGE)),
                w.Record(6, 0, 0, 0, boot)]
    sources = tuple(w.Source(tag, 2 if any(r.tag == tag for r in records) else 1,
                             sum(r.tag == tag for r in records), bytes([tag + 16]) * 32)
                    for tag in range(1, 7))
    nonce = bytes(range(1, 25))
    identity = w.Expected(nonce, (b"K" * 32, b"R" * 32, b"H" * 32,
                                   hashlib.sha256(base).digest(), b"O" * 32, b"F" * 32))
    facts = w.Snapshot(identity, dram, envelope, boot, (envelope,), sources, bytes(384), tuple(records))
    wire_bytes = w.HEADER + len(records) * w.RECORD_BYTES
    values = dict(revision=2, version=2, virt_base=virt_base, phys_base=envelope.base,
                  mem_size=envelope.bytes, top_of_kernel_data=boot.base + boot.bytes,
                  video=dict(base=0, display=0, stride=0, width=0, height=0, depth=0),
                  machine_type=0, devtree=(allocation.base - envelope.base + virt_base) & h.U64_MASK,
                  devtree_size=h.planned_size(base, wire_bytes), cmdline="cpus=1 serial=3",
                  boot_flags=1, mem_size_actual=dram.bytes)
    native = h.bootargs.encode(values, wire_profile=h.bootargs.WIRE_PROFILE)
    identity = replace(identity, identities=identity.identities[:-1] + (hashlib.sha256(native).digest(),))
    facts = replace(facts, identity=identity)
    wire = w.encode(facts, identity)
    return base, wire, identity, native, allocation


def updated_native(fixture_values, **changes):
    base, wire, identity, native, allocation = fixture_values
    values = h.bootargs.decode(native, wire_profile=h.bootargs.WIRE_PROFILE)
    values.update(changes)
    native = h.bootargs.encode(values, wire_profile=h.bootargs.WIRE_PROFILE)
    observation = h.facts.decode(wire, identity)
    identity = replace(identity, identities=identity.identities[:-1] + (hashlib.sha256(native).digest(),))
    wire = h.facts.encode(replace(observation.facts, identity=identity), identity)
    return base, wire, identity, native, allocation


class HandoffTests(unittest.TestCase):
    def test_exact_insertion_preserves_other_bytes_and_no_self_hash(self):
        args = fixture()
        base, wire, expected, native, allocation = args
        before = tuple(args)
        planned = h.plan(*args)
        self.assertEqual(args, before)
        self.assertEqual(h.canonical_base(planned.payload), base)
        parsed_before = h._parse(base)
        parsed_after = h._parse(planned.payload)
        self.assertEqual(set(parsed_before), set(parsed_after))
        for path, old in parsed_before.items():
            current = parsed_after[path]
            self.assertEqual([(p.key, p.value, p.template) for p in current.properties if p.key != h.PROPERTY],
                             [(p.key, p.value, p.template) for p in old.properties])
        self.assertEqual(parsed_after["/chosen"].get(h.PROPERTY).value, wire)
        self.assertEqual(planned.base_adt_sha256, expected.identities[3].hex())
        self.assertEqual(planned.final_bootargs_sha256, expected.identities[5].hex())
        self.assertNotEqual(planned.final_adt_sha256, planned.base_adt_sha256)
        self.assertFalse(planned.metadata()["target_write_performed"])
        self.assertEqual(planned.metadata()["status"], "PURE_HANDOFF_PLAN_NOT_RUNTIME_OR_POLICY")
        with self.assertRaisesRegex(h.Refused, "already present"):
            h.planned_size(planned.payload, len(wire))

    def test_hash_bindings_and_native_size_address_envelope(self):
        args = fixture()
        base, wire, expected, native, allocation = args
        for field in range(6):
            digests = list(expected.identities)
            digests[field] = bytes([field + 1]) * 32
            bad = replace(expected, identities=tuple(digests))
            with self.assertRaises(ValueError):
                h.plan(base, wire, bad, native, allocation)
        # Rebind both the wire and expected label, leaving actual source bytes unchanged.
        for field in (3, 5):
            digests = list(expected.identities)
            digests[field] = bytes([field + 1]) * 32
            bad = replace(expected, identities=tuple(digests))
            snapshot = h.facts.decode(wire, expected).facts
            rebound = h.facts.encode(replace(snapshot, identity=bad), bad)
            with self.assertRaisesRegex(h.Refused, "identity"):
                h.plan(base, rebound, bad, native, allocation)
        for changes in (dict(devtree_size=len(base)), dict(devtree=0),
                        dict(mem_size=0x8000000), dict(phys_base=expected.identities[0][0]),
                        dict(top_of_kernel_data=allocation.base + 1024),
                        dict(top_of_kernel_data=allocation.base + allocation.bytes + 0x1000000)):
            with self.subTest(changes=tuple(changes)), self.assertRaises(h.Refused):
                h.plan(*updated_native(args, **changes))
        snapshot = h.facts.decode(wire, expected).facts
        shifted = h.facts.Range(snapshot.retained_boot.base + h.PAGE,
                                snapshot.retained_boot.bytes - h.PAGE)
        records = tuple(replace(r, span=shifted) if r.tag == 6 else r for r in snapshot.records)
        altered = h.facts.encode(replace(snapshot, retained_boot=shifted, records=records), expected)
        with self.assertRaisesRegex(h.Refused, "retained boot mismatch"):
            h.plan(base, altered, expected, native, allocation)
        # The modulo translation also handles a synthetic wrapping virtual base.
        self.assertEqual(h.canonical_base(h.plan(*fixture(virt_base=(1 << 64) - 0x10000)).payload), base)

    def test_capacity_alias_and_known_exclusions(self):
        args = fixture()
        for allocation in (h.Allocation(args[4].base + 1, h.PAGE), h.Allocation(0, h.PAGE),
                           h.Allocation(args[4].base, True), h.Allocation(h.PA_LIMIT - h.PAGE, h.PAGE * 2)):
            with self.assertRaises(h.Refused):
                h.plan(*args[:4], allocation)
        with self.assertRaisesRegex(h.Refused, "insufficient"):
            h.plan(*fixture(opaque_bytes=h.PAGE))
        with self.assertRaisesRegex(h.Refused, "known exclusion"):
            h.plan(*fixture(hazard=True))
        with self.assertRaisesRegex(h.Refused, "bounds/alias"):
            h.plan(*fixture(ba_offset=0x20000))
        after = fixture(ba_offset=0x30000)
        with self.assertRaisesRegex(h.Refused, "bounds/alias"):
            h.plan(*updated_native(after, top_of_kernel_data=after[4].base + after[4].bytes))
        with self.assertRaisesRegex(h.Refused, "descriptor mismatch"):
            h.plan(*fixture(descriptor_offset=0x30000))
        with self.assertRaisesRegex(h.Refused, "chosen DRAM"):
            h.plan(*fixture(dram_size=0x3ffffc000))
        # Change the trusted allocation label alone: it cannot replace the DT descriptor.
        with self.assertRaises(h.Refused):
            h.plan(*args[:4], h.Allocation(args[4].base + h.PAGE, h.PAGE))

    def test_malformed_tree_and_fixed_property_rules(self):
        base, wire, expected, native, allocation = fixture()
        for length in range(len(base)):
            with self.assertRaises(h.Refused):
                h.canonical_base(base[:length])
        for bad in (base + b"\0", bytearray(base), b"x" * (h.ADT_LIMIT + 1),
                    node("root", (), (node("chosen"), node("chosen"))),
                    node("root", (), (node("chosen", (prop("name", b"again\0"),)),)),
                    node("root", (), (node("../chosen"),)),
                    node("root", (), (node("chosen"), node("../other"))),
                    node("root", (), (node("chosen", (prop("X" * 32, b"opaque"),)),)),
                    node("root", (), (node("chosen", (prop("opaque", b"data", True),)),)),
                    node("root", (), (node("chosen", (prop(h.PROPERTY, wire, True),)),))):
            with self.assertRaises(h.Refused):
                h.canonical_base(bad)
        # Duplicate name-property keys are refused even when their payloads match.
        bad = node("root", (), (node("chosen", (prop("x", b"a"), prop("x", b"a"))),))
        with self.assertRaisesRegex(h.Refused, "duplicate property"):
            h.canonical_base(bad)
        for size in (True, 0, 1279, 1281, 9473):
            with self.assertRaises(h.Refused):
                h.planned_size(base, size)

    def test_tree_depth_node_and_property_caps(self):
        chain = node("end")
        for _ in range(33):
            chain = node("level", (), (chain,))
        with self.assertRaises(h.Refused):
            h.canonical_base(node("root", (), (node("chosen"), chain)))
        children = tuple(node("n" + str(i)) for i in range(4094))
        with self.assertRaises(h.Refused):
            h.canonical_base(node("root", (), (node("chosen", (), children), node("last"))))
        chosen = node("chosen", tuple(prop("p" + str(i), b"") for i in range(4095)))
        full = node("root", (), (chosen,))
        self.assertEqual(h.canonical_base(full), full)
        with self.assertRaisesRegex(h.Refused, "additional property"):
            h.planned_size(full, 1280)

    def test_source_dependency_drift_before_import_and_after_import(self):
        with tempfile.TemporaryDirectory(prefix="handoff-deps-") as directory:
            bridge = Path(directory)
            target = bridge / "native_policy_handoff"
            target.mkdir()
            copied = target / "handoff.py"
            shutil.copyfile(h.__file__, copied)
            for relative in h.DEPENDENCIES:
                destination = bridge / relative
                destination.parent.mkdir(exist_ok=True)
                shutil.copyfile(Path(h.__file__).resolve().parents[1] / relative, destination)
            dependency = bridge / "native_policy_snapshot/facts_wire.py"
            original = dependency.read_bytes()
            dependency.write_bytes(original + b"\n")
            spec = importlib.util.spec_from_file_location("_handoff_drift_copy", copied)
            module = importlib.util.module_from_spec(spec)
            sys.modules[spec.name] = module
            with self.assertRaises(ValueError):
                spec.loader.exec_module(module)
            dependency.write_bytes(original)
            module = importlib.util.module_from_spec(spec)
            sys.modules[spec.name] = module
            spec.loader.exec_module(module)
            args = fixture()
            compatible = (args[0], args[1], module.Expected(args[2].nonce, args[2].identities),
                          args[3], module.Allocation(args[4].base, args[4].bytes))
            self.assertEqual(module.canonical_base(module.plan(*compatible).payload), args[0])
            dependency.write_bytes(original + b"\n")
            with self.assertRaisesRegex(module.Refused, "dependency changed"):
                module.plan(*compatible)

    def test_decoder_rejection_never_becomes_a_plan(self):
        base, wire, expected, native, allocation = fixture()
        for offset in (0, 8, 12, 48, 52, 64, 88, 320, 496, 784, 1168):
            changed = bytearray(wire)
            changed[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                h.plan(base, bytes(changed), expected, native, allocation)
        with self.assertRaises(ValueError):
            h.plan(base, wire, expected, native[:736], allocation)


if __name__ == "__main__":
    unittest.main()
