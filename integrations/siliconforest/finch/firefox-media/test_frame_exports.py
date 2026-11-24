# SPDX-License-Identifier: MIT
import unittest
import re
import frame_export_analysis as a
import frame_marker as m


def row(sequence,frame,time,pid=100):
    bits=m.bits(frame);levels=[224 if x else 32 for x in bits]+[32 if x else 224 for x in bits]
    return f'FXFRAMEID pid={pid} sequence={sequence} surface=3 size=1920x1472 fbo=1 before_ms={time} after_ms={time+.1} readback_bytes=10368 frame={frame} gl_error=0 reason=ok rows='+','.join(map(str,levels))+'\n'


class Export(unittest.TestCase):
    @staticmethod
    def buffered():
        text=''.join(row(i,frame,i*7500).replace('readback_bytes=','log_buffered=1 readback_bytes=')
                     for i,frame in enumerate([0,1,899]))
        return text+f'FXFRAMEID_BUFFER explicit_flush=1 pid=100 records=3 bytes={len(text.encode())} write_return={len(text.encode())} write_errno=0 write_ms=1 overflow=0 format_sum_ms=.03 format_max_ms=.01 hook_sum_ms=.4 hook_max_ms=.2\n'

    def test_all900_native_ids(self):
        result=a.analyze(''.join(row(i,i,i*1000/60) for i in range(900)),100)
        self.assertTrue(result['all900_ids_observed']);self.assertAlmostEqual(result['diagnostic_distinct_id_hz'],60)
        self.assertFalse(result['panel_delivery_proved']);self.assertFalse(result['production_performance_qualified'])

    def test_sparse_ids_cannot_be_all_frames(self):
        result=a.analyze(row(0,0,0)+row(1,2,1000/30)+row(2,899,15000),100)
        self.assertFalse(result['all900_ids_observed']);self.assertEqual(result['distinct_source_ids'],3)
        self.assertEqual(len(result['missing_source_ids']),897)

    def test_preroll_and_after_end_duplicates_excluded(self):
        result=a.analyze(row(0,0,0)+row(1,0,1000)+row(2,1,1017)+row(3,899,16000)+row(4,899,20000),100)
        self.assertEqual(result['first_to_last_identity_span_ms'],15000)

    def test_native_raw_disagreement_refused(self):
        with self.assertRaises(ValueError):a.analyze(row(0,0,0).replace('frame=0','frame=1'),100)

    def test_foreign_pid_does_not_fill_missing_own_id(self):
        result=a.analyze(row(0,0,0)+row(0,1,10,pid=101)+row(1,899,15000),100)
        self.assertEqual(result['foreign_pid_rows'],1);self.assertIn(1,result['missing_source_ids'])

    def test_regressions_retained(self):
        result=a.analyze(row(0,0,0)+row(1,2,16)+row(2,1,33)+row(3,899,15000),100)
        self.assertEqual(result['regressions'],1)

    def test_clock_and_empty_span_refused(self):
        with self.assertRaises(ValueError):a.analyze(row(0,0,20)+row(1,1,10),100)
        with self.assertRaises(ValueError):a.analyze('',100)
        self.assertFalse(a.analyze(row(0,1,0)+row(1,899,15000),100)['identity_span_complete'])

    def test_previous_write_cost_keeps_boundary_unknown(self):
        text=row(0,0,0).replace('readback_bytes=','previous_emit_ms=0 readback_bytes=')
        text+=row(1,899,15000).replace('readback_bytes=','previous_emit_ms=2.5 readback_bytes=')
        result=a.analyze(text,100)
        self.assertEqual(result['previous_log_write_cost_ms_excluding_last']['count'],1)
        self.assertEqual(result['previous_log_write_cost_ms_excluding_last']['p50'],2.5)

    def test_complete_deferred_log_and_preview_separate(self):
        preview=row(0,0,0).replace('FXFRAMEID ','FXFRAMEID_PREVIEW ')
        result=a.analyze(preview+self.buffered(),100)
        self.assertEqual(result['samples'],3);self.assertTrue(result['buffered_log_receipt']['complete'])
        self.assertIsNone(result['previous_log_write_cost_ms_excluding_last'])

    def test_short_missing_overflow_or_foreign_dump_refused(self):
        text=self.buffered()
        for bad in [text.rsplit('FXFRAMEID_BUFFER',1)[0],text.replace('records=3','records=2'),
                    re.sub(r'write_return=\d+','write_return=1',text),
                    text.replace('overflow=0','overflow=1'),text.replace('explicit_flush=1 ','dir=old '),text.replace('write_errno=0','write_errno=5'),
                    text.replace('explicit_flush=1 pid=100','explicit_flush=1 pid=101')]:
            with self.assertRaises(ValueError):a.analyze(bad,100)

    def test_mixed_deferred_and_live_rows_refused(self):
        with self.assertRaises(ValueError):a.analyze(self.buffered().replace('log_buffered=1 ','',1),100)


if __name__=='__main__':unittest.main()
