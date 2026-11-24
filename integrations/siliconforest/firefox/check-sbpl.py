#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check permission narrowing in a host-built private SBPL compiler, not kernel enforcement."""
import argparse
import ctypes
import json
import struct
import sys

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('library')
ap.add_argument('--cold-avd-firmware', action='store_true')
args = ap.parse_args()
lib = ctypes.CDLL(args.library)
compile = lib.sbpl_compile
compile.argtypes = [ctypes.c_char_p, ctypes.c_void_p, ctypes.c_char_p,
                    ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_size_t),
                    ctypes.POINTER(ctypes.c_char_p)]
libc = ctypes.CDLL(None)
libc.free.argtypes = [ctypes.c_void_p]


def rules(source):
    blob, size, error = ctypes.c_void_p(), ctypes.c_size_t(), ctypes.c_char_p()
    status = compile(source.encode(), None, None, ctypes.byref(blob), ctypes.byref(size), ctypes.byref(error))
    if status != 0:
        raise RuntimeError(error.value)
    data = ctypes.string_at(blob, size.value)
    libc.free(blob)
    magic, version, default, count, strings_size, reserved = struct.unpack_from('<6I', data)
    assert magic == 0x42535044 and version == 1
    strings = data[24 + count * 16:]
    result = []
    for i in range(count):
        ops, action, filter, _, offset, length = struct.unpack_from('<IBBHII', data, 24 + i * 16)
        path = strings[offset:offset + length].rstrip(b'\0').decode() if length else ''
        result.append((ops, action, filter, path))
    return result


cases = [
    ('explicit', '(allow iokit-open (iokit-connection "IOAccelerator"))', True),
    ('deny', '(deny iokit-open (iokit-connection "IOAccelerator"))', False),
    ('other-service', '(allow iokit-open (iokit-connection "IOAudioEngine"))', False),
    ('other-operation', '(allow mach-lookup (iokit-connection "IOAccelerator"))', False),
    ('unfiltered', '(allow iokit-open)', False),
    ('intersection', '(allow iokit-open (require-all (iokit-connection "IOAccelerator") (literal "/other")))', False),
    ('unchecked-context', '(with-filter (require-all (iokit-connection "other")) (allow iokit-open (iokit-connection "IOAccelerator")))', False),
]
report = []
for name, source, expected in cases:
    try:
        emitted = rules('(version 1) (deny default) ' + source)
    except RuntimeError as error:
        if name != 'unchecked-context':
            raise
        report.append(dict(name=name, passed=True, rejected=str(error)))
        continue
    grants = [r for r in emitted if r[1] == 0 and r[3] == '/dev/sfavd']
    assert bool(grants) == expected, (name, emitted)
    if grants:
        assert grants == [(5, 0, 1, '/dev/sfavd')], grants
    graphics = [r for r in emitted if r[3] in {'/usr/local/lib/mesa-asahi', '/dev/dri', '^/dev/dri/renderD[0-9]+$'}]
    assert bool(graphics) == expected, (name, emitted)
    if graphics:
        assert set(graphics) == {(3, 0, 2, '/usr/local/lib/mesa-asahi'),
                                 (3, 0, 1, '/dev/dri'), (7, 0, 3, '^/dev/dri/renderD[0-9]+$')}
    firmware = [r for r in emitted if r[3] == '/Library/Firmware/apple/avd-fw-v2-t0.bin']
    assert bool(firmware) == (expected and args.cold_avd_firmware), (name, firmware)
    if firmware:
        assert firmware == [(3, 0, 1, '/Library/Firmware/apple/avd-fw-v2-t0.bin')], firmware
    report.append(dict(name=name, passed=True, avd_grants=grants, graphics_grants=graphics, firmware_read_grants=firmware))
font_paths = {'/etc/fonts', '/private/etc/fonts', '/usr/share/fonts',
              '/System/Library/Fonts', '/var/cache/fontconfig', '/private/var/cache/fontconfig'}
font_cases = [
    ('font-server', '(allow mach-lookup (global-name "com.apple.FontObjectsServer"))', True),
    ('fonts-service', '(allow mach-lookup (global-name "com.apple.fonts"))', True),
    ('deny-fonts', '(deny mach-lookup (global-name "com.apple.fonts"))', False),
    ('unrelated-font-name', '(allow mach-lookup (global-name "unrelated.service"))', False),
    ('wrong-font-operation', '(allow iokit-open (global-name "com.apple.fonts"))', False),
    ('font-intersection', '(allow mach-lookup (require-all (global-name "com.apple.fonts") (literal "/other")))', False),
]
for name, source, expected in font_cases:
    emitted = rules('(version 1) (deny default) ' + source)
    grants = [r for r in emitted if r[1] == 0 and r[3] in font_paths]
    assert bool(grants) == expected, (name, emitted)
    if grants:
        assert {r[3] for r in grants} == font_paths
        assert all(r[:3] == (3, 0, 2) for r in grants), grants
    report.append(dict(name=name, passed=True, font_read_grants=grants))
print(json.dumps(dict(scope='host compiler permission checks only', tests=report), indent=2))
