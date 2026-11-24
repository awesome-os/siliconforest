#!/usr/bin/env python3
"""Extract and compile candidate C++, with host-only dependency fakes.

No device/MMIO, SDK ABI, kernel scheduler or physical-input claim. Tests model
the ONCE contract documented in pinned XNU; production source stays unchanged.
"""
from pathlib import Path
import hashlib
import os
import re
import resource
import shutil
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
QUEUE = 'src/Kernel/Extensions/IOHIDFamily/IOHIDSystem/PDHIDEventQueue.cpp'
HEADER = 'src/Kernel/Extensions/IOHIDFamily/IOHIDSystem/PDHIDEventQueue.h'
NATIVE = 'src/Kernel/Extensions/PDM1SPIHID/PDM1SPIHID.cpp'
SERVICE = 'src/Kernel/Extensions/IOHIDFamily/IOHIDFamily/IOHIDEventService.cpp'
BASE_HASHES = {
    SERVICE: '856c46ec745af36ba9f7ce7b3c92b68b1f5c90bdf336308309d24e868fbb02fb',
    QUEUE: 'c7a2f35bd86bc2da17c766d39728d930fe80aedf1526935b739e74e74204a602',
    HEADER: 'b2fdd4610c5810099dde560d1f000e854f9aed21ddd332ede08cb17c4f9001bb',
    NATIVE: 'b38f00879e8505de17c02ef59c6c6d22e1b818fd7dba27e2411ab76740101389',
}
CASES = ('no-request', 'empty-repeat', 'keyboard-no-mouse', 'deferred-devfs',
         'reentry', 'concurrent', 'setup-race', 'call-failure', 'lock-failure', 'major-failure',
         'node-failure', 'persistent-failure', 'event-before-devfs',
         'mouse-first-event', 'event-no-start', 'push-concurrent', 'capabilities', 'capabilities-refused', 'native-super-fail', 'native-timer-fail', 'native-success')


def function(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def extracted(queue=None, native=None):
    queue = queue if queue is not None else (HERE/'after'/QUEUE).read_text()
    native = native if native is not None else (HERE/'after'/NATIVE).read_text()
    header = (HERE/'after'/HEADER).read_text()
    # Replace only platform includes with host collaborators, not function bodies.
    strip = lambda text: re.sub(r'^#include[^\n]*\n', '', text, flags=re.M)
    return '\n'.join([(HERE/'tests/host_stubs.hpp').read_text(), strip(header),
                      strip(queue), (HERE/'tests/native_stubs.hpp').read_text(),
                      function(native, 'bool PDM1SPIHID::start('),
                      (HERE/'tests/hid_stubs.hpp').read_text(),
                      function((HERE/'after'/SERVICE).read_text(), 'static void\npdHIDPublishSupportedQueues('),
                      (HERE/'tests/host_cases.cpp').read_text()])


def compile_case(directory, source, name='harness'):
    cpp = directory/(name+'.cpp')
    binary = directory/name
    cpp.write_text(source)
    compiler = shutil.which('clang++') or shutil.which('g++')
    if not compiler:
        raise RuntimeError('A local C++ compiler is required; no installation performed')
    result = subprocess.run([compiler, '-std=c++14', '-pthread', '-Wall', '-Wextra', '-Werror',
                    '-Wpedantic', '-O1', '-g', '-fsanitize=undefined',
                    '-fno-sanitize-recover=all', str(cpp), '-o', str(binary)],
                   capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    return binary


class Publication(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        cls.temp = tempfile.TemporaryDirectory(prefix='m1-native-input-')
        cls.directory = Path(cls.temp.name)
        cls.binary = compile_case(cls.directory, extracted())

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_case(self, case):
        env = dict(os.environ, UBSAN_OPTIONS='halt_on_error=1')
        result = subprocess.run([str(self.binary), case], capture_output=True,
                                text=True, timeout=20, env=env)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_preserved_bases_and_native_patch_scope(self):
        for name, expected in BASE_HASHES.items():
            self.assertEqual(hashlib.sha256((HERE/'before'/name).read_bytes()).hexdigest(), expected)
        old = (HERE/'before'/NATIVE).read_text()
        new = (HERE/'after'/NATIVE).read_text()
        start_old = function(old, 'bool PDM1SPIHID::start(')
        start_new = function(new, 'bool PDM1SPIHID::start(')
        self.assertEqual(old, new)  # native005 is now completely unchanged
        service = (HERE/'after'/SERVICE).read_text()
        tail = function(service, 'bool IOHIDEventService::start (')
        self.assertEqual(tail.count('pdHIDPublishSupportedQueues('), 1)
        self.assertLess(tail.index('parseSupportedElements'), tail.index('_readyForInputReports = true;'))
        self.assertLess(tail.index('_readyForInputReports = true;'), tail.index('pdHIDPublishSupportedQueues('))
        queue = (HERE/'after'/QUEUE).read_text()
        for signature in ('void\nPDHIDPushKeyboardEvent(', 'static void\npd_hid_mouse_enqueue('):
            push = function(queue, signature)
            for forbidden in ('PublishKeyboardDevice', 'PublishMouseDevice', '_setup(', '_setup_lock(', 'Alloc(', 'cdevsw', 'devfs_', 'thread_call_allocate'):
                self.assertNotIn(forbidden, push)
        push = function(queue, 'void\nPDHIDPushKeyboardEvent(')
        self.assertLess(push.index('IOLockLock('), push.index('++gKbdSequence'))

    def test_runtime_mutants_are_rejected(self):
        queue = (HERE/'after'/QUEUE).read_text()
        native = (HERE/'after'/NATIVE).read_text()
        mutants = [
            ('missing_once', queue.replace('THREAD_CALL_OPTIONS_ONCE);', '0);'), native, 'empty-repeat'),
            ('drop_request', queue.replace('const UInt32 previous = OSBitOrAtomic(requested, &gPublishRequests);', 'const UInt32 previous = 0;'), native, 'native-success'),
            ('fabricated_mouse', queue.replace('pd_hid_request_publication(kPublishKeyboard);', 'pd_hid_request_publication(kPublishKeyboard | kPublishMouse);'), native, 'native-success'),
            ('false_ready', queue.replace('return *node && (!wantAlias || *alias) ? kPublishOK : kPublishNode;', 'return kPublishOK;'), native, 'node-failure'),
            ('no_retry', queue.replace('thread_call_enter_delayed(pd_hid_load(&gPublishCall), deadline);', '(void)deadline; (void)&thread_call_enter_delayed;'), native, 'deferred-devfs'),
            ('wrong_empty_result', queue.replace('error = EAGAIN;', 'error = ENXIO;'), native, 'empty-repeat'),
        ]
        for name, q, n, case in mutants:
            with self.subTest(name=name):
                self.assertTrue(q != queue or n != native)
                binary = compile_case(self.directory, extracted(q, n), name)
                result = subprocess.run([str(binary), case], capture_output=True, timeout=20)
                self.assertNotEqual(result.returncode, 0, 'surviving mutation: '+name)


for _case in CASES:
    setattr(Publication, 'test_'+_case.replace('-', '_'),
            lambda self, case=_case: self.run_case(case))

if __name__ == '__main__':
    unittest.main(verbosity=2)
