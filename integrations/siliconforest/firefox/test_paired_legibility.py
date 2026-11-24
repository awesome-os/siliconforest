#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Controls for restoring the owned profile after failed paint measurements."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import paired_legibility

spec = importlib.util.spec_from_file_location('compare_legibility', Path(__file__).with_name('compare-legibility.py'))
compare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare)


class Client:
    def __init__(self, previous):
        self.previous = previous
        self.chrome_results = iter([previous, True, False, previous])
        self.scripts = []

    def command(self, name, args=None):
        if name == 'WebDriver:ExecuteScript':
            self.scripts.append(args['script'])
            return next(self.chrome_results)
        if name == 'WebDriver:GetWindowHandles':
            return ['own-tab']
        if name == 'WebDriver:GetCurrentURL':
            return 'https://example.com/'


class PreferenceRestoration(unittest.TestCase):
    def test_success_restores_absent_preference(self):
        client = Client(dict(user=False, value=True))
        with tempfile.TemporaryDirectory() as root, patch.object(paired_legibility, 'measure_pages', return_value=dict(complete=True)):
            result = paired_legibility.measure(client, Path(root))
            self.assertTrue(result['complete'])
            self.assertFalse(result['legibility_qualified'])
            self.assertIn('clearUserPref', client.scripts[-1])
            self.assertEqual(result['restored_preference'], client.previous)

    def test_failed_second_phase_restores_explicit_false_preference(self):
        client = Client(dict(user=True, value=False))
        with tempfile.TemporaryDirectory() as root, patch.object(paired_legibility, 'measure_pages', side_effect=[dict(complete=True), RuntimeError('Capture failed')]):
            with self.assertRaisesRegex(RuntimeError, 'Capture failed'):
                paired_legibility.measure(client, Path(root))
            self.assertIn('setBoolPref("gfx.downloadable_fonts.enabled",false)', client.scripts[-1])
            self.assertTrue((Path(root)/'paired-legibility.json').exists())

    def test_nonce_normalization_preserves_real_search_parameters(self):
        key = compare.page_key
        self.assertEqual(key('https://www.reddit.com/?js_challenge=1&solution=one&q=firefox#top'),
                         key('https://www.reddit.com/?js_challenge=1&solution=two&q=firefox#top'))
        self.assertNotEqual(key('https://www.reddit.com/?q=one'), key('https://www.reddit.com/?q=two'))
        challenge = 'https://www.google.com/sorry/index?continue=search&solution=one'
        self.assertEqual(key(challenge), challenge)
        ordinary = 'https://www.reddit.com/?solution=ordinary'
        self.assertEqual(key(ordinary), ordinary)


if __name__ == '__main__':
    unittest.main()
