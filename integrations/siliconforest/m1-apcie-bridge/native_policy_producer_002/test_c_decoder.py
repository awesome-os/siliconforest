"""The unchanged C 908e decoder reads producer002 wires as intended. Host only."""
from dataclasses import replace
import hashlib
import sys
import unittest

sys.dont_write_bytecode = True
import cdecoder
import fixtures as fx
import oracle
import producer as p


class CDecoderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.c = cdecoder.Decoder()

    @classmethod
    def tearDownClass(cls):
        cls.c.close()

    def check(self, case):
        try:
            bundle = case.produce()
        except ValueError as error:
            self.fail("Known-valid synthetic source pair refused: " + str(error))
        py = p.facts.decode(bundle.wire, bundle.expected)
        status, out = self.c.decode(bundle.wire, bundle.expected)
        self.assertEqual(status, 0)
        want = [(r.tag, r.ordinal, r.flags, r.auxiliary, r.span.base, r.span.bytes) for r in py.facts.records]
        self.assertEqual(cdecoder.records(out), want)
        physical = [(b, n) for t, _, _, _, b, n in want if t == 4]
        self.assertEqual(physical, [(s.span.base, s.span.bytes) for s in bundle.original_inventory.physical])
        self.assertEqual(cdecoder.identities(out), bundle.expected.identities)
        self.assertEqual(cdecoder.identities(out)[3], hashlib.sha256(case.base).digest())
        self.assertEqual(cdecoder.labels(out), tuple(s.declared_sha256 for s in py.facts.sources))
        mapping, _ = oracle.projection(bundle.original_inventory, bundle.prepared_inventory)
        labels = oracle.expected_labels(py.facts.records, bundle.measurement.source_join_receipt,
                                        bundle.original_inventory, bundle.prepared_inventory, mapping)
        self.assertEqual(cdecoder.labels(out)[1:5], tuple(labels[t] for t in (2, 3, 4, 5)))
        self.assertEqual(cdecoder.union(out), [(r.base, r.bytes) for r in py.exclusion_union])
        # The guest checks the PREPARED identity; an original-ADT label refuses.
        swapped = list(bundle.expected.identities)
        swapped[3] = hashlib.sha256(case.original_adt).digest()
        status, _ = self.c.decode(bundle.wire, replace(bundle.expected, identities=tuple(swapped)))
        self.assertNotEqual(status, 0)
        return out

    def test_default_pair(self):
        out = self.check(fx.Case(p))
        self.assertEqual((out.record_count, out.union_count), (33, 2))

    def test_256_records(self):
        self.assertEqual(self.check(fx.Case(p, pmap_count=224, allocation_pages=2)).record_count, 256)

    def test_union_8_with_removed_rows_inside_ram(self):
        out = self.check(fx.Case(p, removed_inside=True, extra_inside=5))
        self.assertEqual(out.union_count, 8)


if __name__ == "__main__":
    unittest.main()
