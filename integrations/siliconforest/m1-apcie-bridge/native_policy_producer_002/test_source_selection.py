"""Host-boundary selection tests and pinned-source binding. Fake enums only.

The IODEV classes here are explicitly fake test doubles copied from the pinned
member table; passing them is not native proof of any runtime selection.
"""
import ast
import enum
import hashlib
from pathlib import Path
import sys
from types import SimpleNamespace
import types
import unittest

sys.dont_write_bytecode = True
import fixtures as fx
import oracle
import producer as p
import source_selection as s

M1N1 = oracle.LAB / "sources/m1n1-baseline/proxyclient/m1n1"
PINNED = {"proxy.py": s.PROXY_SHA256, "hv/__init__.py": s.HV_SHA256,
          "adt.py": "2b11e575aa6fa51e1417ada02261066b1e40aec38cfdc3e8039eba52b70133e6"}


def pinned(name):
    raw = (M1N1 / name).read_bytes()
    assert hashlib.sha256(raw).hexdigest() == PINNED[name], name
    return raw


def function(tree, name):
    return next(n for n in ast.walk(tree) if isinstance(n, ast.FunctionDef) and n.name == name)


class PinnedSourceBindingTests(unittest.TestCase):
    def test_proxy_iodev_members_and_whoami_return_enum(self):
        tree = ast.parse(pinned("proxy.py"))
        cls = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == "IODEV")
        self.assertEqual([ast.unparse(b) for b in cls.bases], ["IntEnum"])
        members = tuple((a.targets[0].id, ast.literal_eval(a.value)) for a in cls.body if isinstance(a, ast.Assign))
        self.assertEqual(members, s.PINNED_IODEV)
        whoami = function(tree, "iodev_whoami")
        self.assertEqual(ast.unparse(whoami.body[0]), "return IODEV(self.request(self.P_IODEV_WHOAMI))")

    def test_hv_stores_enum_and_edits_adt_as_modelled(self):
        tree = ast.parse(pinned("hv/__init__.py"))
        init = ast.unparse(function(tree, "init"))
        self.assertIn("self.iodev = self.p.iodev_whoami()", init)
        # setup_adt runs inside init, before any guest image is loaded.
        self.assertIn("self.setup_adt()", init)
        self.assertNotIn("setup_adt", ast.unparse(function(tree, "load_raw")))
        product = ast.unparse(function(tree, "setup_adt"))
        for line in ("self.adt['product'].product_name += ' on m1n1 hypervisor'",
                     "self.adt['product'].product_description += ' on m1n1 hypervisor'",
                     "soc_name = 'Virtual ' + self.adt['product'].product_soc_name + ' on cursed m1n1 hypervisor'",
                     "soc_name = 'Virtual ' + self.adt['product'].product_soc_name + ' on m1n1 hypervisor'",
                     "self.adt['product'].product_soc_name = soc_name"):
            self.assertIn(line, product)
        self.assertEqual((p.HV_SUFFIX, [t for _, t in p.HV_SOC]),
                         (" on m1n1 hypervisor", ["Virtual %s on m1n1 hypervisor", "Virtual %s on cursed m1n1 hypervisor"]))
        self.assertEqual(oracle.hv_setup_templates(), p.HV_USB_TEMPLATES)
        self.assertEqual(set(p.SELECTED_PATHS), set(fx.HV_USB0_DELETE))
        self.assertEqual(len(p.SELECTED_PATHS), 17)
        setup = ast.unparse(function(tree, "setup_adt"))
        self.assertIn("if self.iodev >= IODEV.USB0:", setup)
        self.assertIn("idx = self.iodev - IODEV.USB0", setup)
        load = ast.unparse(function(tree, "load_raw"))
        for line in ("idx = names.index('__OS_LOG')", "names = names[:idx] + names[idx + 1:]",
                     "node.segment_names = ';'.join(names)",
                     "node.segment_ranges = node.segment_ranges[:idx * 32] + node.segment_ranges[32 + idx * 32:]",
                     "for node in self.adt['/arm-io']:", "for nub in node:"):
            self.assertIn(line, load)
        loops = [n for n in ast.walk(function(tree, "load_raw")) if isinstance(n, ast.For) and
                 isinstance(n.target, ast.Name) and n.target.id in ("node", "nub")]
        self.assertEqual([ast.unparse(n.iter) for n in loops], ["self.adt['/arm-io']", "node"])

    def test_adt_rebuild_layout(self):
        text = pinned("adt.py").decode()
        for line in ('"name" / PaddedString(32, "ascii")', '"size" / Int32ul',
                     '"properties" / Array(this.property_count, Aligned(4, ADTPropertyStruct))',
                     '"size": len(value) | (0x80000000 if is_template else 0)',
                     "elif v and v[-1] == 0 and all(0x20 <= i <= 0x7e for i in v[:-1]):"):
            self.assertIn(line, text)


FAKE = "fake_pinned_proxy"   # explicitly fake module name for test doubles


def module_for(cls, name=None):
    module = types.ModuleType(name or cls.__module__)
    module.IODEV = cls
    return module


class SelectionTests(unittest.TestCase):
    def setUp(self):
        self.IODEV = enum.IntEnum("IODEV", list(s.PINNED_IODEV), module=FAKE)
        self.proxy = module_for(self.IODEV)

    def test_pinned_usb0_member_normalizes_to_builtin_int4(self):
        try:
            selection, value = s.select(SimpleNamespace(iodev=self.IODEV.USB0), self.proxy)
        except s.Refused as error:
            self.fail("pinned USB0 member refused: " + str(error))
        self.assertIs(type(value), int)
        self.assertEqual(value, 4)
        self.assertEqual((selection.enum_module, selection.enum_class, selection.member, selection.source_iodev),
                         (FAKE, "IODEV", "USB0", 4))
        witness = selection.witness()
        self.assertEqual((witness["enum_member"], witness["numeric_value"], witness["normalization"]),
                         ("USB0", 4, "int.__int__"))
        self.assertFalse(witness["inferred_from_adt"] or witness["authentication"] or
                         witness["source_bytes_verified_here"])
        case = fx.Case(p)
        self.assertEqual(case.measure(source_iodev=value).source_iodev, 4)
        with self.assertRaises(p.Refused):
            case.measure(source_iodev=self.IODEV.USB0)

    def test_custom_int_conversion_never_called(self):
        called = []

        class IODEV(enum.IntEnum):
            UART = 0
            DOCKCHANNEL_UART = 1
            FB = 2
            USB_VUART = 3
            USB0 = 4
            USB1 = 5
            USB2 = 6
            USB3 = 7
            USB4 = 8
            USB5 = 9
            USB6 = 10
            USB7 = 11

            def __int__(self):
                called.append(self)
                return 99

        try:
            _, value = s.select(SimpleNamespace(iodev=IODEV.USB0), module_for(IODEV))
        except s.Refused as error:
            self.fail("pinned USB0 member refused: " + str(error))
        self.assertEqual((type(value), value), (int, 4))
        self.assertFalse(called)

    def test_plain_bool_foreign_member_and_missing_refused(self):
        Other = enum.IntEnum("IODEV", list(s.PINNED_IODEV), module=FAKE)
        Short = enum.IntEnum("Short", [("USB0", 4)], module=FAKE)

        class Wide(int):
            def __int__(self):
                raise AssertionError("custom conversion invoked")

        for witness in (4, True, False, 4.0, "4", None, Wide(4), self.IODEV.USB1, self.IODEV.UART,
                        Other.USB0, Short.USB0):
            with self.subTest(witness=repr(witness)), self.assertRaises(s.Refused):
                s.select(SimpleNamespace(iodev=witness), self.proxy)
        with self.assertRaisesRegex(s.Refused, "missing"):
            s.select(SimpleNamespace(), self.proxy)
        with self.assertRaisesRegex(s.Refused, "missing"):
            s.select(SimpleNamespace(iodev=None), self.proxy)

        class Meta(enum.EnumMeta):
            pass

        class Custom(enum.IntEnum, metaclass=Meta):
            UART = 0
            DOCKCHANNEL_UART = 1
            FB = 2
            USB_VUART = 3
            USB0 = 4
            USB1 = 5
            USB2 = 6
            USB3 = 7
            USB4 = 8
            USB5 = 9
            USB6 = 10
            USB7 = 11

        Custom.__name__ = Custom.__qualname__ = "IODEV"
        # Each wrong class supplies its own USB0 member and a matching module,
        # so exactly one check has to refuse it.
        for name, cls, proxy in (
                ("short_table", enum.IntEnum("IODEV", [("USB0", 4)], module=FAKE), None),
                ("missing_member", enum.IntEnum("IODEV", list(s.PINNED_IODEV[:-1]), module=FAKE), None),
                ("wrong_class_name", enum.IntEnum("Device", list(s.PINNED_IODEV), module=FAKE), None),
                ("copied_from_other_module", enum.IntEnum("IODEV", list(s.PINNED_IODEV), module="not_m1n1_proxy"),
                 types.ModuleType(FAKE)),
                ("custom_metaclass", Custom, None)):
            proxy = module_for(cls) if proxy is None else proxy
            proxy.IODEV = cls
            with self.subTest(name=name), self.assertRaises(s.Refused):
                s.select(SimpleNamespace(iodev=cls.USB0), proxy)
        for proxy in (SimpleNamespace(), SimpleNamespace(IODEV=None), SimpleNamespace(IODEV=self.IODEV),
                      module_for(int, FAKE), module_for(Short),
                      module_for(enum.Enum("IODEV", list(s.PINNED_IODEV), module=FAKE))):
            with self.subTest(proxy=repr(proxy)), self.assertRaises(s.Refused):
                s.select(SimpleNamespace(iodev=self.IODEV.USB0), proxy)


if __name__ == "__main__":
    unittest.main()
