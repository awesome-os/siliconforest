#!/usr/bin/env python3
"""Host-only opt-in tests; no device."""
import json
import unittest
from unittest.mock import Mock, patch
import host_progress as host
from test_native_desktop_progress import inputs, MEMBERS

SURVEY = 'org.puredarwin.driver.PDDARTSurvey'
EXPERIMENT = {'version': 1, 'driver': 'PDDARTSurvey', 'mode': 'read_only',
              'diagnostic_property': 'PDDARTSurveyDiagnosticV1', 'device_initialization': False}

def fixture():
    record = json.loads(inputs().contents['build_record'])
    record['dart_survey_experiment'] = dict(EXPERIMENT)
    return inputs(record, ids=(MEMBERS[0], SURVEY), members=MEMBERS + [SURVEY])

class SurveyPolicy(unittest.TestCase):
    def test_default_off_and_exact_optin(self):
        self.assertFalse(host.dart_survey_enabled(inputs({})))
        self.assertNotIn('pddartsurvey', host.guest_bootargs(inputs()))
        self.assertTrue(host.dart_survey_enabled(fixture()))
        self.assertEqual(host.guest_bootargs(fixture()), host.BOOTARGS + ' pdm1kbd=1 pddartsurvey=1')

    def test_malformed_values_and_unknown_keys(self):
        for key, values in [('version', [True, '1', 0]), ('driver', ['Other', None]),
                            ('mode', ['initialize', None]), ('device_initialization', [0, True]),
                            ('diagnostic_property', ['Other', None]), ('extra', [False])]:
            for value in values:
                i=fixture(); r=json.loads(i.contents['build_record']);r['dart_survey_experiment'][key]=value
                i.contents['build_record']=json.dumps(r).encode()
                with self.subTest(key=key,value=value), self.assertRaises(ValueError):
                    host.dart_survey_enabled(i)
        for value in [None,False,[],1]:
            i=fixture();r=json.loads(i.contents['build_record']);r['dart_survey_experiment']=value
            i.contents['build_record']=json.dumps(r).encode()
            with self.subTest(value=value),self.assertRaises(ValueError):host.dart_survey_enabled(i)

    def test_real_member_and_unique_raw_entry_required(self):
        i=fixture();del i.report['kernel']['members'][SURVEY]
        with self.assertRaises(ValueError):host.dart_survey_enabled(i)
        from test_native_desktop_progress import fileset
        for ids in [(MEMBERS[0],),(MEMBERS[0],SURVEY,SURVEY)]:
            i=fixture();i.contents['kernel']=fileset(ids)
            with self.subTest(ids=ids),self.assertRaises(ValueError):host.dart_survey_enabled(i)

    def test_native_seal_and_no_virtual_producer_required(self):
        for field in ['native_keyboard_desktop_experiment','desktop_experiment']:
            i=fixture();r=json.loads(i.contents['build_record']);del r[field]
            i.contents['build_record']=json.dumps(r).encode()
            with self.subTest(field=field),self.assertRaises(ValueError):host.dart_survey_enabled(i)
        for field in ['keyboard_experiment','virtual_input_experiment','terminal_input_experiment']:
            i=fixture();r=json.loads(i.contents['build_record']);r[field]=None
            i.contents['build_record']=json.dumps(r).encode()
            with self.subTest(field=field),self.assertRaises(ValueError):host.dart_survey_enabled(i)
        i=fixture();i.report['kernel']['members']['org.puredarwin.driver.PDSerialHID']={}
        with self.assertRaises(ValueError):host.dart_survey_enabled(i)

    def test_duplicate_keys_rejected(self):
        i=fixture();i.contents['build_record']=b'{"dart_survey_experiment":null,"dart_survey_experiment":null}'
        with self.assertRaises(ValueError):host.dart_survey_enabled(i)

    def test_pre_reboot_verification_and_worker_use_same_gate(self):
        import ast
        tree=ast.parse(open(host.__file__).read())
        fs={n.name:n for n in tree.body if isinstance(n,ast.FunctionDef)}
        def calls(n,name):return any(isinstance(x,ast.Call) and isinstance(x.func,ast.Name) and x.func.id==name for x in ast.walk(n))
        self.assertTrue(calls(fs['verify_inputs'],'dart_survey_enabled'))
        self.assertTrue(calls(fs['run_worker'],'guest_bootargs'))

if __name__=='__main__':unittest.main()
