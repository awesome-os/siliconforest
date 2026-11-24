#!/usr/bin/env python3
"""Host-only BootArgs translation/refusal tests; no serial imports or device I/O."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bootargs_progress as b


def fixture():
    # Scalars printed by the failed keyboard001 window startup, not a raw
    # BootArgs capture or a claim that this fixture was read from hardware.
    return dict(revision=2, version=2, virt_base=0xfffffffffe078000,
                phys_base=0x802078000, mem_size=0x3d0ce4000,
                mem_size_actual=0x400000000, top_of_kernel_data=0x805764000,
                devtree=0x70c000, devtree_size=0x60000)


class BootArgsChecks(unittest.TestCase):
    def validate(self, ba=None, address=0x805760000, size=736):
        return b.validate_bootargs(fixture() if ba is None else ba, address, size)

    def test_observed_wrap_translates_without_mutating_metadata(self):
        ba = fixture()
        before = copy.deepcopy(ba)
        self.assertLess(ba['devtree']-ba['virt_base']+ba['phys_base'], 0)
        self.assertEqual(self.validate(ba), 0x80470c000)
        self.assertEqual(ba, before)

    def test_nonwrapped_display002_metadata(self):
        ba = fixture()
        # Selected scalars decoded independently from saved display002 BA.
        ba.update(virt_base=0xc078000, devtree=0x13010000,
                  devtree_size=377476, top_of_kernel_data=0x809074000)
        self.assertEqual(self.validate(ba), 0x809010000)

    def test_inverse_wrapped_va_and_rebuilt_metadata_roundtrip(self):
        ba = fixture()
        before = copy.deepcopy(ba)
        for address in (ba['phys_base'], 0x80470c000, 0x809010000,
                        ba['phys_base'] + ba['mem_size'] - b.PAGE):
            with self.subTest(address=hex(address)):
                virtual = b.physical_to_virtual(ba, address, b.PAGE)
                rebuilt = dict(ba, devtree=virtual, devtree_size=b.PAGE)
                self.assertEqual(self.validate(rebuilt), address)
        self.assertEqual(b.physical_to_virtual(ba, 0x80470c000, 0x60000), 0x70c000)
        self.assertEqual(ba, before)

    def test_forward_nonwrapping_high_va_roundtrip(self):
        ba = fixture()
        ba['virt_base'] = 0xfffffe0010000000
        addr = 0x809010000
        va = b.physical_to_virtual(ba, addr, b.PAGE)
        self.assertEqual(va, ba['virt_base'] + addr - ba['phys_base'])
        self.assertEqual(self.validate(dict(ba, devtree=va, devtree_size=b.PAGE)), addr)

    def test_all_fields_reject_non_u64_without_masking(self):
        for name in b.FIELDS:
            for value in (True, False, -1, 1 << 64, 1.0, '0', None):
                with self.subTest(field=name, value=value), self.assertRaises(b.BootArgsError):
                    self.validate(dict(fixture(), **{name: value}))

    def test_missing_fields_refuse(self):
        for name in b.FIELDS:
            ba = fixture()
            del ba[name]
            with self.subTest(field=name), self.assertRaises(b.BootArgsError):
                self.validate(ba)

    def test_integer_subclasses_allowed(self):
        class HexInt(int):
            pass
        self.assertEqual(self.validate({k: HexInt(v) for k, v in fixture().items()}), 0x80470c000)

    def test_abi_checks_preserved(self):
        for revision in (1, 2, 3):
            for version in (1, 2):
                self.assertEqual(self.validate(dict(fixture(), revision=revision, version=version)), 0x80470c000)
        for field, value in (('revision', 0), ('revision', 4), ('version', 0), ('version', 3)):
            with self.subTest(field=field, value=value), self.assertRaises(b.BootArgsError):
                self.validate(dict(fixture(), **{field: value}))

    def test_ram_layout_top_and_alignment_refusals(self):
        ba = fixture()
        cases = [dict(phys_base=0), dict(phys_base=ba['phys_base']+1),
                 dict(virt_base=ba['virt_base']+1), dict(mem_size=0),
                 dict(mem_size=ba['mem_size_actual']+1), dict(mem_size_actual=b.MAX_RAM+1),
                 dict(mem_size=ba['mem_size_actual']),  # usable high exceeds actual DRAM
                 dict(top_of_kernel_data=ba['phys_base']-1),
                 dict(top_of_kernel_data=ba['phys_base']+ba['mem_size']+1),
                 dict(phys_base=(1<<64)-b.PAGE, mem_size=b.PAGE*2,
                      mem_size_actual=1<<32, top_of_kernel_data=(1<<64)-b.PAGE)]
        for changes in cases:
            with self.subTest(changes=changes), self.assertRaises(b.BootArgsError):
                self.validate(dict(ba, **changes))

    def test_bootargs_physical_range_and_size_not_wrapped(self):
        ba = fixture()
        high = ba['phys_base'] + ba['mem_size']
        for addr, size in ((True, 736), (-1, 736), (1<<64, 736),
                           (0x800000000-1, 736), (high-735, 736),
                           (0x805760000, 0), (0x805760000, -1),
                           (0x805760000, True), (0x805760000, 1<<64)):
            with self.subTest(address=addr, size=size), self.assertRaises(b.BootArgsError):
                self.validate(ba, addr, size)
        # Original BootArgs may be in the bounded DRAM prefix, as before.
        self.assertEqual(self.validate(ba, 0x800000000, 736), 0x80470c000)

    def test_arbitrary_va_and_near_underflow_do_not_become_valid_ram(self):
        ba = fixture()
        for delta in (-1, -b.PAGE, ba['mem_size'], b.MAX_RAM, 1<<63):
            value = (ba['virt_base'] + delta) & b.U64_MASK
            with self.subTest(delta=delta), self.assertRaises(b.BootArgsError):
                self.validate(dict(ba, devtree=value))

    def test_adt_size_and_end_bounds(self):
        ba = fixture()
        for size in (0, b.MAX_ADT+1):
            with self.subTest(size=size), self.assertRaises(b.BootArgsError):
                self.validate(dict(ba, devtree_size=size))
        last = (ba['virt_base'] + ba['mem_size'] - b.PAGE) & b.U64_MASK
        end = dict(ba, devtree=last, devtree_size=b.PAGE)
        self.assertEqual(self.validate(end), ba['phys_base']+ba['mem_size']-b.PAGE)
        with self.assertRaises(b.BootArgsError):
            self.validate(dict(end, devtree_size=b.PAGE+1))

    def test_reverse_refuses_unbounded_physical_spans(self):
        ba = fixture()
        high = ba['phys_base'] + ba['mem_size']
        cases = [(ba['phys_base']-1, 1), (high, 1), (high-1, 2),
                 ((1<<64)-1, 2), (-1, 1), (True, 1), (1<<64, 1),
                 (ba['phys_base'], 0), (ba['phys_base'], True),
                 (ba['phys_base'], -1), (ba['phys_base'], 1<<64)]
        for addr, size in cases:
            with self.subTest(address=addr, size=size), self.assertRaises(b.BootArgsError):
                b.physical_to_virtual(ba, addr, size)

    def test_reverse_enforces_same_metadata_guards(self):
        for changes in (dict(revision=4), dict(phys_base=0), dict(virt_base=True),
                        dict(top_of_kernel_data=0), dict(mem_size_actual=b.MAX_RAM+1)):
            with self.subTest(changes=changes), self.assertRaises(b.BootArgsError):
                b.physical_to_virtual(dict(fixture(), **changes), 0x80470c000, b.PAGE)


if __name__ == '__main__':
    unittest.main()
