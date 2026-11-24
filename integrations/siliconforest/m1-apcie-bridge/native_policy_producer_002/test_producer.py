"""Synthetic dual-source tests for producer002. No proxy, kernel, device or target."""
from dataclasses import replace
import enum
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode = True
import fixtures as fx
import oracle
import producer as p

HV_ORDER = [0, 2, 5, 6, 8, 9, 10, 11, 13]
REMOVED = [(1, "FIRST_OS_LOG"), (3, "USB0_SELECTED_SUBTREE"), (4, "USB0_SELECTED_SUBTREE"),
           (7, "FIRST_OS_LOG"), (12, "USB0_SELECTED_SUBTREE")]


def records(bundle, tag):
    return [r for r in p.facts.decode(bundle.wire, bundle.expected).facts.records if r.tag == tag]


def span(raw):
    phys, _, remap, size, _ = struct.unpack("<QQQII", raw)
    return p.facts.Range(phys, size), p.facts.Range(remap, size)


class ProducerTests(unittest.TestCase):
    def valid(self, case, **override):
        return self.ok(lambda: case.produce(**override))

    def ok(self, call):
        try:
            return call()
        except ValueError as error:
            self.fail("Known-valid synthetic source input refused: " + str(error))

    def refused(self, case, message=None, **override):
        # measure and produce are checked separately; produce gets a valid
        # reference native wire so only its own source checks can refuse.
        native = fx.Case(p).native()
        for call in (lambda: case.measure(**override), lambda: case.produce(native=native, **override)):
            with self.assertRaises(ValueError) as caught:
                call()
            if message:
                self.assertIn(message, str(caught.exception))

    def test_roundtrip_all_original_rows_and_identities(self):
        case = fx.Case(p)
        bundle = self.valid(case)
        observed = p.facts.decode(bundle.wire, bundle.expected)
        f = observed.facts
        self.assertEqual(bundle.expected.identities, (b"K" * 32, b"R" * 32, p.HV_SHA256,
                         hashlib.sha256(case.base).digest(), hashlib.sha256(case.original_bootargs).digest(),
                         hashlib.sha256(case.native()).digest()))
        self.assertNotEqual(bundle.expected.identities[3], hashlib.sha256(case.original_adt).digest())
        self.assertEqual(p.handoff.canonical_base(bundle.plan.payload), case.base)
        m = bundle.measurement
        self.assertEqual(m, case.measure())
        self.assertEqual((m.base_adt_sha256, m.original_adt_sha256),
                         (hashlib.sha256(case.base).hexdigest(), hashlib.sha256(case.original_adt).hexdigest()))
        self.assertIs(type(m.source_iodev), int)
        self.assertEqual(m.source_counts, (2, 1, 1, 14, 14, 1))
        self.assertEqual((len(bundle.original_inventory.segments), len(bundle.prepared_inventory.segments)), (14, 9))
        original = bundle.original_inventory
        physical, remap = records(bundle, 4), records(bundle, 5)
        self.assertEqual([r.ordinal for r in physical], list(range(14)))
        self.assertEqual([r.span for r in physical], [span(s.raw)[0] for s in original.segments])
        self.assertEqual([r.span for r in remap], [span(s.raw)[1] for s in original.segments])
        self.assertEqual(sum(r.span.base == 0 for r in remap), 7)
        high = [s.ordinal for s in original.segments if s.iova_opaque == (1 << 64) - 1]
        self.assertEqual(high, [1, 6, 8, 10])   # opaque all-ones IOVA rows kept, never used as PAs
        self.assertEqual([physical[o].span for o in high], [span(original.segments[o].raw)[0] for o in high])
        pmap = records(bundle, 2)
        self.assertEqual([(r.flags, r.auxiliary, r.span) for r in pmap],
                         [(0x40000000, 0x12345678, p.facts.Range(0x680000000, 0x4000))])
        self.assertEqual(records(bundle, 3)[0].span, p.facts.Range(p.facts.DRAM_BASE + 0x300000000, 0x3000000))
        # Removed rows are emitted, including one below the current RAM window.
        rows = case.rows
        for ordinal, _ in REMOVED:
            self.assertEqual(physical[ordinal].span, span(original.segments[ordinal].raw)[0])
        self.assertLess(span(rows["L1"])[0].base, f.envelope.base)
        self.assertEqual(physical[7].span, span(rows["L1"])[0])
        # L3 is absent from PREPARED yet inside RAM: it enters the union.
        self.assertEqual(observed.exclusion_union, (f.retained_boot, span(rows["L3"])[0]))
        self.assertEqual(f.ram, (f.envelope,))
        meta = bundle.metadata()
        self.assertEqual((meta["emitted_origin"], meta["guest_origin_visibility"]),
                         ("ORIGINAL", "OPAQUE_COMPOSITE_LABELS_ONLY"))
        self.assertFalse(meta["global_exclusion_coverage"] or meta["target_write_performed"])
        text = json.dumps(meta)
        for leak in ("/arm-io", "__OS_LOG", "same", "dart-usb0"):
            self.assertNotIn(leak, text)

    def test_occurrence_map_duplicates_cross_node_and_reasons(self):
        case = fx.Case(p)
        bundle = self.valid(case)
        projection = bundle.projection
        self.assertEqual(list(projection.segments), HV_ORDER)
        self.assertEqual(list(projection.removed_segments), REMOVED)
        self.assertEqual(list(projection.pmap), [0])
        expected_map, expected_removed = oracle.projection(bundle.original_inventory, bundle.prepared_inventory)
        self.assertEqual((list(projection.segments), list(projection.removed_segments)),
                         (expected_map, expected_removed))
        original, prepared = bundle.original_inventory, bundle.prepared_inventory
        same = [s.ordinal for s in original.segments if s.raw == case.rows["A"]]
        self.assertEqual(same, [6, 8, 10])
        # Equal whole rows map by node and projected slot, never to one target.
        targets = [projection.segments[s.ordinal] for s in prepared.segments if s.raw == case.rows["A"]]
        self.assertEqual(targets, [6, 8, 10])
        device = [s for s in prepared.segments if s.path == "/arm-io/device"]
        self.assertEqual([(s.slot, projection.segments[s.ordinal]) for s in device], [(0, 6), (1, 8), (2, 9)])
        self.assertEqual(original.segments[9].name, fx.OS_LOG)    # second __OS_LOG retained
        kept = {original.segments[o].path for o in projection.segments}
        for path in ("/arm-io/bus/nub/deep", "/other/child", "/arm-io/dart-usb1", "/arm-io/usb-drd0-aux"):
            self.assertIn(path, kept)

    def test_receipt_exact_schema_independent_of_native_and_capped(self):
        case = fx.Case(p)
        bundle = self.valid(case)
        receipt = bundle.measurement.source_join_receipt
        mapping, removed = oracle.projection(bundle.original_inventory, bundle.prepared_inventory)
        want = oracle.receipt(bundle.original_inventory, bundle.prepared_inventory, mapping, removed,
                              bundle.measurement.source_counts, case.context, case.original_bootargs, case.mcc,
                              oracle.product_rewrite(case.original_adt, case.base))
        self.assertEqual(receipt, want)
        value = json.loads(receipt)
        self.assertEqual(sorted(value), ["caps", "context", "emitted", "global_exclusion_coverage",
                                         "guest_independent_origin_visibility", "occurrence_map", "original",
                                         "prepared", "profile", "projection", "schema_version", "selection",
                                         "status"])
        self.assertEqual(value["selection"]["source_iodev"], 4)
        self.assertEqual(value["original"]["adt_sha256"], hashlib.sha256(case.original_adt).hexdigest())
        self.assertEqual(value["prepared"]["adt_sha256"], hashlib.sha256(case.base).hexdigest())
        for leak in (b"/arm-io", b"__OS_LOG", b"same", b"dart"):
            self.assertNotIn(leak, receipt)
        video = dict(p.bootargs.decode(case.native(), wire_profile=p.bootargs.WIRE_PROFILE)["video"], display=0)
        other = case.produce(native=case.native(video=video))
        self.assertNotEqual(other.expected.identities[5], bundle.expected.identities[5])
        self.assertEqual(other.measurement.source_join_receipt, receipt)
        self.assertEqual((p.JOIN_RECEIPT_MAX, p.CAPS["receipt_bytes"], p.CAPS["records"], p.facts.MAX_RECORDS,
                          p.CAPS["physical_union"], p.CAPS["mcc_slots"]), (65536, 65536, 256, 256, 8, 4))
        with mock.patch.object(p, "JOIN_RECEIPT_MAX", len(receipt)):
            self.assertEqual(self.ok(case.measure).source_join_receipt, receipt)
        with mock.patch.object(p, "JOIN_RECEIPT_MAX", len(receipt) - 1):
            with self.assertRaises(ValueError) as caught:
                case.measure()
            self.assertIn("no truncation", str(caught.exception))

    def test_projection_refusals(self):
        def add_row(tree):
            node = fx.find(tree, "/arm-io/twin")
            node.set("segment-names", node.get("segment-names")[:-1] + b";new\0")
            node.set("segment-ranges", node.get("segment-ranges") + fx.row(0x698000000, 0, 0, 0x4000, 99))

        def change_row(tree):
            node = fx.find(tree, "/arm-io/twin")
            raw = bytearray(node.get("segment-ranges"))
            raw[28] ^= 1
            node.set("segment-ranges", bytes(raw))

        def drop_plain_row(tree):
            node = fx.find(tree, "/arm-io/bus/nub")
            node.props = [[k, v] for k, v in node.props if not k.startswith("segment-")]

        def wrong_oslog(tree):
            node = fx.find(tree, "/arm-io/device")
            rows = [("same", case.rows["A"]), (fx.OS_LOG, case.rows["L1"]), ("same", case.rows["A"])]
            for key, value in fx.segments(rows):
                node.set(key, value)

        def keep_first_oslog(tree):
            for key, value in fx.segments([("x", case.rows["B"]), (fx.OS_LOG, case.rows["L3"])]):
                fx.find(tree, "/arm-io/bus/nub").set(key, value)

        def readd_usb0(tree):
            fx.find(tree, "/arm-io").children.append(fx.Node("dart-usb0", fx.segments(
                [("r", case.rows["C"]), (fx.OS_LOG, case.rows["L5"])])))

        def move_twin(tree):
            twin = fx.find(tree, "/arm-io/twin")
            device = fx.find(tree, "/arm-io/device")
            twin.props, device.props = device.props, twin.props

        def poke(path, key, data):
            return lambda tree: fx.find(tree, path).set(key, data)

        case = fx.Case(p)
        edits = dict(
            prepared_only_row=add_row, changed_row=change_row, extra_removal=drop_plain_row,
            second_oslog_removed=wrong_oslog, first_oslog_kept=keep_first_oslog,
            selected_subtree_kept=readd_usb0,
            depth5_subtree_deleted=lambda t: fx.delete(t, "/arm-io/bus/nub/deep"),
            outside_arm_io_deleted=lambda t: fx.delete(t, "/other/child"),
            usb1_subtree_deleted=lambda t: fx.delete(t, "/arm-io/dart-usb1"),
            unanchored_sibling_deleted=lambda t: fx.delete(t, "/arm-io/usb-drd0-aux"),
            rows_swapped_between_nodes=move_twin,
            pmap_changed=poke("/defaults", "pmap-io-ranges", struct.pack("<QQII", 0x680000000, 0x8000, 0x40000000, 0x12345678)),
            dram_changed=poke("/chosen", "dram-size", struct.pack("<Q", fx.DRAM_BYTES - 0x100000)),
            vram_changed=poke("/vram", "reg", struct.pack("<QQ", p.facts.DRAM_BASE + 0x300000000, 0x2000000)))
        for name, edit in edits.items():
            with self.subTest(name=name):
                self.refused(fx.Case(p, prepared_edit=edit), "exact pinned USB0 projection")
        two_rows = struct.pack("<QQII", 0x680000000, 0x4000, 1, 2) + struct.pack("<QQII", 0x681000000, 0x4000, 3, 4)
        self.valid(fx.Case(p, original_edit=poke("/defaults", "pmap-io-ranges", two_rows)))
        self.refused(fx.Case(p, original_edit=poke("/defaults", "pmap-io-ranges", two_rows),
                             prepared_edit=poke("/defaults", "pmap-io-ranges", two_rows[24:] + two_rows[:24])))
        for junk in (dict(junk_padding={("/arm-io/twin", "segment-names")}),
                     dict(junk_padding={("/arm-io/device", "segment-names")}),
                     dict(junk_key={("/arm-io/twin", "segment-ranges")}),
                     dict(junk_key={("/arm-io/device", "segment-ranges")})):
            with self.subTest(junk=junk):
                self.refused(fx.Case(p, **junk), "exact pinned USB0 projection")
        with self.assertRaises(ValueError):   # origins swapped
            p.measure(case.original_adt, case.original_bootargs, case.mcc, case.context,
                      **case.kw(original_adt=case.base))
        for bad in (None, b"", bytearray(case.original_adt), case.original_adt[:-4]):
            with self.subTest(original=repr(bad)[:20]):
                self.refused(case, original_adt=bad)
        with self.assertRaises(TypeError):
            p.measure(case.base, case.original_bootargs, case.mcc, case.context,
                      profile=p.PROFILE, original_profile=p.ORIGINAL_PROFILE, source_iodev=4)

    def test_original_must_predate_hv_setup_adt(self):
        # Without a second eligible __OS_LOG, a prepared or half-prepared tree
        # is a valid projection source of itself; only the product rewrite
        # relation (setup_adt runs once, before load_raw) refuses these.
        case = fx.Case(p, second_oslog=False)
        self.valid(case)
        tweaked = fx.copy.deepcopy(case.prepared_tree)
        fx.find(tweaked, "/defaults").set("serial-device", struct.pack("<I", 0x66))
        half = fx.serialize(case.setup_only)
        twice = fx.serialize(fx.hv_load_raw(fx.hv_setup_adt(case.setup_only), case.allocation, case.phys + 0x10000))
        for name, original, base in (("identical", case.base, case.base),
                                     ("prepared_with_byte_flip", fx.serialize(tweaked), case.base),
                                     ("captured_after_setup_adt", half, case.base),
                                     ("setup_adt_applied_twice", half, twice)):
            with self.subTest(name=name):
                with self.assertRaises(ValueError) as caught:
                    p.measure(base, case.original_bootargs, case.mcc, case.context, **case.kw(original_adt=original))
                self.assertIn("already carries the pinned HV product rewrite", str(caught.exception))
        # Projection-only acceptance would have dropped original rows here.
        inventory = oracle.inventory_module()
        lost = len(inventory.extract(case.original_adt, profile=inventory.PROFILE).segments) - \
            len(inventory.extract(half, profile=inventory.PROFILE).segments)
        self.assertEqual(lost, 3)

    def test_prepared_product_rewrite_exact(self):
        def poke(key, value):
            return lambda tree: fx.find(tree, "/product").set(key, value)

        cursed = self.valid(fx.Case(p, sprr=True))
        self.assertEqual(json.loads(cursed.measurement.source_join_receipt)["projection"]["hv_product_rewrite"],
                         "PINNED_SETUP_ADT_CURSED_SPRR")
        plain = self.valid(fx.Case(p))
        self.assertEqual(json.loads(plain.measurement.source_join_receipt)["projection"]["hv_product_rewrite"],
                         "PINNED_SETUP_ADT_PLAIN")
        for key, value, message in (
                ("product-name", b"MacBook Pro (13-inch, M1, 2020)\0", "product-name"),
                ("product-name", b"MacBook Pro (13-inch, M1, 2020) on m1n1 hypervisor on m1n1 hypervisor\0", "product-name"),
                ("product-description", b"MacBook Pro (13-inch, M1, 2020)\0", "product-description"),
                ("product-soc-name", b"Apple M1\0", "product-soc-name"),
                ("product-soc-name", b"Virtual Apple M1 on m1n1 hypervisor \0", "product-soc-name"),
                ("product-soc-name", b"Virtual Apple M1 on m1n1 hypervisor\0\0", "C string")):
            with self.subTest(key=key, value=value):
                self.refused(fx.Case(p, prepared_edit=poke(key, value)), message)
        self.refused(fx.Case(p, prepared_edit=lambda t: setattr(fx.find(t, "/product"), "props", [])))

    def test_selected_subtree_inside_eligible_nub_and_emptying_removal(self):
        case = fx.Case(p, hpm=True)
        bundle = self.valid(case)
        mapping, removed = oracle.projection(bundle.original_inventory, bundle.prepared_inventory)
        self.assertEqual((list(bundle.projection.segments), list(bundle.projection.removed_segments)),
                         (mapping, removed))
        reasons = {bundle.original_inventory.segments[o].raw: why for o, why in removed}
        self.assertEqual((reasons[case.rows["L7"]], reasons[case.rows["Q"]]),
                         ("FIRST_OS_LOG", "USB0_SELECTED_SUBTREE"))
        # A node whose only row is __OS_LOG: the faithful HV output has empty
        # segment properties (inventory refuses); dropping them refuses here.
        self.refused(fx.Case(p, solo_oslog=True), "segment record framing")

        def drop_solo(tree):
            node = fx.find(tree, "/arm-io/solo")
            node.props = [[k, v] for k, v in node.props if not k.startswith("segment-")]

        self.refused(fx.Case(p, solo_oslog=True, prepared_edit=drop_solo), "would empty")

    def test_source_iodev_exact_builtin_int4(self):
        class IODEV(enum.IntEnum):
            USB0 = 4
            USB1 = 5

        class Wide(int):
            pass

        case = fx.Case(p)
        self.assertEqual(case.measure(source_iodev=4).source_iodev, 4)
        for bad in (True, False, 5, 0, 4.0, "4", None, IODEV.USB0, IODEV.USB1, Wide(4), b"\x04"):
            with self.subTest(source_iodev=repr(bad)):
                self.refused(case, "built-in int 4", source_iodev=bad)
        with self.assertRaises(TypeError):
            p.measure(case.base, case.original_bootargs, case.mcc, case.context,
                      profile=p.PROFILE, original_profile=p.ORIGINAL_PROFILE, original_adt=case.original_adt)

    def test_capacity_256_257_counts_all_original_rows(self):
        full = self.valid(fx.Case(p, pmap_count=224, allocation_pages=2))
        self.assertEqual(len(p.facts.decode(full.wire, full.expected).facts.records), 256)
        self.assertEqual(len(full.wire), p.facts.MAX_BYTES)
        over = fx.Case(p, pmap_count=225, allocation_pages=2)
        with self.assertRaises(ValueError) as caught:
            over.measure()
        self.assertIn("exceed 256", str(caught.exception))
        prepared = oracle.inventory_module().extract(over.base, profile="t8103-base-adt-source-inventory-v1")
        self.assertEqual(2 + len(prepared.pmap) + 1 + 2 * len(prepared.segments) + 1, 247)

    def test_union_8_9_removed_rows_adjacency_overlap_and_no_clipping(self):
        def union(case):
            bundle = self.valid(case)
            return p.facts.decode(bundle.wire, bundle.expected)

        self.assertEqual(len(union(fx.Case(p, extra_inside=6)).exclusion_union), 8)
        with self.assertRaisesRegex(ValueError, "exceeds8"):
            fx.Case(p, extra_inside=7).produce()
        case = fx.Case(p, removed_inside=True, extra_inside=5)
        observed = union(case)
        self.assertEqual(len(observed.exclusion_union), 8)
        self.assertIn(span(case.rows["L1"])[0], observed.exclusion_union)
        nine = fx.Case(p, removed_inside=True, extra_inside=6)
        with self.assertRaisesRegex(ValueError, "exceeds8"):
            nine.produce()
        prepared = oracle.inventory_module().extract(nine.base, profile="t8103-base-adt-source-inventory-v1")
        env = p.facts.Range(nine.phys, nine.size)
        inside = {(s.span.base, s.span.bytes) for s in prepared.physical if p.facts._intersects(s.span, env)}
        self.assertEqual(len(inside) + 1, 7)   # a prepared-only union would wrongly fit
        rows = fx.Case(p).rows
        l3 = span(rows["L3"])[0]
        adjacent = union(fx.Case(p, adjacent=True)).exclusion_union
        self.assertEqual(adjacent[1], p.facts.Range(l3.base, 0x8000))
        overlap = union(fx.Case(p, overlap=True)).exclusion_union
        self.assertEqual(overlap[1], p.facts.Range(l3.base, 0x6000))
        straddle = fx.Case(p, straddle=True)
        first = union(straddle).exclusion_union[0]
        self.assertEqual(first, p.facts.Range(straddle.phys - 0x4000, 0x404000))
        below = union(fx.Case(p))
        self.assertNotIn(span(rows["L1"])[0], below.exclusion_union)
        self.assertIn(span(rows["L1"])[0], [r.span for r in below.facts.records if r.tag == 4])

    def test_measure_produce_repeat_and_change_after_measure(self):
        case = fx.Case(p)
        first, again = case.measure(), case.measure()
        self.assertEqual(first, again)
        self.assertEqual(self.valid(case).measurement, first)

        def edit(tree):
            node = fx.find(tree, "/arm-io/twin")
            raw = bytearray(node.get("segment-ranges"))
            raw[24] ^= 1
            node.set("segment-ranges", bytes(raw))

        changed = fx.Case(p, original_edit=edit)
        later = changed.produce(native=case.native(first))
        self.assertNotEqual(later.measurement, first)
        self.assertNotEqual(later.measurement.source_join_receipt, first.source_join_receipt)
        other_nonce = replace(case.context, nonce=bytes(range(2, 26)))
        bumped = p.measure(case.base, case.original_bootargs, case.mcc, other_nonce, **case.kw())
        self.assertNotEqual(bumped.source_join_receipt, first.source_join_receipt)

    def test_class_labels_match_independent_oracle(self):
        case = fx.Case(p)
        bundle = self.valid(case)
        f = p.facts.decode(bundle.wire, bundle.expected).facts
        mapping, _ = oracle.projection(bundle.original_inventory, bundle.prepared_inventory)
        labels = oracle.expected_labels(f.records, bundle.measurement.source_join_receipt,
                                        bundle.original_inventory, bundle.prepared_inventory, mapping)
        for tag in (2, 3, 4, 5):
            self.assertEqual(f.sources[tag - 1].declared_sha256, labels[tag], tag)
        self.assertEqual(f.sources[0].declared_sha256, hashlib.sha256(b"BCM_MCC_V1\0" + case.mcc).digest())
        retained = f.retained_boot
        self.assertEqual(f.sources[5].declared_sha256, hashlib.sha256(
            b"BCM_BOOT_PREFIX_V1\0" + hashlib.sha256(case.native()).digest() +
            struct.pack("<QQ", retained.base, retained.bytes)).digest())
        digests = [s.declared_sha256 for s in f.sources]
        self.assertEqual(len(set(digests)), 6)
        self.assertEqual(sum(s.count for s in f.sources), len(f.records))

    def test_framing_is_unambiguous_and_role_bound(self):
        from types import SimpleNamespace as N
        a = N(properties=(N(path="/a", key="bc", serialized=b"x"),))
        b = N(properties=(N(path="/ab", key="c", serialized=b"x"),))
        self.assertNotEqual(p._properties_sha256(a), p._properties_sha256(b))
        inv = lambda h: N(base_adt_sha256=h * 64, base_adt_bytes=1)
        one, two = inv("1"), inv("2")
        rows = (p.facts.Record(4, 0, 0, 0, p.facts.Range(1, 1)),)
        base = p._class_digest(4, b"r", one, two, (), (), lambda i: 1, (0, 1), rows)
        for changed in (p._class_digest(4, b"r", two, one, (), (), lambda i: 1, (0, 1), rows),
                        p._class_digest(4, b"r", one, two, (), (), lambda i: 1, (1, 0), rows),
                        p._class_digest(4, b"s", one, two, (), (), lambda i: 1, (0, 1), rows),
                        p._class_digest(5, b"r", one, two, (), (), lambda i: 1, (0, 1), rows),
                        p._class_digest(4, b"r", one, one, (), (), lambda i: 1, (0, 1), rows)):
            self.assertNotEqual(changed, base)

    def test_native_profile_context_and_mcc_refusals(self):
        case = fx.Case(p)
        native = p.bootargs.decode(case.native(), wire_profile=p.bootargs.WIRE_PROFILE)
        for fields in (dict(mem_size=native["mem_size"] + p.PAGE), dict(devtree_size=native["devtree_size"] - 4),
                       dict(top_of_kernel_data=native["phys_base"]),
                       dict(phys_base=p.facts.DRAM_BASE, mem_size=0x40000000,
                            devtree=case.allocation.base - p.facts.DRAM_BASE + native["virt_base"])):
            with self.subTest(fields=sorted(fields)), self.assertRaises(ValueError):
                case.produce(native=case.native(**fields))
        for key in ("base", "stride", "width", "height", "depth"):
            video = dict(native["video"])
            video[key] += 1
            with self.subTest(key=key), self.assertRaises(ValueError):
                case.produce(native=case.native(video=video))
        # A non-page-aligned top is covered outward, never clipped.
        unaligned = self.ok(lambda: case.produce(native=case.native(top_of_kernel_data=case.phys + 0x400000 - 1)))
        self.assertEqual(p.facts.decode(unaligned.wire, unaligned.expected).facts.retained_boot,
                         p.facts.Range(case.phys, 0x400000))
        for override in (dict(profile="t8103-pinned-hv-source-facts-v1"), dict(profile=None),
                         dict(original_profile="")):
            with self.subTest(override=override):
                self.refused(case, **override)
        for context in (replace(case.context, nonce=bytes(24)), replace(case.context, hv_source_sha256=b"x" * 32)):
            with self.assertRaises(ValueError):
                p.measure(case.base, case.original_bootargs, case.mcc, context, **case.kw())
        for offset in range(0, 384, 7):
            bad = bytearray(case.mcc)
            bad[offset] ^= 1
            with self.assertRaises(ValueError):
                p.measure(case.base, case.original_bootargs, bytes(bad), case.context, **case.kw())
        with self.assertRaises(ValueError):
            p.measure(case.base, case.original_bootargs, case.mcc[:80], case.context, **case.kw())

    def test_dependency_binding_on_import_and_before_source_use(self):
        with tempfile.TemporaryDirectory(prefix="producer002-dependency-") as directory:
            bridge = Path(directory)
            leaf = bridge / "native_policy_producer_002"
            leaf.mkdir()
            target = leaf / "producer.py"
            shutil.copyfile(p.__file__, target)
            names = ("native_policy_handoff_002/handoff.py", "adt_source_inventory/inventory.py",
                     "native_policy_snapshot/facts_wire.py", "native_bootargs/native_bootargs.py")
            for name in names:
                (bridge / name).parent.mkdir(exist_ok=True)
                shutil.copyfile(Path(p.__file__).resolve().parents[1] / name, bridge / name)
            handoff = bridge / names[0]
            raw = handoff.read_bytes()
            handoff.write_bytes(raw + b"\n")
            spec = importlib.util.spec_from_file_location("_producer002_copy", target)
            copied = importlib.util.module_from_spec(spec)
            with self.assertRaises(ValueError):
                spec.loader.exec_module(copied)
            handoff.write_bytes(raw)
            copied = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(copied)
            case = fx.Case(copied)
            case.measure()
            inventory = bridge / names[1]
            inventory.write_bytes(inventory.read_bytes() + b"\n")
            with self.assertRaisesRegex(copied.Refused, "dependency changed"):
                case.measure()


if __name__ == "__main__":
    unittest.main()
