# SPDX-License-Identifier: MIT
"""Synthetic counter/source refusals only, not browser or presentation proof."""
import copy,importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('pair',Path(__file__).with_name('analyze-video-pair.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

def fixture():
    stats='kern.sf_avd.stats: jobs 0 done 0 errors 0 timeouts 0 dart_faults 0 quarantined_desc 0'
    gpu={k:0 for k in ['cpu_upload_bytes','cpu_readback_bytes','gpu_blit_bytes','direct_alias_commits']}
    return dict(video=dict(source='file:///private/var/db/sffx/media-highrate-v1/pattern-1080p60-h264.mp4',quality=[{'total':600,'dropped':0}]),
                avd_before=stats,avd_after=stats,gpu_before=gpu,gpu_after=dict(gpu))

class PairTests(unittest.TestCase):
    def test_shared_footer_duplicates_are_not_added(self):
        row=m.case_summary(fixture(),[(600,0,1)]*4,'shared')
        self.assertEqual(row['vt_counter']['zero_copy_frames'],600)
        self.assertEqual(row['vt_counter']['pixel_copies_per_decoded_frame'],0)
        self.assertEqual(len(row['vt_counter_candidates']),1)
    def test_copy_frames_field_zero_is_not_zero_copies(self):
        row=m.case_summary(fixture(),[(0,1866240000,0)]*2+[(0,3732480000,0)]*2,'copy')
        self.assertEqual(row['vt_counter']['zero_copy_frames'],0)
        self.assertEqual(row['vt_counter']['pixel_copies_per_decoded_frame'],1)
    def test_missing_or_wrong_mode_counter_is_unknown(self):
        for rows in [[],[(0,0,0)],[(600,0,0)],[(599,0,1)]]:
            with self.subTest(rows=rows):self.assertIsNone(m.case_summary(fixture(),rows,'shared')['vt_counter'])
    def test_cpu_scene_is_not_renderer_proof(self):
        case=fixture();case['video']['renderer_capture']={'returncode':0,'file':'renderer.ppm'}
        self.assertFalse(m.case_summary(case,[],'copy')['actual_renderer_source_observed'])
        case['video']['renderer_capture']['source']='renderer'
        self.assertTrue(m.case_summary(case,[],'copy')['actual_renderer_source_observed'])
    def test_counter_reset_refused(self):
        case=fixture();case['gpu_after']['gpu_blit_bytes']=-1
        with self.assertRaises(ValueError):m.case_summary(case,[],'copy')
    def test_unfrozen_stream_refused(self):
        for source in ['https://unowned.example/pattern-1080p60-h264.mp4','file:///other/pattern-1080p60-h264.mp4',
                       'file:///private/var/db/sffx/../other/pattern-1080p60-h264.mp4']:
            case=fixture();case['video']['source']=source
            with self.subTest(source=source),self.assertRaises(ValueError):m.case_summary(case,[],'copy')

if __name__=='__main__':unittest.main()
