#!/usr/bin/env python3
"""Focused host-only checks for the T1 trackpad enumeration adapter.

Source scope against frozen keyboard rev005, ordering/forbidden-path checks,
exact patch replay, and the real extracted adapter bodies compiled against
host stand-ins plus the core helpers. No SDK, IOKit, MMIO or hardware.
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
ENUM = HERE.parent
KBD = ENUM.parent / 'm1-keyboard'
FILES = ('CMakeLists.txt', 'PDM1SPIHID.h', 'PDM1SPIHID.cpp')
MANIFEST = json.loads((HERE / 'manifest.json').read_text())
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()


def functions(text):
    """Map 'PDM1SPIHID::name' / static helper names to exact definition text."""
    out = {}
    for m in re.finditer(r'^(?:[A-Za-z_][\w:<>\*\s]*?\s\*?)?((?:PDM1SPIHID::)?~?\w+)\(([^;{]*?)\)\s*(?:const\s*)?\{',
                         text, re.M):
        start, depth, i = m.start(), 0, m.end() - 1
        while True:
            depth += {'{': 1, '}': -1}.get(text[i], 0)
            i += 1
            if depth == 0:
                break
        out.setdefault(m.group(1), text[start:i])
    return out


class Adapter(unittest.TestCase):
    old = (KBD / 'PDM1SPIHID.cpp').read_text()
    new = (HERE / 'PDM1SPIHID.cpp').read_text()

    def test_hashes_and_tree(self):
        for f in FILES:
            self.assertEqual(sha(KBD / f), MANIFEST['files'][f]['before_sha256'], f)
            self.assertEqual(sha(HERE / f), MANIFEST['files'][f]['after_sha256'], f)
        self.assertEqual(sha(HERE / 'trackpad-enum-adapter.patch'), MANIFEST['patch_sha256'])
        for rel, digest in MANIFEST['unchanged_keyboard_inputs'].items():
            self.assertEqual(sha(KBD / rel), digest, rel)
        for rel, digest in MANIFEST['dependencies'].items():
            self.assertEqual(sha(ENUM / rel), digest, rel)
        self.assertFalse((HERE / 'Info.plist').exists())

    def test_patch_replays(self):
        with tempfile.TemporaryDirectory() as t:
            d = Path(t) / 'src/Kernel/Extensions/PDM1SPIHID'
            d.mkdir(parents=True)
            for f in FILES:
                shutil.copy(KBD / f, d / f)
            subprocess.run(['patch', '-p1', '-s', '-i', str(HERE / 'trackpad-enum-adapter.patch')], cwd=t, check=True)
            for f in FILES:
                self.assertEqual((d / f).read_bytes(), (HERE / f).read_bytes(), f)

    def test_change_scope(self):
        a, b = functions(self.old), functions(self.new)
        changed = {k for k in a if k in b and a[k] != b[k]}
        self.assertEqual(changed, {'PDM1SPIHID::init', 'PDM1SPIHID::probe', 'PDM1SPIHID::handleStart',
                                   'PDM1SPIHID::waitResponse', 'PDM1SPIHID::enumerate'})
        self.assertEqual(set(b) - set(a), {'PDM1SPIHID::enumerateTrackpad', 'PDM1SPIHID::preserveTrackpadEnumeration'})
        self.assertFalse(set(a) - set(b))
        # poll, receive, fail, teardown, metadata recorders, descriptor/identity are byte-identical
        for k in ('PDM1SPIHID::poll', 'PDM1SPIHID::receive', 'PDM1SPIHID::fail', 'PDM1SPIHID::quiesce',
                  'PDM1SPIHID::stopGated', 'PDM1SPIHID::disposeTransport', 'PDM1SPIHID::start',
                  'PDM1SPIHID::startHardware', 'PDM1SPIHID::newReportDescriptor', 'PDM1SPIHID::terminate'):
            self.assertEqual(a[k], b[k], k)
        self.assertEqual(a['PDM1SPIHID::waitResponse'].replace('m1kbd_claim_request', 'm1tp_claim_request'),
                         b['PDM1SPIHID::waitResponse'])
        self.assertEqual(a['PDM1SPIHID::enumerate'].replace('descriptorLength=message.payload_length;\n    return true;',
                                                            'descriptorLength=message.payload_length;\n    return enumerateTrackpad();'),
                         b['PDM1SPIHID::enumerate'])

    def test_probe_opt_ins_precede_claim(self):
        p = functions(self.new)['PDM1SPIHID::probe']
        self.assertLess(p.index('"pdm1kbd"'), p.index('OSCompareAndSwap'))
        self.assertLess(p.index('"pdm1trackpadenum"'), p.index('OSCompareAndSwap'))
        self.assertIn('trackpadOptIn!=1) return nullptr;', p)
        self.assertEqual(self.new.count('OSCompareAndSwap'), 1)

    def test_read_only_enumeration(self):
        t = functions(self.new)['PDM1SPIHID::enumerateTrackpad']
        self.assertEqual(re.findall(r'waitResponse\(([^,]+),([^,]+),([^,]+),', t), [('2', '2', '512'), ('0x10', '2', '512')])
        changed = ''.join(functions(self.new)[k] for k in ('PDM1SPIHID::enumerateTrackpad', 'PDM1SPIHID::preserveTrackpadEnumeration',
                                                           'PDM1SPIHID::handleStart', 'PDM1SPIHID::probe', 'PDM1SPIHID::init'))
        changed = re.sub(r'/\*.*?\*/', '', changed, flags=re.S)  # judge code, not comments
        for bad in ('0x52', '0x51', '0x32', 'dlsp_request', 'writePacket', 'setReport', 'getReport', 'handleReport',
                    'publishResource', 'registerService', 'addMatchingNotification', 'IOHIDInterface', 'attach', 'newUserClient'):
            self.assertNotIn(bad, changed, bad)
        self.assertEqual(self.new.count('dlsp_request('), 1)
        self.assertIn('dlsp_request(0xd0,0x20,report,subdevice,id,responseLength,nullptr,0,packet)', self.new)
        self.assertNotRegex(self.new, r'struct\s+m1tp_observation\s+\w+\s*[;=]')  # no stack snapshot

    def test_handle_start_order(self):
        h = functions(self.new)['PDM1SPIHID::handleStart']
        self.assertLess(h.index('runAction(startGated)'), h.index('preserveTrackpadEnumeration(provider)'))
        self.assertIn('bool preserved=preserveTrackpadEnumeration(provider);', h)  # never short-circuited
        self.assertIn('return gated && preserved;', h)
        p = functions(self.new)['PDM1SPIHID::preserveTrackpadEnumeration']
        self.assertLess(p.index('request_mask=usedRequestKeys'), p.index('OSData::withBytes'))
        self.assertIn('provider->setProperty(M1TP_OBSERVATION_KEY,data)', p)
        self.assertIn('setProperty(M1TP_OBSERVATION_KEY,data)', p.replace('provider->', ''))

    def test_init_and_header(self):
        i = functions(self.new)['PDM1SPIHID::init']
        for s in ('bzero(&trackpadEnumeration,sizeof(trackpadEnumeration));', 'M1TP_OBS_NOT_ATTEMPTED', 'M1TP_OBS_BEFORE_IO',
                  'M1TP_OBSERVATION_VERSION'):
            self.assertIn(s, i)
        h = (HERE / 'PDM1SPIHID.h').read_text()
        self.assertLess(h.index('#include "observation_layout.h"'), h.index('#include "m1tp_enum.h"'))
        self.assertIn('struct m1tp_observation trackpadEnumeration;', h)
        c = (HERE / 'CMakeLists.txt').read_text()
        self.assertEqual(c.replace(' m1tp_enum.c)', ')'), (KBD / 'CMakeLists.txt').read_text())

    def test_extracted_bodies_on_host(self):
        f = functions(self.new)
        body = '\n'.join(f[k] for k in ('PDM1SPIHID::enumerateTrackpad', 'PDM1SPIHID::preserveTrackpadEnumeration',
                                        'PDM1SPIHID::handleStart'))
        with tempfile.TemporaryDirectory() as t:
            src = Path(t) / 'host.cpp'
            src.write_text((HERE / 'tests/host_stubs.hpp').read_text() + '\n' + body + '\n' +
                           (HERE / 'tests/host_cases.cpp').read_text())
            exe = Path(t) / 'host'
            flags = ['-Wall', '-Wextra', '-Werror', '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']
            subprocess.run(['cc', '-std=c11', *flags, '-c', str(ENUM / 'core/m1tp_enum.c'), '-o', str(Path(t) / 'core.o')], check=True)
            subprocess.run(['c++', '-std=c++14', *flags, '-I', str(ENUM), '-I', str(ENUM / 'core'), str(src), str(Path(t) / 'core.o'),
                            '-o', str(exe)], check=True)
            env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0:abort_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
            r = subprocess.run([str(exe)], capture_output=True, text=True, env=env)
            self.assertEqual(r.returncode, 0, r.stderr[-3000:])
            self.assertIn('HOST_TRACKPAD_ENUM_ADAPTER cases=16', r.stdout)


if __name__ == '__main__':
    unittest.main()
