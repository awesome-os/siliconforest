# SPDX-License-Identifier: MIT
"""Synthetic refusal/clock controls only; never runtime or audible evidence."""
import copy
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('audio_state_analysis',Path(__file__).with_name('analyze-audio-state.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)


def fixture():
    names=['full','half','muted','unmuted','paused','restarted','two-tabs',
           'second-with-first-paused','second-with-first-closed','all-media-tabs-closed']
    phases=[dict(name=name,start=100+3*i,end=102+3*i) for i,name in enumerate(names)]
    result=dict(diagnostic_complete=True,state_controls=dict(phases=phases),
                timeline=[dict(epoch=1000000,monotonic=100)],before_cleanup_owned=[],cleanup_signals=[])
    text=['FXHAL pid=7 monotonic=90 epoch=1000000']
    count=0;power=0
    for phase in phases:
        silent=phase['name'] in ['muted','paused','all-media-tabs-closed']
        density=0 if silent else (1e-6 if phase['name']=='full' else .25e-6)
        for n in range(20):
            count+=100;power+=100*density
            text.append('SFHAL_PCM pid=7 context=0 monotonic=%.6f calls=%d frames=%d samples=%d square-sum=%.17g peak=.001 errors=0 bad-buffers=0 nonfinite=0 last-host=1'%
                (phase['start']+n/10-10,count//100,count//2,count,power))
    return result,'\n'.join(text)


class AnalysisTests(unittest.TestCase):
    def test_installed_clock_gate_requires_real_selector_images_hal_and_quit(self):
        r,t=fixture()
        r.update(installed_outer_launcher=True,expected_installed_clock=True,normalized_audio_family=False,
                 parent_pid=7,exit_before_cleanup=0,launcher_final_exit=0,media_providers={'complete':True},
                 selected_manifest={'audio_output_clock':True,'audio_override':
                     '/usr/local/lib/siliconforest/firefox/160-system1/overrides/audio-clock-v1/Frameworks'})
        t+='\nSF_AU_CLOCK normalized=1 device-fixed-frames=8192\nSFMEDIA_RING pid=7 rate=48000 write=8192 read=4096\n'
        self.assertTrue(m.analyze(r,t,True)['installed_clock_gate']['passed'])
        for changes in [dict(exit_before_cleanup=139),dict(launcher_final_exit=139),dict(expected_installed_clock=False),
                        dict(normalized_audio_family=True),dict(media_providers={'complete':False}),
                        dict(selected_manifest={}),dict(before_cleanup_owned=[7]),dict(cleanup_signals=[{'pid':7,'signal':15}])]:
            bad=copy.deepcopy(r);bad.update(changes)
            with self.subTest(changes=changes):self.assertFalse(m.analyze(bad,t,True)['output_boundary_passed'])
        for old,new in [('normalized=1','normalized=0'),('SFMEDIA_RING pid=7','SFMEDIA_RING pid=8'),
                        ('rate=48000','rate=44100'),('write=8192','write=0'),('read=4096','read=0')]:
            with self.subTest(old=old):self.assertFalse(m.analyze(r,t.replace(old,new),True)['output_boundary_passed'])

    def test_meter_can_supply_its_own_clock_bridge(self):
        r,t=fixture();lines=[]
        for line in t.splitlines()[1:]:
            f=m.fields(line);epoch=float(f['monotonic'])+999910
            lines.append(line+' epoch=%.6f'%epoch)
        a=m.analyze(r,'\n'.join(lines))
        self.assertTrue(a['output_boundary_passed'])
        self.assertAlmostEqual(a['native_to_controller_seconds'][7],10)
    def test_clock_bridge_and_quarter_power(self):
        r,t=fixture();a=m.analyze(r,t)
        self.assertTrue(a['output_boundary_passed']);self.assertAlmostEqual(a['half_to_full_power_ratio'],.25)
        self.assertAlmostEqual(a['native_to_controller_seconds'][7],10)
        self.assertFalse(a['acoustic_proof']);self.assertFalse(a['glitch_free_proof'])
    def test_missing_meter_refused(self):
        r,t=fixture()
        with self.assertRaises(ValueError):m.analyze(r,t.splitlines()[0])
    def test_wrong_native_clock_cannot_look_silent(self):
        r,t=fixture()
        with self.assertRaises(ValueError):m.analyze(r,t.replace('epoch=1000000','epoch=1000020'))
    def test_missing_bridge_refused(self):
        r,t=fixture()
        with self.assertRaises(ValueError):m.analyze(r,'\n'.join(t.splitlines()[1:]))
    def test_unstable_controller_clock_refused(self):
        r,t=fixture();r['timeline'].append(dict(epoch=1000001,monotonic=100))
        with self.assertRaises(ValueError):m.analyze(r,t)
    def test_invalid_pcm_refused(self):
        r,t=fixture()
        for field in ['errors','bad-buffers','nonfinite']:
            with self.subTest(field=field),self.assertRaises(ValueError):m.analyze(r,t.replace(field+'=0',field+'=1'))
    def test_incomplete_control_refused(self):
        r,t=fixture();r['state_controls']['phases'].pop()
        with self.assertRaises(ValueError):m.analyze(r,t)
    def test_nonfinite_power_refused(self):
        r,t=fixture()
        with self.assertRaises(ValueError):m.analyze(r,t.replace('square-sum=0.0001','square-sum=nan',1))
    def test_cleanup_does_not_prove_natural_exit(self):
        r,t=fixture();r['cleanup_signals']=[dict(pid=7,signal=15)]
        self.assertFalse(m.analyze(r,t)['natural_exit_without_cleanup_signals'])
    def test_browser_error_refused(self):
        r,t=fixture();r['error']='incomplete browser'
        with self.assertRaises(ValueError):m.analyze(r,t)


if __name__=='__main__':unittest.main()
