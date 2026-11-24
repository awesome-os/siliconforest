#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Host evidence controls; no browser, desktop, or acoustic claim."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace
from lifecycle_observer import close_button, window_button
import provider_images

spec = importlib.util.spec_from_file_location('lifecycle_gate', Path(__file__).with_name('analyze-lifecycle.py'))
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)
spec = importlib.util.spec_from_file_location('lifecycle_run', Path(__file__).with_name('lifecycle-run.py'))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class PrivateUISelection(unittest.TestCase):
    def test_system_cohort_gets_explicit_private_precedence(self):
        runtime = Path('/usr/local/lib/siliconforest/firefox/160-system1')
        prefix = Path('/private/var/db/sffx/quit-candidate')
        env = {'DYLD_FRAMEWORK_PATH': '/media:/System/Library/Frameworks',
               'DYLD_LIBRARY_PATH': '/canonical-gl:/usr/lib', 'MOZ_CRASHREPORTER_DISABLE': '1'}
        selected = runner.private_ui_environment(env, runtime, prefix)
        self.assertEqual(selected['DYLD_FRAMEWORK_PATH'], str(prefix / 'appkit') + ':' + env['DYLD_FRAMEWORK_PATH'])
        self.assertEqual(selected['DYLD_LIBRARY_PATH'], str(prefix / 'windowserver') + ':' + env['DYLD_LIBRARY_PATH'])
        self.assertEqual(selected['MOZ_CRASHREPORTER_DISABLE'], '1')
        self.assertEqual(env['DYLD_FRAMEWORK_PATH'], '/media:/System/Library/Frameworks')

    def test_legacy_and_repeated_selection_do_not_leave_competing_private_ui(self):
        runtime, prefix = Path('/runtime'), Path('/candidate')
        env = {'DYLD_FRAMEWORK_PATH': '/media:/runtime/appkit:/candidate/appkit:/System/Library/Frameworks',
               'DYLD_LIBRARY_PATH': '/runtime/windowserver:/canonical-gl:/usr/lib'}
        selected = runner.private_ui_environment(env, runtime, prefix)
        self.assertEqual(selected['DYLD_FRAMEWORK_PATH'], '/candidate/appkit:/media:/System/Library/Frameworks')
        self.assertEqual(selected['DYLD_LIBRARY_PATH'], '/candidate/windowserver:/canonical-gl:/usr/lib')
        self.assertEqual(runner.private_ui_environment(selected, runtime, prefix), selected)

    def test_sampled_system_windowserver_cannot_qualify_private_candidate(self):
        prefix = '/private/var/db/sffx/quit-candidate'
        selected = prefix + '/windowserver/libWindowServer.dylib'
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'providers.json'
            actual_ws = [selected]

            def command(argv, **kwargs):
                if argv[0] == 'ps':
                    return SimpleNamespace(stdout=('start\n' if argv[-1] == 'lstart='
                        else 'Firefox GPU Helper gpu-helper.app/Contents/MacOS/Firefox GPU Helper\n'
                        if argv[2] == '22' else 'firefox\n'), returncode=0)
                Path(argv[-1]).write_text('\nBinary Images:\n'
                    '0x1 AppKit <UUID> ' + prefix + '/appkit/AppKit.framework/Versions/C/AppKit\n'
                    '0x2 libWindowServer.dylib <UUID> ' + actual_ws[0] + '\n')
                return SimpleNamespace(stdout='', returncode=0)

            with patch.object(provider_images.subprocess, 'run', side_effect=command):
                def inspect():
                    return provider_images.inspect('/private/var/db/sffx/sf-sample', output,
                        {11: 'start', 22: 'start'}, 11, prefix + '/appkit',
                        private_windowserver=selected)
                self.assertTrue(inspect()['complete'])
                actual_ws[0] = '/usr/lib/libWindowServer.dylib'
                self.assertFalse(inspect()['complete'])


class LifecycleEvidence(unittest.TestCase):
    def setUp(self):
        self.result = dict(action='close-box', installed_outer_or_dock=True,
            action_monotonic=100, after_action_monotonic=106, cleanup_monotonic=109,
            before_action_owned_identities={'11': 'parent', '22': 'content'},
            after_action_owned_alive={'11': 'parent'}, before_content_identities={'22': 'content'},
            admission={'window_id': 9}, after_action_native_windows='stack []\n',
            after_reopen_windows=[{'tabs': [{'uri': 'about:newtab'}]}],
            reopen_admission={'complete': True}, reopen_route='compositor-app-request',
            document={'url': 'http://127.0.0.1/test'}, diagnostic_complete=True,
            providers={'complete': True})
        self.log = [
            'SFMEDIA_AU phase=state pid=11 event=start unit=0x123 status=0 monotonic=99',
            'SFMEDIA_CALLBACK pid=11 unit=0x123 monotonic=99.5 calls=8 successes=8 active=1',
            'SFMEDIA_AU phase=state pid=11 event=stop unit=0x123 status=0 monotonic=100.2',
        ] + ['SFMEDIA_CALLBACK pid=11 unit=0x123 monotonic=%s calls=10 successes=10 active=0' % t
             for t in [101.2, 101.7, 102.2, 102.7, 103.2]]

    def analyze(self, result=None, log=None):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'result.json').write_text(json.dumps(self.result if result is None else result))
            (root / 'firefox.log').write_text('\n'.join(self.log if log is None else log))
            return gate.analyze(root)

    def test_closed_media_and_compositor_reopen(self):
        self.assertTrue(self.analyze()['complete'])
        for route in [None, 'app-menu-socket']:
            with self.subTest(route=route):
                self.result['reopen_route'] = route
                self.assertFalse(self.analyze()['complete'])

    def test_invisible_or_duplicate_reopen_fails(self):
        for changes in [dict(reopen_admission={}),
                        dict(after_reopen_windows=[{'tabs': []}, {'tabs': []}]),
                        dict(after_action_native_windows='stack [9]\nwindow 9 mapped=true\n')]:
            with self.subTest(changes=changes):
                self.assertFalse(self.analyze(dict(self.result, **changes))['complete'])

    def test_observer_loss_or_late_stop_does_not_prove_silence(self):
        self.assertFalse(self.analyze(log=self.log[:2])['complete'])
        self.assertFalse(self.analyze(log=[line.replace('monotonic=100.2', 'monotonic=110')
                                          for line in self.log])['complete'])

    def test_closed_content_must_end_before_cleanup(self):
        self.result['after_action_owned_alive']['22'] = 'content'
        self.assertFalse(self.analyze()['complete'])

    def test_compositor_quit_requires_success_and_whole_family_exit(self):
        result = dict(self.result, action='desktop-quit', after_action_owned_alive={},
                      exit_before_cleanup=0, native_action={'route': 'compositor-app-request'})
        self.assertTrue(self.analyze(result, self.log[:2])['complete'])
        for change in [dict(exit_before_cleanup=139), dict(native_action={}),
                       dict(after_action_owned_alive={'22': 'content'})]:
            with self.subTest(change=change):
                self.assertFalse(self.analyze(dict(result, **change), self.log[:2])['complete'])

    def test_hidden_media_continues_and_reopens_original_tab(self):
        result = copy.deepcopy(self.result)
        result.update(action='hide', after_action_owned_alive={'11': 'parent', '22': 'content'},
            after_action_native_windows='stack []\nwindow 9 mapped=true lifecycle=Hidden\n',
            after_reopen_windows=[{'tabs': [{'uri': result['document']['url']}]}])
        log = self.log[:2] + [
            'SFMEDIA_CALLBACK pid=11 unit=0x123 monotonic=%s calls=%s successes=%s active=1' % (t, n, n)
            for t, n in [(101.2, 10), (101.7, 20), (102.2, 30), (102.7, 40)]]
        self.assertTrue(self.analyze(result, log)['complete'])
        result['after_reopen_windows'][0]['tabs'][0]['uri'] = 'about:newtab'
        self.assertFalse(self.analyze(result, log)['complete'])

    def test_windowless_quit_must_really_follow_window_close(self):
        result = dict(self.result, action='windowless-quit', after_action_owned_alive={},
            exit_before_cleanup=0, native_action={'route': 'compositor-app-request'},
            prequit_native_windows='stack []\n')
        self.assertTrue(self.analyze(result, self.log[:2])['complete'])
        for dump in ['', 'stack [9]\nwindow 9 mapped=true\n']:
            self.assertFalse(self.analyze(dict(result, prequit_native_windows=dump), self.log[:2])['complete'])


class CloseButtonGeometry(unittest.TestCase):
    row = ('window 92 mapped=true frame=Some(Rect { pos: Point { x: 112, y: 59 }, '
           'size: Size { w: 1942, h: 1541 } }) '
           'buttons=[("Close", 2014, 92), ("Maximize", 1972, 92)]')

    def test_minimize_uses_its_own_observed_button(self):
        row=self.row.replace('("Maximize", 1972, 92)', '("Miniaturize", 1930, 92)')
        self.assertEqual(window_button(row,92,'Miniaturize'),(1930,92))
        with self.assertRaises(ValueError):window_button(self.row,92,'Miniaturize')

    def test_observed_compositor_center(self):
        self.assertEqual(close_button(self.row, 92), (2014, 92))

    def test_foreign_hidden_ambiguous_or_offscreen_target_refused(self):
        for dump in [self.row.replace('window 92', 'window 93'),
                     self.row.replace('mapped=true', 'mapped=false'),
                     self.row + '\n' + self.row,
                     self.row.replace('2014, 92', '2600, 92'),
                     self.row.replace('"Close"', '"Miniaturize"')]:
            with self.subTest(dump=dump):
                with self.assertRaises(ValueError):
                    close_button(dump, 92)


if __name__ == '__main__':
    unittest.main()
