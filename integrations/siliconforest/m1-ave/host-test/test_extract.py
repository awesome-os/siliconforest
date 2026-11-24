# SPDX-License-Identifier: MIT
"""Boundary/parser checks use synthetic bytes only, never private Apple inputs."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('extract',ROOT/'tools/extract-dpe.py')
extract=importlib.util.module_from_spec(spec);spec.loader.exec_module(extract)


class ExtractTests(unittest.TestCase):
    def test_private_paths_reject_traversal_and_symlink_escape(self):
        with tempfile.TemporaryDirectory() as td:
            base=Path(td)/'private';base.mkdir();(base/'escape').symlink_to('/tmp',target_is_directory=True)
            self.assertEqual(extract.private_path(base,base/'good'),base/'good')
            for path in [base,base/'../outside',base/'escape/output']:
                with self.assertRaises(ValueError):extract.private_path(base,path)

    def test_der_rejects_truncation_and_indefinite_lengths(self):
        tool=extract.module()
        for data in [b'',b'\x30',b'\x30\x80',b'\x30\x82\x01',b'\x30\x04x']:
            with self.assertRaises(tool.Refused):tool.der_read(data,0)

    def test_dpe_format_is_little_endian_and_strictly_sized(self):
        tool=extract.module()
        tables={'cat_default':[],'cac_default':[(4,0xff,7)],'cac_8bit':[(8,0xff,9)]}
        data=tool.dpe_encode(tables)
        self.assertEqual(data[:24],b'AVEDPE01'+struct.pack('<4I',0,1,1,0))
        self.assertEqual(tool.dpe_decode(data),tables)
        for invalid in [data[:-1],data+b'x',b'wrongxxx'+data[8:]]:
            with self.assertRaises(tool.Refused):tool.dpe_decode(invalid)


if __name__=='__main__':unittest.main()
