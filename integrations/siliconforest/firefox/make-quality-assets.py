#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate original ICC-tagged pixel fixtures; never acquires system profiles."""
import argparse
import ctypes as C
from ctypes.util import find_library
from pathlib import Path
from PIL import Image


class xyY(C.Structure):
    _fields_ = [('x', C.c_double), ('y', C.c_double), ('Y', C.c_double)]


class Primaries(C.Structure):
    _fields_ = [('red', xyY), ('green', xyY), ('blue', xyY)]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('output', type=Path)
    args = ap.parse_args()
    args.output.mkdir(exist_ok=False)
    lib = C.CDLL(find_library('lcms2'))
    lib.cmsBuildGamma.argtypes = [C.c_void_p, C.c_double]
    lib.cmsBuildGamma.restype = C.c_void_p
    lib.cmsCreateRGBProfile.argtypes = [C.POINTER(xyY), C.POINTER(Primaries), C.POINTER(C.c_void_p)]
    lib.cmsCreateRGBProfile.restype = C.c_void_p
    lib.cmsCreate_sRGBProfile.restype = C.c_void_p
    lib.cmsSaveProfileToMem.argtypes = [C.c_void_p, C.c_void_p, C.POINTER(C.c_uint32)]
    lib.cmsSaveProfileToMem.restype = C.c_int
    lib.cmsCloseProfile.argtypes = [C.c_void_p]
    lib.cmsFreeToneCurve.argtypes = [C.c_void_p]
    white = xyY(.3127, .3290, 1)
    primary = Primaries(xyY(.64, .33, 1), xyY(.30, .60, 1), xyY(.15, .06, 1))
    gamma = lib.cmsBuildGamma(None, 1.0)
    if not gamma:
        raise RuntimeError('Cannot create linear tone curve')
    curves = (C.c_void_p * 3)(gamma, gamma, gamma)
    linear = lib.cmsCreateRGBProfile(C.byref(white), C.byref(primary), curves)
    lib.cmsFreeToneCurve(gamma)
    srgb = lib.cmsCreate_sRGBProfile()
    for name, profile in [('linear-rgb', linear), ('srgb', srgb)]:
        if not profile:
            raise RuntimeError('Cannot create ' + name)
        size = C.c_uint32()
        if not lib.cmsSaveProfileToMem(profile, None, C.byref(size)):
            raise RuntimeError('Cannot size ICC profile')
        data = C.create_string_buffer(size.value)
        if not lib.cmsSaveProfileToMem(profile, data, C.byref(size)):
            raise RuntimeError('Cannot serialize ICC profile')
        encoded = data.raw[:size.value]
        (args.output / (name + '.icc')).write_bytes(encoded)
        Image.new('RGB', (16, 16), (128, 128, 128)).save(args.output / (name + '.png'), icc_profile=encoded)
        lib.cmsCloseProfile(profile)


if __name__ == '__main__':
    main()
