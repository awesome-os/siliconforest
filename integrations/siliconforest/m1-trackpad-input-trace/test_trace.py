#!/usr/bin/env python3
"""Original host-only trace checks; no SDK, device, or input qualification."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
LAB = HERE.parents[2]
BASE = LAB / 'integrations/siliconforest/m1-trackpad-child'
KEYBOARD = LAB / 'integrations/siliconforest/m1-keyboard'
spec = importlib.util.spec_from_file_location('frozen_t2_trace_host_tests', BASE / 'test_child.py')
t2 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(t2)


def run(args, expected=0):
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    p = subprocess.run([str(x) for x in args], capture_output=True, text=True,
                       timeout=90, env=env)
    if expected is not None:
        assert p.returncode == expected, (args, p.returncode, p.stdout, p.stderr)
    return p


FLAGS = ['-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
         '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie']


def core_executable(d, ndebug=False, mutant=None):
    source = (HERE / 'core/m1tp_trace.c').read_text()
    if mutant:
        old, new = mutant
        assert old in source
        source = source.replace(old, new, 1)
    (d / 'm1tp_trace.c').write_text(source)
    return run(['cc', '-std=c11', *FLAGS, '-no-pie',
                *(['-DNDEBUG'] if ndebug else []),
                '-I' + str(HERE / 'core'), '-I' + str(KEYBOARD),
                HERE / 'test_trace.c', d / 'm1tp_trace.c',
                KEYBOARD / 'native_protocol/dl_spi_hid.c', '-o', d / 'test'],
               expected=None if ndebug else 0)


class TraceTests(unittest.TestCase):
    def test_policy_crc_and_output_bounds_sanitized(self):
        with tempfile.TemporaryDirectory(prefix='dl24-core-') as name:
            d = Path(name); core_executable(d)
            self.assertIn('HOST ONLY PASS', run([d / 'test']).stdout)

    def test_ndebug_refused_at_explicit_guard(self):
        with tempfile.TemporaryDirectory(prefix='dl24-ndebug-') as name:
            p = core_executable(Path(name), ndebug=True)
            self.assertNotEqual(p.returncode, 0)
            self.assertIn('NDEBUG forbidden', p.stderr)

    def test_assertion_killed_policy_mutants(self):
        mutants = [
            ('s->logged02<16', 's->logged02<17'),
            ('s->logged3f<2', 's->logged3f<3'),
            ('s->logged44<1', 's->logged44<2'),
            ('motion && !s->motion_logged', 'motion'),
            ('m->device!=2', 'm->device!=1'),
            ('m->payload_length!=8', 'm->payload_length<8'),
            ('m->payload[2]!=0x80 && m->payload[3]!=0x80 &&', '1 &&'),
            ('count(&s->seen02);', 's->seen02++;'),
        ]
        for mutant in mutants:
            with self.subTest(mutant=mutant), tempfile.TemporaryDirectory(prefix='dl24-mutant-') as name:
                d = Path(name); core_executable(d, mutant=mutant)
                p = run([d / 'test'], expected=None)
                self.assertNotEqual(p.returncode, 0)
                self.assertIn('Assertion', p.stderr)

    def test_source_delta_keeps_transport_requests_lifetime_and_route(self):
        before = (BASE / 'adapter/SFM1SPIHID.cpp').read_text()
        after = (HERE / 'adapter/SFM1SPIHID.cpp').read_text()
        # Reversing ONLY the declared additions reproduces the complete frozen
        # parent byte-for-byte, including every non-poll method and five keys.
        restored = after.replace(t2.body(after, 'SFM1SPIHID::traceTrackpad'), '')
        restored = restored.replace('    m1tp_trace_init(&trackpadTrace);\n', '')
        restored = restored.replace('    if (self->t2Enabled && m1tp_trace_announce(&self->trackpadTrace))\n'
                                    '        IOLog("DL24_TP_TRACE version=1 passive=1 cap02=16 cap3f=2 cap44=1 motion_reserved=1\\n");\n', '')
        restored = restored.replace('            self->traceTrackpad(&message);\n', '')
        self.assertEqual(restored, before)
        header = (HERE / 'adapter/SFM1SPIHID.h').read_text()
        header = header.replace('#include "m1tp_trace.h"\n', '')
        header = header.replace('    struct m1tp_trace_state trackpadTrace;\n'
                                '    void traceTrackpad(const struct dlsp_message *);\n', '')
        self.assertEqual(header, (BASE / 'adapter/SFM1SPIHID.h').read_text())
        poll = t2.body(after, 'SFM1SPIHID::poll')
        announce = poll.index('m1tp_trace_announce(')
        self.assertLess(poll.index('!self->published'), announce)
        self.assertLess(poll.index('__ATOMIC_ACQUIRE)!=1) return;'), announce)
        self.assertIn('self->t2Enabled && m1tp_trace_announce', poll)
        branch = 'else if (self->t2Enabled && message.device==2 && message.flags==0x20 && message.command==0x10) {'
        self.assertIn(branch + '\n            self->traceTrackpad(&message);\n            self->routeTrackpad(&message);', poll)
        tap = t2.body(after, 'SFM1SPIHID::traceTrackpad')
        self.assertIn('char hex[129];', tap)
        self.assertNotRegex(tap, r'\b(?:new|malloc|IOMalloc|snprintf|sprintf|dlsp_request|waitResponse)\b')
        self.assertNotIn('IOLog', t2.body(after, 'SFM1SPIHID::init'))
        cmake = (HERE / 'adapter/CMakeLists.txt').read_text()
        self.assertEqual(cmake.replace(' m1tp_trace.c)', ')'),
                         (BASE / 'adapter/CMakeLists.txt').read_text())

    def test_actual_cpp_tap_poll_and_existing_lifecycle_under_sanitizers(self):
        with tempfile.TemporaryDirectory(prefix='dl24-cpp-') as name:
            d = Path(name); t2.stage(d)
            for p in (HERE / 'adapter').iterdir(): shutil.copyfile(p, d / p.name)
            shutil.copyfile(HERE / 'core/m1tp_trace.h', d / 'm1tp_trace.h')
            fake = (d / 'fake_iokit.h').read_text()
            fake = fake.replace('#include <cstdio>', '#include <cstdio>\n#include <cstdarg>')
            old = 'inline void IOLog(const char *,...) {}'
            self.assertIn(old, fake)
            fake = fake.replace(old, '''static std::vector<std::string> dl24Lines;
inline void IOLog(const char *format,...) {
    if (std::strncmp(format,"DL24_",5)) return;
    char line[256];va_list args;va_start(args,format);
    int n=std::vsnprintf(line,sizeof(line),format,args);va_end(args);
    assert(n>0 && (size_t)n<sizeof(line));dl24Lines.emplace_back(line);
}''')
            (d / 'fake_iokit.h').write_text(fake)
            unit = (d / 'lifecycle.cpp').read_text()
            old = (BASE / 'adapter/SFM1SPIHID.cpp').read_text()
            new = (HERE / 'adapter/SFM1SPIHID.cpp').read_text()
            for method in ('init', 'poll'):
                a = t2.body(old, 'SFM1SPIHID::' + method)
                self.assertIn(a, unit)
                unit = unit.replace(a, t2.body(new, 'SFM1SPIHID::' + method), 1)
            unit += '\n' + t2.body(new, 'SFM1SPIHID::traceTrackpad')
            unit = unit.replace('int main()\n', 'void baseline_main()\n', 1)
            unit += r'''
int main() {
    baseline_main();
    reset();dl24Lines.clear();auto *p=make();assert(dl24Lines.empty());
    SFM1SPIHID::poll(p,nullptr);assert(dl24Lines.empty());
    assert(p->start(p->provider));assert(dl24Lines.empty());
    p->t2Phase=0;SFM1SPIHID::poll(p,nullptr);assert(dl24Lines.empty());
    p->t2Phase=1;++Faults::gateDepth;SFM1SPIHID::poll(p,nullptr);--Faults::gateDepth;
    assert(dl24Lines.size()==1 && p->trackpadTrace.sequence==0);
    SFM1SPIHID::poll(p,nullptr);assert(dl24Lines.size()==1);
    uint8_t keyboard[10]={1};incoming.push_back({1,0x20,0x10,1,0,0,0,keyboard,10});
    ++Faults::gateDepth;SFM1SPIHID::poll(p,nullptr);--Faults::gateDepth;
    assert(dl24Lines.size()==1 && p->reportsDelivered==1);
    std::vector<uint8_t> payload(1752);
    for(size_t i=0;i<payload.size();i++)payload[i]=(uint8_t)i;
    payload[0]=0x44;
    onReport=[&](IOHIDDevice *dev,IOMemoryDescriptor *report){
        assert(dev==p->childOwned && report->bytes==payload);
        assert(dl24Lines.size()==31 && dl24Lines.back().find("DL24_TP_END seq=1 bytes=1752 crc16=")==0);
    };
    incoming.push_back({2,0x20,0x10,0x99,0x83,0xfe,65535,payload.data(),payload.size()});
    ++Faults::gateDepth;SFM1SPIHID::poll(p,nullptr);--Faults::gateDepth;
    assert(p->childObserved==1 && p->childSubmitted==1 && p->trackpadTrace.sequence==1);
    p->stopping=true;SFM1SPIHID::poll(p,nullptr);assert(dl24Lines.size()==31);
    std::puts("DL24_HOST_RECORD_BEGIN");
    for(const auto &line:dl24Lines)std::fputs(line.c_str(),stdout);
    std::puts("DL24_HOST_RECORD_END");
    reset();dl24Lines.clear();auto *legacy=make(false);assert(legacy->start(legacy->provider));
    SFM1SPIHID::poll(legacy,nullptr);assert(dl24Lines.empty());
    std::puts("HOST ONLY PASS: actual poll gating, unchanged keyboard/child dispatch, exact trace lines");
}
'''
            (d / 'lifecycle.cpp').write_text(unit)
            includes = ['-I' + str(d), '-I' + str(KEYBOARD), '-I' + str(HERE / 'core')]
            objects = []
            for src in (d / 'm1tp_child_policy.c', KEYBOARD / 'native_protocol/dl_spi_hid.c',
                        KEYBOARD / 'm1kbd_hid.c', HERE / 'core/m1tp_trace.c'):
                obj = d / (src.stem + '.o'); objects.append(obj)
                run(['cc', '-std=c11', *FLAGS, *includes, '-c', src, '-o', obj])
            run(['c++', '-std=c++14', *FLAGS, '-no-pie', *includes,
                 d / 'lifecycle.cpp', *objects, '-o', d / 'lifecycle'])
            output = run([d / 'lifecycle']).stdout
            self.assertIn('HOST ONLY PASS: actual poll', output)
            block = output.split('DL24_HOST_RECORD_BEGIN\n')[1].split('DL24_HOST_RECORD_END')[0]
            lines = block.splitlines()
            contract = json.loads((HERE / 'TRACE_CONTRACT.json').read_text())
            self.assertEqual(lines[0], contract['initial_line'])
            self.assertEqual(lines[1], 'DL24_TP_BEGIN seq=1 id=44 bytes=1752 reason=1 seen02=0 seen3f=0 seen44=1')
            data = bytearray()
            for index, line in enumerate(lines[2:-1]):
                m = re.fullmatch(r'DL24_TP_HEX seq=1 index=(\d+) data=([0-9a-f]+)', line)
                self.assertIsNotNone(m)
                self.assertEqual(int(m[1]), index)
                self.assertEqual(len(m[2]), 48 if index == 27 else 128)
                data.extend(bytes.fromhex(m[2]))
            expected = bytes([0x44]) + bytes(i % 256 for i in range(1,1752))
            self.assertEqual(bytes(data), expected)
            crc = 0
            for b in data:
                crc ^= b
                for _ in range(8): crc = (crc >> 1) ^ (0xa001 if crc & 1 else 0)
            self.assertEqual(lines[-1], f'DL24_TP_END seq=1 bytes=1752 crc16={crc:04x}')


if __name__ == '__main__':
    unittest.main(verbosity=2)
