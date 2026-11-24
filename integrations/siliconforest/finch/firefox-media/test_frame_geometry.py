# SPDX-License-Identifier: MIT
import unittest
import frame_geometry as g


class Geometry(unittest.TestCase):
    def test_bounds_with_both_orientations(self):
        self.assertEqual(g.parse('1280,800,20,100,960,540,1'),[1280,800,20,100,960,540,1])
        self.assertEqual(g.parse('1280,800,20,100,960,540,-1')[-1],-1)

    def test_refuse_unbounded_ambiguous_or_noncanonical(self):
        for value in ['1280,800,20,100,960,540,0','1280,800,1000,100,960,540,1',
                      '9000,800,20,100,960,540,1','1280,800,-1,100,960,540,1',
                      '1280,800,20,100,960,540','1280,800,20,100,0960,540,1',
                      '1280,800,20,100,960,540,1\n']:
            with self.subTest(value=value),self.assertRaises(ValueError):g.parse(value)


if __name__=='__main__':unittest.main()
