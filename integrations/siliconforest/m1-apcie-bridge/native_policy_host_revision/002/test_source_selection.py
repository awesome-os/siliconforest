"""Source-only strict selection and optin tests. No source acquisition or actor."""
import enum,importlib.util,json
from pathlib import Path
from types import SimpleNamespace
import unittest
HERE=Path(__file__).resolve().parent
P=HERE/'after/integrations/puredarwin/m1-bringup/private_policy_progress.py'
spec=importlib.util.spec_from_file_location('new_host002_source_selection',P);policy=importlib.util.module_from_spec(spec);spec.loader.exec_module(policy)
class IODEV(enum.IntEnum):USB0=4;USB1=5
class Other(enum.IntEnum):USB0=4
class Host002SelectionTests(unittest.TestCase):
    def test_actual_runtime_enum_normalized_without_user_int(self):
        proxy=SimpleNamespace(IODEV=IODEV);h=SimpleNamespace(iodev=IODEV.USB0);b=SimpleNamespace(proxy_module=proxy)
        module,typ,value,integer=policy.source_selection(h,b)
        self.assertIs(module,proxy);self.assertIs(typ,IODEV);self.assertIs(value,IODEV.USB0);self.assertIs(type(integer),int);self.assertEqual(integer,4)
    def test_wrong_type_value_boolean_and_user_conversion_refused(self):
        class Arbitrary(int):
            def __int__(self):raise AssertionError('untrusted conversion invoked')
        backend=SimpleNamespace(proxy_module=SimpleNamespace(IODEV=IODEV))
        for value in (4,True,4.0,'4',None,IODEV.USB1,Other.USB0,Arbitrary(4)):
            with self.subTest(value=repr(value)),self.assertRaises((ValueError,AttributeError)):
                policy.source_selection(SimpleNamespace(iodev=value),backend)
    def test_explicit_mode1_never_falls_back(self):
        old={'native_policy_experiment':{'version':1,'producer_profile':'t8103-pinned-hv-source-facts-v1'}}
        with self.assertRaisesRegex(ValueError,'Invalid private-policy experiment'):
            policy.typed_mode(SimpleNamespace(contents={'build_record':json.dumps(old).encode()}))
        self.assertFalse(policy.typed_mode(SimpleNamespace(contents={'build_record':b'{}'})))
    def test_no_guessed_producer_pin_pending_freeze(self):
        # This preparation-only assertion is removed/replaced by exact final pin
        # after the actual producer receipt exists; it is not a successful stub.
        self.assertIsNone(policy.SOURCES['integrations/puredarwin/m1-apcie-bridge/native_policy_producer_002/producer.py'])
        with self.assertRaises((ValueError,FileNotFoundError)):policy.check_sources()
if __name__=='__main__':unittest.main(verbosity=2)
