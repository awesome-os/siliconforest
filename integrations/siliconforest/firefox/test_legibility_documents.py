#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Host coordinate/cleanup controls; cross-origin native execution is separate."""
import unittest
from legibility_documents import document, translate_runs


class FrameCoordinates(unittest.TestCase):
    def test_child_run_translates_into_root_viewport(self):
        run = dict(text='Consent label', boxes=[dict(x=2, y=3, width=20, height=10)])
        result = translate_runs([run], [100, 200], [100, 200, 500, 500])
        self.assertEqual(result[0]['boxes'], [dict(x=102, y=203, width=20, height=10)])
        self.assertEqual(run['boxes'][0]['x'], 2)

    def test_clipped_text_is_not_counted_as_a_complete_run(self):
        run = dict(text='Clipped label', boxes=[dict(x=2, y=3, width=20, height=10)])
        self.assertEqual(translate_runs([run], [100, 200], [110, 200, 500, 500]), [])

    def test_han_samples_translate_and_clip_with_the_child_document(self):
        run=dict(text='日本語',boxes=[dict(x=2,y=3,width=20,height=10)],han_samples=[
            dict(text='日',boxes=[dict(x=2,y=3,width=8,height=10)]),
            dict(text='語',boxes=[dict(x=30,y=3,width=8,height=10)])])
        result=translate_runs([run],[100,200],[100,200,125,250])[0]
        self.assertEqual(result['han_samples'],[
            dict(text='日',boxes=[dict(x=102,y=203,width=8,height=10)])])
        self.assertEqual(run['han_samples'][0]['boxes'][0]['x'],2)

    def test_frame_failure_restores_top_document(self):
        class Client:
            calls = []
            def command(self, name, args):
                self.calls.append((name, args))
                if args['id'] == 'gone':
                    raise RuntimeError('Frame navigated')
        client = Client()
        with self.assertRaises(RuntimeError):
            with document(client, ['parent', 'gone']):
                self.fail('Must not enter a missing frame')
        self.assertEqual(client.calls[-1], ('WebDriver:SwitchToFrame', {'id': None}))


if __name__ == '__main__':
    unittest.main()
