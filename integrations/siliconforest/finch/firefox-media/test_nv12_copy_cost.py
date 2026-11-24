# SPDX-License-Identifier: MIT
"""Reject incomplete or unavailable cost evidence; no native performance claims."""
import unittest
import nv12_copy_cost as cost

def fixture():
    rows=[]
    for offset,top in [(0,2048),(512,2112)]:
        for profile in [0x3200,0x4100,0x1000]:
            for range_ in ['video','full']:
                rows.extend(f'SFNV12COPYCOST profile={profile:x} revision={revision} wall_ns=1000000 wall_available=1 process_cpu_us=500 cpu_available=1' for revision in [0,1])
                rows.append(f'SFNV12STRICT parent profile={profile} range={range_} visible=1920x1080 stride=2048 y_offset={offset} top_stride={top} uv_offset=2228224 padded_allocation=3358720 cross_exec=1 local_use=0 global_in_use=0 pass=1')
    return '\n'.join(rows)+'\n'

class Costs(unittest.TestCase):
    def test_second_call_separate_and_unknown_cpu_not_zero(self):
        result=cost.analyze(fixture());self.assertEqual(result['by_revision']['1']['wall_ms']['median'],1)
        result=cost.analyze(fixture().replace('process_cpu_us=500 cpu_available=1','process_cpu_us=0 cpu_available=0'))
        self.assertIsNone(result['by_revision']['1']['process_cpu_ms']);self.assertEqual(result['cpu_available'],0)

    def test_incomplete_duplicate_or_cross_profile_refused(self):
        for text in ['\n'.join(fixture().splitlines()[1:]),
                fixture().replace('profile=3200 revision=1','profile=4100 revision=1',1),
                fixture().replace('range=full','range=video',1)]:
            with self.assertRaises(ValueError):cost.analyze(text)

    def test_counter_reversal_not_a_cost_saving(self):
        with self.assertRaises(ValueError):cost.analyze(fixture().replace('process_cpu_us=500','process_cpu_us=-1',1))
        with self.assertRaises(ValueError):cost.analyze(fixture().replace('wall_ns=1000000','wall_ns=-1',1))

if __name__=='__main__':unittest.main()
