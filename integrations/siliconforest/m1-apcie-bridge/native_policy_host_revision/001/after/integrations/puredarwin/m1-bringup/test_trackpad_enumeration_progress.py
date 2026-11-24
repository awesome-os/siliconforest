"""Host-only explicit T1 opt-in; no target, transport or fake input."""
import ast
import json
import unittest
import host_progress as h
from test_native_desktop_progress import inputs, MEMBERS

E = {'version':1,'driver':'PDM1SPIHID','interface':2,'mode':'enumeration_only',
     'feature_writes':False,'diagnostic_property':'PDM1TrackpadEnumerationV1'}
def fixture():
    i=inputs();r=json.loads(i.contents['build_record']);r['trackpad_enumeration_experiment']=dict(E)
    i.contents['build_record']=json.dumps(r).encode();return i

class TrackpadPolicy(unittest.TestCase):
    def test_default_and_exact_optin(self):
        self.assertFalse(h.trackpad_enumeration_enabled(inputs({})))
        self.assertTrue(h.trackpad_enumeration_enabled(fixture()))
        self.assertNotIn('pdm1trackpadenum',h.guest_bootargs(inputs()))
        self.assertEqual(h.guest_bootargs(fixture()),h.BOOTARGS+' pdm1kbd=1 pdm1trackpadenum=1')
    def test_malformed_field_types_unknowns(self):
        for key,values in [('version',[True,0,'1']),('interface',[True,1,'2',3]),
                           ('feature_writes',[0,True,None]),('driver',['Other',None]),
                           ('mode',['enable_mt',None]),('diagnostic_property',['Other',None]),('extra',[False])]:
            for v in values:
                i=fixture();r=json.loads(i.contents['build_record']);r['trackpad_enumeration_experiment'][key]=v
                i.contents['build_record']=json.dumps(r).encode()
                with self.subTest(key=key,value=v),self.assertRaises(ValueError):h.trackpad_enumeration_enabled(i)
        for v in [None,False,[],1]:
            i=fixture();r=json.loads(i.contents['build_record']);r['trackpad_enumeration_experiment']=v
            i.contents['build_record']=json.dumps(r).encode()
            with self.assertRaises(ValueError):h.trackpad_enumeration_enabled(i)
    def test_exclusive_native_mode_only(self):
        for key in ['native_keyboard_desktop_experiment','desktop_experiment']:
            i=fixture();r=json.loads(i.contents['build_record']);del r[key]
            i.contents['build_record']=json.dumps(r).encode()
            with self.subTest(key=key),self.assertRaises(ValueError):h.trackpad_enumeration_enabled(i)
        for key in ['dart_survey_experiment','terminal_input_experiment','keyboard_experiment','virtual_input_experiment']:
            i=fixture();r=json.loads(i.contents['build_record']);r[key]=None
            i.contents['build_record']=json.dumps(r).encode()
            with self.subTest(key=key),self.assertRaises(ValueError):h.trackpad_enumeration_enabled(i)
    def test_real_members_and_ram_required(self):
        for member in MEMBERS:
            i=fixture();del i.report['kernel']['members'][member]
            with self.subTest(member=member),self.assertRaises(ValueError):h.trackpad_enumeration_enabled(i)
        for member in ['org.puredarwin.driver.PDDARTSurvey','org.puredarwin.driver.PDSerialHID']:
            i=fixture();i.report['kernel']['members'][member]={}
            with self.subTest(member=member),self.assertRaises(ValueError):h.trackpad_enumeration_enabled(i)
        i=fixture();del i.contents['ramdisk']
        with self.assertRaises(ValueError):h.trackpad_enumeration_enabled(i)
    def test_duplicate_keys_and_raw_spi_presence(self):
        i=fixture();i.contents['build_record']=b'{"trackpad_enumeration_experiment":null,"trackpad_enumeration_experiment":null}'
        with self.assertRaises(ValueError):h.trackpad_enumeration_enabled(i)
        from test_native_desktop_progress import fileset
        for ids in [(),(MEMBERS[0],MEMBERS[0])]:
            i=fixture();i.contents['kernel']=fileset(ids)
            with self.assertRaises(ValueError):h.trackpad_enumeration_enabled(i)
    def test_both_controller_and_worker_use_gate(self):
        tree=ast.parse(open(h.__file__).read())
        fs={n.name:n for n in tree.body if isinstance(n,ast.FunctionDef)}
        def calls(n,name):
            return any(isinstance(x,ast.Call) and isinstance(x.func,ast.Name) and x.func.id==name for x in ast.walk(n))
        self.assertTrue(calls(fs['verify_inputs'],'trackpad_enumeration_enabled'))
        self.assertTrue(calls(fs['guest_bootargs'],'trackpad_enumeration_enabled'))
        self.assertTrue(calls(fs['run_worker'],'guest_bootargs'))

if __name__=='__main__':unittest.main()
