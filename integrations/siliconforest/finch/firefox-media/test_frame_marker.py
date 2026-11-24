# SPDX-License-Identifier: MIT
"""Marker protocol controls, independent of target/frame-rate evidence."""
import unittest
import frame_marker as m


def levels(frame):
    bits=m.bits(frame)
    return [[224 if value else 32 for value in bits],[32 if value else 224 for value in bits]]


class Marker(unittest.TestCase):
    def test_all900_ids_round_trip(self):
        for frame in range(900):self.assertEqual(m.decode_levels(levels(frame)),frame)

    def test_adjacent_gray_codes_change_one_bit(self):
        for frame in range(899):self.assertEqual(sum(a!=b for a,b in zip(m.bits(frame)[4:14],m.bits(frame+1)[4:14])),1)

    def test_ambiguous_level_refused(self):
        rows=levels(123);rows[0][9]=128
        with self.assertRaises(ValueError):m.decode_levels(rows)

    def test_mixed_rows_refused(self):
        with self.assertRaises(ValueError):m.decode_levels([levels(123)[0],levels(124)[1]])

    def test_signature_and_invalid_size_refused(self):
        rows=levels(123);rows[0][0]=32;rows[1][0]=224
        with self.assertRaises(ValueError):m.decode_levels(rows)
        with self.assertRaises(ValueError):m.decode_levels([[32]*18])

    def test_out_of_range_refused(self):
        for frame in [-1,900,1023]:
            with self.assertRaises(ValueError):m.bits(frame)


if __name__=='__main__':unittest.main()
