# SPDX-License-Identifier: MIT
"""Synthetic installed-gate refusals; these do not qualify native playback."""
import copy
import importlib.util
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('installed_video',HERE/'analyze-installed-video.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)


def fixtures():
    from video_selection import RUNTIME,FAMILY
    manifest=dict(target=str(RUNTIME),system_layout=True,system_ui=True,system_gpu_providers=True,
        video_override=str(FAMILY/'Frameworks'),video_zero_copy=True,audio_output_clock=True,
        audio_override=str(RUNTIME/'overrides/audio-clock-v1/Frameworks'),
        network_override=str(RUNTIME/'overrides/network-v1/Frameworks'))
    launch=dict(release=manifest,installed_media=True,staged_media=False,frame_mode='require',
                boundary_trace=False,synthetic_input=False)
    def stats(n):return 'kern.sf_avd.stats: jobs '+str(n)+' done '+str(n)+' errors 0 timeouts 0 dart_faults 0 quarantined_desc 0'
    gpu=dict(cpu_upload_bytes=0,cpu_readback_bytes=0,gpu_blit_bytes=0,direct_alias_commits=0)
    clips=[]
    for frames,file,hash in [(600,'pattern-1080p60-h264.mp4','b424348'),(300,'pattern-4k30-hevc.mp4','88999ae5')]:
        audio=str(RUNTIME/'overrides/audio-clock-v1/Frameworks/AudioToolbox.framework/Versions/A')
        images=dict(complete=True,processes=[dict(type=kind,passed=True,images=['0 - 1 CoreVideo <ADF50510-1DC8-3759-ACAF-A2AA1F7D6310> /own/CoreVideo'],selected={
            name:[audio+'/'+name] for name in ['AudioToolbox','AudioToolbox.output-sf']},staged_media_checks={
            name:True for name in ['CoreVideo','VideoToolbox','VideoToolbox.pd']}) for kind in ['browser','rdd']])
        video=dict(source='file:///private/var/db/sffx/media-highrate-v1/'+file,
            quality=[dict(total=frames,dropped=0)],ended=True,finalPixels={'hash':hash},media_providers=images,
            renderer_capture=dict(source='renderer',input_injected=False,owner_input_overlap=False))
        clips.append(dict(video=video,avd_before=stats(0),avd_after=stats(frames),
                          gpu_before=gpu,gpu_after=dict(gpu,gpu_blit_bytes=100)))
    raw=dict(video_measurements=clips,synthetic_input=False,automation_complete=True,
             providers=dict(complete=True,expected_system_gpu=True),
             browser_survived_run=False,measurement_passed=False,exit_before_cleanup=0,exit_after_cleanup=0)
    quit=dict(requested=True,family_before=[123],family_remaining=[],cleanup_signals=[])
    return launch,raw,quit


class InstalledVideoTests(unittest.TestCase):
    def analyze(self,mutate=lambda *x:None,staged=False):
        launch,raw,quit=fixtures();mutate(launch,raw,quit)
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'browser').mkdir()
            for path,value in [('cadence-launch.json',launch),('browser/result.json',raw),('natural-quit.json',quit)]:
                (root/path).write_text(json.dumps(value))
            (root/'browser/firefox.log').write_text('SF_VT_GPU frames=600 copied-bytes=0 required=1\nSF_VT_GPU frames=300 copied-bytes=0 required=1\n')
            (root/'browser/native-output').mkdir();data=b'collected own runtime log\n'
            (root/'browser/native-output/firefox-1-123.log').write_bytes(data)
            (root/'browser/native-logs.json').write_text(json.dumps([dict(path='native-output/firefox-1-123.log',
                bytes=len(data),sha256=hashlib.sha256(data).hexdigest())]))
            return m.analyze(root,staged=staged)

    def test_intentional_quit_keeps_original_survival_failure(self):
        result=self.analyze();self.assertTrue(result['passed'])
        self.assertFalse(result['shared_runner_verdict']['measurement_passed'])
        self.assertIn('REQUIRED',result['visual_review'])

    def test_private_outer_cannot_qualify_as_installed(self):
        self.assertFalse(self.analyze(lambda launch,*_:launch.update(staged_media=True))['passed'])
        self.assertFalse(self.analyze(lambda launch,*_:launch.update(private_quartzcore_copy='on'))['passed'])

    def test_candidate_requires_explicit_scope_and_keeps_graphics_gate(self):
        def candidate(launch,*_):launch.update(staged_media=True,installed_media=False)
        self.assertFalse(self.analyze(candidate)['passed'])
        result=self.analyze(candidate,staged=True)
        self.assertTrue(result['passed'])
        self.assertEqual(result['qualification'],'private media candidate')
        self.assertNotIn('installed_outer',result['checks'])
        def wrong_graphics(launch,raw,*_):
            candidate(launch);raw['providers']['expected_system_gpu']=False
        self.assertFalse(self.analyze(wrong_graphics,staged=True)['passed'])
        self.assertFalse(self.analyze(staged=True)['passed'])

    def test_clock_family_requires_actual_new_uuid(self):
        from video_selection import CLOCK_FAMILY
        def selected(launch,*_):launch['release']['video_override']=str(CLOCK_FAMILY/'Frameworks')
        self.assertFalse(self.analyze(selected)['passed'])
        def actual_clock(launch,raw,*_):
            selected(launch)
            for clip in raw['video_measurements']:
                for process in clip['video']['media_providers']['processes']:
                    process['images']=['0 - 1 CoreVideo <9FB0E7EB-4CFA-35AB-B18F-2C9F21B5AFDE> /own/CoreVideo']
        self.assertTrue(self.analyze(actual_clock)['passed'])

    def test_cleanup_exit_or_live_family_refused(self):
        for mutate in [lambda l,r,q:r.update(exit_before_cleanup=None,exit_after_cleanup=143),
                       lambda l,r,q:q.update(family_remaining=[123]),
                       lambda l,r,q:q.update(cleanup_signals=[15])]:
            with self.subTest(mutate=mutate):self.assertFalse(self.analyze(mutate)['passed'])

    def test_copy_counter_cpu_scene_drops_or_missing_child_refused(self):
        def changed(field,value):
            return lambda l,r,q:r['video_measurements'][0]['video'].update({field:value})
        for mutate in [changed('renderer_capture',dict(source='cpu-scene')),
                       changed('quality',[dict(total=600,dropped=1)]),
                       changed('media_providers',dict(complete=False,processes=[]))]:
            with self.subTest(mutate=mutate):self.assertFalse(self.analyze(mutate)['passed'])
        result=self.analyze(lambda l,r,q:r['video_measurements'][0]['video']['media_providers']['processes'][0]['staged_media_checks'].pop('VideoToolbox.pd'))
        self.assertFalse(result['passed'])


if __name__=='__main__':unittest.main()
