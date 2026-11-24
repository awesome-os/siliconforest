#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Host admission refusals, not native GPU or performance evidence."""
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('strict_nv12_native',
    Path(__file__).with_name('strict-nv12-native.py'))
native=importlib.util.module_from_spec(spec);spec.loader.exec_module(native)

ODD=dict(width=65,height=33,stride=128,uv_offset=6144,allocation=16384)
HD=dict(width=1920,height=1080,stride=2048,uv_offset=2228224,allocation=3358720)
UHD=dict(width=3840,height=2160,stride=4096,uv_offset=8880128,allocation=13336576)

def fixture(layout,byte_count):
    rows=[]
    for offset,top_stride in [(0,layout['stride']),(512,layout['stride']+64)]:
        for profile in [0x3200,0x4100,0x1000]:
            for range_ in ['video','full']:
                rows.append(f'SFNV12STRICT child profile={profile:x} exact_bytes={byte_count} cpu_upload=0 cpu_readback=0 gpu_blit={byte_count} reuse_after_completion=1 state_restored=9 paired_copy=1 local_use=0 global_in_use=0 pass=1')
                rows.append(f'SFNV12STRICT parent profile={profile} range={range_} visible={layout["width"]}x{layout["height"]} stride={layout["stride"]} y_offset={offset} top_stride={top_stride} uv_offset={layout["uv_offset"]} padded_allocation={layout["allocation"]} cross_exec=1 local_use=0 global_in_use=0 pass=1')
    return '\n'.join(rows+['SFNV12STRICT malformed_layouts_rejected=4 cases=12 pass=1'])+'\n'

class Admission(unittest.TestCase):
    def test_bounded_geometries_and_exact_both_revisions(self):
        for layout,expected in [(ODD,6534),(HD,6220800),(UHD,24883200)]:
            self.assertTrue(all(native.plane_checks(fixture(layout,expected),layout).values()))
        self.assertEqual(native.probe_layout(),ODD)

    def test_tiny_control_cannot_qualify_large_surface(self):
        self.assertFalse(all(native.plane_checks(fixture(ODD,6534),UHD).values()))

    def test_one_revision_or_one_plane_cannot_pass(self):
        self.assertFalse(native.plane_checks(fixture(HD,3110400),HD)['exact_all_bytes'])
        text=fixture(UHD,24883200).replace('gpu_blit=24883200','gpu_blit=16588800',1)
        self.assertFalse(native.plane_checks(text,UHD)['exact_gpu_blits'])

    def test_wrong_pitch_or_duplicate_range_is_not_coverage(self):
        text=fixture(HD,6220800).replace('top_stride=2112','top_stride=2048',1)
        self.assertFalse(native.plane_checks(text,HD)['distinct_layout_range_profiles'])
        text=fixture(HD,6220800).replace('range=full','range=video',1)
        self.assertFalse(native.plane_checks(text,HD)['distinct_layout_range_profiles'])

    def test_cpu_fallback_and_missing_child_refused(self):
        text=fixture(HD,6220800).replace('cpu_upload=0','cpu_upload=3110400',1)
        self.assertFalse(native.plane_checks(text,HD)['cpu_transport_zero'])
        text='\n'.join(fixture(HD,6220800).splitlines()[1:])+'\n'
        self.assertFalse(native.plane_checks(text,HD)['twelve_children'])

    def test_unbounded_or_inconsistent_metadata_refused(self):
        for layout in [dict(UHD,width=8192),dict(HD,allocation=1),dict(ODD,stride=65)]:
            with self.assertRaises(ValueError):native.probe_layout(layout)

if __name__=='__main__':unittest.main()
