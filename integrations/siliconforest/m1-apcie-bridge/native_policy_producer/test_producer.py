"""Synthetic same-boot byte inputs only; no proxy, kernel or target calls."""
from dataclasses import replace
import hashlib
import importlib.util
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest

import producer as p


def prop(key, value):
    return key.encode().ljust(32, b"\0") + struct.pack("<I", len(value)) + value + b"\0" * ((-len(value)) & 3)


def node(name, properties=(), children=()):
    properties = (prop("name", name.encode() + b"\0"),) + tuple(properties)
    return struct.pack("<II", len(properties), len(children)) + b"".join(properties) + b"".join(children)


def fixture(*, display=2, opaque=0, include_vram=True, duplicate_row=False,
            zero_remap=True, hazard=False, empty_pmap=False, pmap_count=1, allocation_pages=1,
            vram_base=None, video_base=None, vram_bytes=0x3000000):
    dram_base, dram_bytes = p.facts.DRAM_BASE, 0x400000000
    original_phys, original_bytes = dram_base + 0x10000000, 0x30000000
    phys, size = original_phys + 0x10000000, 0x20000000
    vram = phys + 0x20000 if hazard else dram_base + 0x300000000
    vram = vram if vram_base is None else vram_base
    video = (vram + 0x4000 if video_base is None else video_base, 1, 1024, 256, 64, 0x1001e)
    allocation = p.handoff.Allocation(phys + 0x20000, p.PAGE * allocation_pages)
    mm = node("memory-map", (
        prop("DeviceTree", struct.pack("<QQ", allocation.base, allocation.bytes)),
        prop("BootArgs", struct.pack("<QQ", phys + 0x10000, p.PAGE)),
        prop("TrustCache", struct.pack("<QQ", phys + 0x30000, p.PAGE)),
        prop("RAMDisk", struct.pack("<QQ", phys + 0x40000, p.PAGE)),
    ))
    chosen = node("chosen", (prop("dram-base", struct.pack("<Q", dram_base)),
                              prop("dram-size", struct.pack("<Q", dram_bytes)),
                              prop("opaque", b"x" * opaque)), (mm,))
    pmap = b"" if empty_pmap else struct.pack("<QQII", 0x680000000, 0x4000, 0x40000000, 0x12345678) * pmap_count
    first = struct.pack("<QQQII", 0x690000000, (1 << 64) - 1,
                        0 if zero_remap else 0x100000, 0x4000, 7)
    second = first if duplicate_row else struct.pack("<QQQII", 0x690004000, 0, 0x200000, 0x4000, 11)
    device = node("device", (prop("segment-names", b"same;same\0"),
                              prop("segment-ranges", first + second)))
    vram_node = node("vram", (prop("device_type", b"vram\0"),
                               prop("reg", struct.pack("<QQ", vram, vram_bytes))))
    children = (chosen, node("defaults", (prop("pmap-io-ranges", pmap),)), node("arm-io", (), (device,)))
    if include_vram:
        children += (vram_node,)
    base = node("device-tree", (prop("#address-cells", struct.pack("<I", 2)),
                                 prop("#size-cells", struct.pack("<I", 2))), children)
    original = bytearray(736)
    struct.pack_into("<HH", original, 0, 2, 2)
    struct.pack_into("<QQQQ", original, 8, 0xfffffe0000000000, original_phys, original_bytes, original_phys + 0x400000)
    struct.pack_into("<6Q", original, 40, *video)
    struct.pack_into("<Q", original, 728, dram_bytes)
    original = bytes(original)
    words = [0x200000, 0x200003, 1, 0, 0, 0, 0x210000, 0x210007, 1, 0, 0, 0]
    mcc = struct.pack("<96I", *(words * 8))
    context = p.Context(bytes(range(1, 25)), b"K" * 32, b"R" * 32, p.HV_SHA256)
    measured = p.measure(base, original, mcc, context, profile=p.PROFILE, original_profile=p.ORIGINAL_PROFILE)
    native_video = dict(zip(("base", "display", "stride", "width", "height", "depth"), video))
    native_video["display"] = display
    native_values = dict(revision=2, version=2, virt_base=0xfffffe0010000000,
                         phys_base=phys, mem_size=size, top_of_kernel_data=phys + 0x400000,
                         video=native_video, machine_type=0,
                         devtree=allocation.base - phys + 0xfffffe0010000000,
                         devtree_size=measured.final_adt_bytes, cmdline="cpus=1 serial=3",
                         boot_flags=0, mem_size_actual=dram_bytes)
    native = p.bootargs.encode(native_values, wire_profile=p.bootargs.WIRE_PROFILE)
    return base, original, native, mcc, context, allocation


def produce(args):
    return p.produce(*args, profile=p.PROFILE, original_profile=p.ORIGINAL_PROFILE)


def change_native(args, **fields):
    base, original, native, mcc, context, allocation = args
    values = p.bootargs.decode(native, wire_profile=p.bootargs.WIRE_PROFILE)
    values.update(fields)
    return base, original, p.bootargs.encode(values, wire_profile=p.bootargs.WIRE_PROFILE), mcc, context, allocation


class ProducerTests(unittest.TestCase):
    def valid(self, args):
        try:
            return produce(args)
        except ValueError as error:
            self.fail("Known-valid synthetic source bundle refused: " + str(error))

    def test_roundtrip_actual_sources_and_all_identities(self):
        args = fixture()
        before = tuple(args)
        bundle = self.valid(args)
        self.assertEqual(tuple(args), before)
        observed = p.facts.decode(bundle.wire, bundle.expected)
        self.assertEqual(p.handoff.canonical_base(bundle.plan.payload), args[0])
        self.assertEqual(bundle.expected.identities, (args[4].kernel_sha256, args[4].ramdisk_sha256,
                         p.HV_SHA256, hashlib.sha256(args[0]).digest(), hashlib.sha256(args[1]).digest(),
                         hashlib.sha256(args[2]).digest()))
        self.assertEqual(observed.facts.ram, (observed.facts.envelope,))
        self.assertEqual(bundle.measurement.source_counts, (2, 1, 1, 2, 2, 1))
        self.assertEqual(observed.facts.records[3].tag, 3)
        self.assertEqual((observed.facts.records[2].flags, observed.facts.records[2].auxiliary),
                         (0x40000000, 0x12345678))
        self.assertEqual(observed.facts.records[3].span.bytes, 0x3000000)
        # Class3 is the exact named VRAM reservation, not the smaller footprint.
        self.assertGreater(observed.facts.records[3].span.bytes, 1024 * 64)
        self.assertFalse(bundle.metadata()["global_exclusion_coverage"])
        self.assertFalse(bundle.metadata()["target_write_performed"])
        self.assertEqual(observed.exclusion_union, (observed.facts.retained_boot,))

    def test_zero_remap_duplicate_rows_and_opaque_IOVA_survive(self):
        args = fixture(duplicate_row=True)
        bundle = self.valid(args)
        self.assertEqual(len(bundle.inventory.segments), 2)
        self.assertEqual(bundle.inventory.segments[0].raw, bundle.inventory.segments[1].raw)
        self.assertEqual([s.slot for s in bundle.inventory.segments], [0, 1])
        self.assertEqual([s.iova_opaque for s in bundle.inventory.segments], [(1 << 64) - 1] * 2)
        remaps = [r for r in p.facts.decode(bundle.wire, bundle.expected).facts.records if r.tag == 5]
        physical = [r for r in p.facts.decode(bundle.wire, bundle.expected).facts.records if r.tag == 4]
        self.assertEqual(len(physical), 2)
        self.assertEqual([r.ordinal for r in physical], [0, 1])
        self.assertEqual(physical[0].span, physical[1].span)
        self.assertEqual(len(remaps), 2)
        self.assertEqual([r.span.base for r in remaps], [0, 0])
        self.assertEqual([r.ordinal for r in remaps], [0, 1])
        empty = self.valid(fixture(empty_pmap=True))
        self.assertEqual(p.facts.decode(empty.wire, empty.expected).facts.sources[1].status, p.facts.OBSERVED_EMPTY)

    def test_original_profile_envelopes_and_video_fields(self):
        args = fixture()
        native = p.bootargs.decode(args[2], wire_profile=p.bootargs.WIRE_PROFILE)
        for fields in (dict(mem_size=native["mem_size"] + p.PAGE),
                       dict(phys_base=p.facts.DRAM_BASE, mem_size=0x40000000,
                            devtree=args[5].base - p.facts.DRAM_BASE + native["virt_base"]),
                       dict(devtree_size=native["devtree_size"] - 4),
                       dict(top_of_kernel_data=native["phys_base"]),
                       dict(top_of_kernel_data=native["phys_base"] + native["mem_size"] + p.PAGE)):
            with self.assertRaises(ValueError):
                produce(change_native(args, **fields))
        for key in ("base", "stride", "width", "height", "depth"):
            video = dict(native["video"])
            video[key] += 1
            with self.subTest(key=key), self.assertRaises(ValueError):
                produce(change_native(args, video=video))
        self.assertIsInstance(self.valid(fixture(display=0)), p.Bundle)
        # A non-page-aligned top is conservatively covered outward, not clipped.
        unaligned = change_native(args, top_of_kernel_data=native["top_of_kernel_data"] - 1)
        rounded = self.valid(unaligned)
        observed = p.facts.decode(rounded.wire, rounded.expected)
        self.assertEqual(observed.facts.retained_boot.bytes, 0x400000)
        for profile in ("", 1, None):
            with self.assertRaises(ValueError):
                p.produce(*args, profile=p.PROFILE, original_profile=profile)
            with self.assertRaises(ValueError):
                p.produce(*args, profile=profile, original_profile=p.ORIGINAL_PROFILE)

    def test_full_MCC_and_required_vram_no_cover_fallback(self):
        args = fixture()
        for offset in range(384):
            changed = bytearray(args[3])
            changed[offset] ^= 1
            with self.assertRaises(ValueError):
                produce(args[:3] + (bytes(changed),) + args[4:])
        with self.assertRaises(ValueError):
            produce(args[:3] + (args[3][:80],) + args[4:])
        # Mandatory named source is required even when other reservations exist.
        with self.assertRaises(ValueError):
            fixture(include_vram=False)
        outside = bytearray(args[1])
        struct.pack_into("<Q", outside, 40, p.facts.DRAM_BASE)
        with self.assertRaisesRegex(p.Refused, "VRAM"):
            produce((args[0], bytes(outside)) + args[2:])

    def test_bounds_context_and_plan_refusals(self):
        args = fixture()
        for context in (replace(args[4], nonce=b"\0" * 24), replace(args[4], kernel_sha256=b"\0" * 32),
                        replace(args[4], hv_source_sha256=b"x" * 32)):
            with self.assertRaises(ValueError):
                produce(args[:4] + (context, args[5]))
        with self.assertRaisesRegex(p.handoff.Refused, "insufficient"):
            produce(fixture(opaque=p.PAGE))
        with self.assertRaisesRegex(p.handoff.Refused, "known exclusion"):
            produce(fixture(hazard=True))
        for raw in (args[1][:88], args[1] + b"\0", bytearray(args[1])):
            with self.assertRaises(ValueError):
                produce((args[0], raw) + args[2:])

    def test_source_digest_is_recomputed_and_domain_separated(self):
        args = fixture()
        bundle = self.valid(args)
        observed = p.facts.decode(bundle.wire, bundle.expected)
        self.assertEqual(observed.facts.sources[0].declared_sha256,
                         hashlib.sha256(b"BCM_MCC_V1\0" + args[3]).digest())
        self.assertNotEqual(observed.facts.sources[3].declared_sha256,
                            observed.facts.sources[4].declared_sha256)
        self.assertEqual(sum(s.count for s in observed.facts.sources), len(observed.facts.records))
        for source in observed.facts.sources:
            self.assertNotEqual(source.declared_sha256, bytes(32))

    def test_measure_refuses_bad_sources_and_full_record_capacity(self):
        args = fixture()
        base, original, native, mcc, context, allocation = args
        changes = ((16, p.facts.DRAM_BASE - p.PAGE), (24, 0),
                   (40, p.facts.DRAM_BASE), (56, 1023), (64, 1024), (72, 0), (80, 31))
        for offset, value in changes:
            changed = bytearray(original)
            struct.pack_into("<Q", changed, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                p.measure(base, bytes(changed), mcc, context, profile=p.PROFILE,
                          original_profile=p.ORIGINAL_PROFILE)
        for raw in (mcc[:80], mcc[:192] + bytes(192)):
            with self.assertRaises(ValueError):
                p.measure(base, original, raw, context, profile=p.PROFILE,
                          original_profile=p.ORIGINAL_PROFILE)
        with self.assertRaisesRegex(p.Refused, "geometry"):
            fixture(vram_base=0, video_base=0)
        with self.assertRaisesRegex(p.Refused, "geometry"):
            fixture(video_base=p.facts.DRAM_BASE + 0x300004001)
        with self.assertRaisesRegex(p.Refused, "VRAM outside chosen DRAM"):
            fixture(vram_base=0x100000, video_base=0x104000)
        # Visible bytes may fit while the full declared allocation crosses DRAM.
        with self.assertRaisesRegex(p.Refused, "VRAM outside chosen DRAM"):
            fixture(vram_base=p.facts.DRAM_BASE + 0x400000000 - 0x100000,
                    vram_bytes=0x200000)
        full = self.valid(fixture(pmap_count=248, allocation_pages=2))
        self.assertEqual(len(p.facts.decode(full.wire, full.expected).facts.records), 256)
        with self.assertRaisesRegex(p.Refused, "source records exceed256"):
            fixture(pmap_count=251, allocation_pages=2)

    def test_dependency_binding_on_import_and_before_source_use(self):
        with tempfile.TemporaryDirectory(prefix="producer-dependency-") as directory:
            bridge = Path(directory)
            leaf = bridge / "native_policy_producer"
            leaf.mkdir()
            target = leaf / "producer.py"
            shutil.copyfile(p.__file__, target)
            dependencies = ("native_policy_handoff_002/handoff.py", "adt_source_inventory/inventory.py",
                            "native_policy_snapshot/facts_wire.py", "native_bootargs/native_bootargs.py")
            for name in dependencies:
                dest = bridge / name
                dest.parent.mkdir(exist_ok=True)
                shutil.copyfile(Path(p.__file__).resolve().parents[1] / name, dest)
            handoff = bridge / dependencies[0]
            original = handoff.read_bytes()
            handoff.write_bytes(original + b"\n")
            spec = importlib.util.spec_from_file_location("_producer_dependency_copy", target)
            copied = importlib.util.module_from_spec(spec)
            sys.modules[spec.name] = copied
            with self.assertRaises(ValueError):
                spec.loader.exec_module(copied)
            handoff.write_bytes(original)
            copied = importlib.util.module_from_spec(spec)
            sys.modules[spec.name] = copied
            spec.loader.exec_module(copied)
            base, original_ba, native, mcc, context, allocation = fixture()
            private_context = copied.Context(context.nonce, context.kernel_sha256,
                                              context.ramdisk_sha256, context.hv_source_sha256)
            copied.measure(base, original_ba, mcc, private_context, profile=copied.PROFILE,
                           original_profile=copied.ORIGINAL_PROFILE)
            inventory = bridge / dependencies[1]
            inventory.write_bytes(inventory.read_bytes() + b"\n")
            with self.assertRaisesRegex(copied.Refused, "dependency changed"):
                copied.measure(base, original_ba, mcc, private_context, profile=copied.PROFILE,
                               original_profile=copied.ORIGINAL_PROFILE)


if __name__ == "__main__":
    unittest.main()
