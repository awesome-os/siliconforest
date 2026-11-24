#!/usr/bin/env python3
"""Host-only loader bounds, console challenge, and callback tests; no hardware."""
import copy
import io
import json
from pathlib import Path
import plistlib
import struct
import sys
import tempfile
import threading
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import host_progress as host
import normal_image as image


def kernel_fixture():
    """Small fabricated collection, never executed or labeled as a build."""
    def segment(name, vm, nvm, off, size, prot):
        return struct.pack('<II16s4Q4I', 0x19, 72, name.encode(), vm, nvm, off, size, 7, prot, 0, 0)
    base = image.KC_LINK
    text = segment('__TEXT', base, 0x1000, 0, 0x1000, 1)
    kernel = segment('__KERNEL', base+0x4000, 0x4000, 0x1000, 0x4000, 5)
    thread = bytearray(288)
    struct.pack_into('<4I', thread, 0, 5, 288, 6, 68)
    struct.pack_into('<Q', thread, 272, base+0x4400)
    name = b'com.apple.kernel\0'
    size = (32 + len(name) + 7) & -8
    member = struct.pack('<IIQQII', 0x80000035, size, base+0x4000, 0x1000, 32, 0) + name
    member = member.ljust(size, b'\0')
    fixup = struct.pack('<4I', 0x80000034, 16, 0x4f00, 64)
    cmds = text + kernel + thread + member + fixup
    raw = bytearray(0x5000)
    raw[:32] = struct.pack('<8I', 0xfeedfacf, 0x100000c, 0, 12, 5, len(cmds), 0x80000001, 0)
    raw[32:32+len(cmds)] = cmds
    raw[0x1000:0x1020] = struct.pack('<8I', 0xfeedfacf, 0x100000c, 0, 2, 1, 72, 0x80000001, 0)
    raw[0x1020:0x1068] = segment('__TEXT', base+0x4000, 0x4000, 0x1000, 0x4000, 5)
    struct.pack_into('<I', raw, 0x1400, 0xd5034fdf)
    token = b'DEBUG_ARM64_T8103'
    raw[0x1500:0x1500+len(token)] = token
    return raw


class ImageTests(unittest.TestCase):
    def test_virtual_input_requires_exact_optin_and_serial_only_collection(self):
        experiment = {'version': 1, 'origin': 'host_pdserialhid', 'output': [2560, 1600],
                      'client': [800, 480], 'content_origin': [1, 24]}
        record = {'desktop_experiment': {'version': 1, 'command': '/usr/bin/darwin-desktop-test'},
                  'virtual_input_experiment': experiment}
        members = {'org.puredarwin.driver.PDSerialHID': {}, 'com.apple.iokit.IOHIDFamily': {}}
        def inputs(doc=record, names=members):
            return SimpleNamespace(contents={'build_record': json.dumps(doc).encode(), 'ramdisk': b'HOST'},
                                   report={'kernel': {'members': names}})
        self.assertTrue(host.virtual_input_enabled(inputs()))
        self.assertFalse(host.virtual_input_enabled(inputs({})))
        for key, value in [('version', True), ('version', 2), ('origin', 'physical'),
                           ('content_origin', [True, 24]), ('output', [2560., 1600]),
                           ('client', [801, 480])]:
            bad = copy.deepcopy(record)
            bad['virtual_input_experiment'][key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                host.virtual_input_enabled(inputs(bad))
        for names in ({}, {**members, 'org.puredarwin.driver.PDM1SPIHID': {}}):
            with self.assertRaises(ValueError):
                host.virtual_input_enabled(inputs(names=names))
        bad = copy.deepcopy(record)
        bad.pop('desktop_experiment')
        with self.assertRaises(ValueError):
            host.virtual_input_enabled(inputs(bad))

    def test_keyboard_optin_requires_its_native_member_and_bound_root(self):
        record = {'keyboard_experiment': {'version': 1, 'command': '/usr/bin/darwin-keyboard-test'}}
        raw = kernel_fixture()
        def inputs(doc=record, root=True):
            contents = {'build_record': json.dumps(doc).encode(), 'kernel': bytes(raw)}
            if root:
                contents['ramdisk'] = b'HOST ONLY'
            return SimpleNamespace(contents=contents)
        self.assertFalse(host.keyboard_enabled(inputs({})))
        with self.assertRaisesRegex(ValueError, 'SPI HID member'):
            host.keyboard_enabled(inputs())
        name = b'org.puredarwin.driver.PDM1SPIHID\0'
        size = (32+len(name)+7) & -8
        entry = struct.pack('<IIQQII', 0x80000035, size, image.KC_LINK+0x8000, 0x2000, 32, 0)
        entry = (entry+name).ljust(size, b'\0')
        count, length = struct.unpack_from('<II', raw, 16)
        raw[32+length:32+length+size] = entry
        struct.pack_into('<II', raw, 16, count+1, length+size)
        self.assertTrue(host.keyboard_enabled(inputs()))
        with self.assertRaises(ValueError):
            host.keyboard_enabled(inputs(root=False))
        both = {**record, 'desktop_experiment': {'version': 1, 'command': '/usr/bin/darwin-desktop-test'}}
        with self.assertRaises(ValueError):
            host.keyboard_enabled(inputs(both))
        for version in (True, 2, '1'):
            with self.subTest(version=version), self.assertRaises(ValueError):
                host.keyboard_enabled(inputs({'keyboard_experiment': {'version': version,
                    'command': '/usr/bin/darwin-keyboard-test'}}))
        self.assertEqual(host.keyboard_command('b'*48),
                         b"/usr/bin/darwin-keyboard-test '" + b'b'*48 + b"'\r")

    def test_desktop_requires_fixed_command_and_bound_ramdisk(self):
        def inputs(value, ramdisk=True):
            contents = {'build_record': json.dumps(value).encode()}
            if ramdisk:
                contents['ramdisk'] = b'HOST TEST ONLY'
            return SimpleNamespace(contents=contents)
        self.assertFalse(host.desktop_enabled(inputs({})))
        flag = {'desktop_experiment': {'version': 1, 'command': '/usr/bin/darwin-desktop-test'}}
        self.assertTrue(host.desktop_enabled(inputs(flag)))
        with self.assertRaises(ValueError):
            host.desktop_enabled(inputs(flag, False))
        for bad in ({'version': 1, 'command': 'arbitrary command'}, True, {'version': 2}):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                host.desktop_enabled(inputs({'desktop_experiment': bad}))
        self.assertEqual(host.desktop_command('a'*48),
                         b"/usr/bin/darwin-desktop-test '" + b'a'*48 + b"'\r")
        with self.assertRaises(ValueError):
            host.desktop_command("';bad")

    def test_valid_collection_preserves_native_entry_and_never_claims_execution(self):
        value = image.inspect(kernel_fixture())
        self.assertEqual(value['entry'], image.KC_LINK+0x4400)
        self.assertEqual(value['kernel_header_vmaddr'], image.KC_LINK+0x4000)
        self.assertFalse(value['hardware_boot_observed'])
        self.assertFalse(value['command_line_observed'])

    def test_wrong_arch_type_truncation_segment_and_entry_fail_closed(self):
        cases = []
        for off, fmt, value in [(4, '<I', 0x1000007), (12, '<I', 2), (20, '<I', 8),
                                (32+24, '<Q', 0), (32+48, '<Q', 1 << 63),
                                (32+144+272, '<Q', image.KC_LINK+0x8000),
                                (0x1000+24, '<I', 1), (0x1400, '<I', 0x14000000)]:
            raw = kernel_fixture()
            struct.pack_into(fmt, raw, off, value)
            cases.append(raw)
        cases += [kernel_fixture()[:20], kernel_fixture()[:0x2000]]
        for raw in cases:
            with self.subTest(raw=bytes(raw[:32]).hex()), self.assertRaises(ValueError):
                image.inspect(raw)

    def test_legacy_park_and_missing_native_identity_rejected(self):
        raw = kernel_fixture()
        raw[0x1500:0x1511] = b'_' * 17
        with self.assertRaisesRegex(ValueError, 'identity'):
            image.inspect(raw)
        raw = kernel_fixture() + b'DARWIN_LAB_M1_EARLY_CONSOLE checkpoint=entry-before-vm'
        with self.assertRaisesRegex(ValueError, 'park'):
            image.inspect(raw)

    def test_packet_hash_path_and_build_provenance_are_required(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            patch_bytes = b'HOST-ONLY FAKE SOURCE PATCH\n'
            record = dict(source_commit=image.PINS['puredarwin'], corecrypto_commit=image.PINS['corecrypto'],
                          source_patch_sha256=image.sha(patch_bytes), status='BUILT_NOT_BOOTED', build_exit_code=0,
                          kernel_sha256=image.sha(kernel_fixture()), drv_path='/nix/store/'+'a'*32+'-host-fake.drv',
                          output_path='/nix/store/'+'b'*32+'-host-fake')
            raw = {'kernel': bytes(kernel_fixture()), 'source_patch': patch_bytes,
                   'build_record': json.dumps(record).encode()}
            doc = dict(schema_version=1, target='m1-mbp13-j293', purpose='continuing-xnu',
                       source_pins=image.PINS, artifacts={})
            for name, contents in raw.items():
                (root/name).write_bytes(contents)
                doc['artifacts'][name] = dict(path=name, sha256=image.sha(contents), bytes=len(contents))
            path = root/'manifest.json'
            def save():
                path.write_text(json.dumps(doc))
            save()
            self.assertEqual(image.verify_manifest(path)[1]['kernel'], raw['kernel'])
            original = copy.deepcopy(doc)
            for name, value in [('sha256', '0'*64), ('path', '../kernel'), ('bytes', True)]:
                doc = copy.deepcopy(original)
                doc['artifacts']['kernel'][name] = value
                save()
                with self.assertRaises((ValueError, FileNotFoundError)):
                    image.verify_manifest(path)
            doc = copy.deepcopy(original)
            record['source_patch_sha256'] = 'f'*64
            bad = json.dumps(record).encode()
            (root/'build_record').write_bytes(bad)
            doc['artifacts']['build_record'].update(sha256=image.sha(bad), bytes=len(bad))
            save()
            with self.assertRaisesRegex(ValueError, 'provenance'):
                image.verify_manifest(path)

    def test_kernel_slide_and_later_file_backed_overwrites_are_rejected(self):
        for off, fmt, value in [(0x1020, '<I', 2), (0x1020+24, '<Q', image.KC_LINK+0x8000)]:
            raw = kernel_fixture()
            struct.pack_into(fmt, raw, off, value)
            with self.subTest(offset=off), self.assertRaisesRegex(ValueError, 'slide contract'):
                image.inspect(raw)
        raw = kernel_fixture()
        # Append a later segment command, drawing zeros from a different file
        # offset, over the already validated entry instruction.
        command = struct.pack('<II16s4Q4I', 0x19, 72, b'__OVERWRITE', image.KC_LINK+0x4400,
                              4, 0x4800, 4, 1, 1, 0, 0)
        count, size = struct.unpack_from('<II', raw, 16)
        raw[32+size:32+size+72] = command
        struct.pack_into('<II', raw, 16, count+1, size+72)
        with self.assertRaisesRegex(ValueError, 'Overlapping file-backed'):
            image.inspect(raw)

    def test_embedded_text_cannot_extend_over_startup_data_const(self):
        raw=kernel_fixture()
        struct.pack_into('<II',raw,0x1000+16,2,144)
        raw[0x1068:0x10b0]=struct.pack('<II16s4Q4I',0x19,72,b'__DATA_CONST',
            image.KC_LINK+0x6000,0x1000,0x3000,0x1000,3,3,0,0)
        with self.assertRaisesRegex(ValueError,'TEXT overlaps'):
            image.inspect(raw)


def usb_fixture():
    return [dict(idVendor=0x1209, idProduct=0x316d, locationID=0x100000,
                 IORegistryEntryID=123, sessionID=456, IORegistryEntryChildren=[
                     dict(bInterfaceNumber=1, IORegistryEntryChildren=[dict(IOCalloutDevice='/dev/cu.usbmodemPRIMARY')]),
                     dict(bInterfaceNumber=3, IORegistryEntryChildren=[dict(IOCalloutDevice='/dev/cu.usbmodemVUART')])])]


class ConsoleTests(unittest.TestCase):
    def test_console_close_is_idempotent_and_first_failure_is_sticky(self):
        for fail in (False, True):
            with self.subTest(fail=fail), tempfile.TemporaryDirectory() as directory:
                class Port:
                    closes = 0
                    def read(self, _):
                        threading.Event().wait(.001)
                        return b''
                    def close(self):
                        self.closes += 1
                        if fail:
                            raise OSError('synthetic serial close failure')
                port = Port()
                session = host.ConsoleSession('/host-only-fake', Path(directory),
                                              serial_factory=lambda *a, **kw: port)
                if fail:
                    with self.assertRaises(OSError):
                        session.close()
                    with self.assertRaisesRegex(ValueError, 'synthetic serial close failure'):
                        session.close()
                    self.assertFalse((Path(directory)/'console-result.json').exists())
                else:
                    session.close()
                    original = (Path(directory)/'console-result.json').read_bytes()
                    session.close()
                    self.assertEqual((Path(directory)/'console-result.json').read_bytes(), original)
                self.assertEqual(port.closes, 1)

    def test_fixed_guest_command_follows_strict_proof_once_and_is_opt_in(self):
        nonce = 'a' * 48
        for kind, valid in (('desktop', True), ('keyboard', True), ('none', True), ('desktop', False), ('keyboard', False)):
            enabled = kind != 'none'
            with self.subTest(kind=kind, valid=valid), tempfile.TemporaryDirectory() as directory:
                chunks = [b'\n# ']
                finished = threading.Event()
                first_write = threading.Event()
                class Port:
                    def __init__(self):
                        self.writes = []
                    def read(self, _):
                        if chunks:
                            return chunks.pop(0)
                        finished.wait(.001)
                        return b''
                    def write(self, data):
                        self.writes.append(data)
                        if len(self.writes) == 1:
                            system = b'Darwin' if valid else b'Linux'
                            chunks.append(b'\nDL9_BEGIN_' + nonce.encode() + b'\n' + system +
                                          b'\narm64\n25.5.0\nDL9_END_' + nonce.encode() + b'\n# ')
                            first_write.set()
                        else:
                            chunks.extend([b'\n# ', b'\n# '])
                            finished.set()
                        return len(data)
                    def close(self):
                        pass
                port = Port()
                with patch.object(host.secrets, 'token_hex', return_value=nonce):
                    session = host.ConsoleSession('/host-only-fake', Path(directory),
                                                  serial_factory=lambda *a, **kw: port,
                                                  desktop=kind=='desktop', keyboard=kind=='keyboard')
                try:
                    if enabled and valid:
                        self.assertTrue(finished.wait(2))
                    else:
                        self.assertTrue(first_write.wait(2))
                        for _ in range(100):
                            if session.proof.complete or (not chunks and session.proof.sent_at is not None):
                                break
                            finished.wait(.001)
                finally:
                    session.close()
                self.assertEqual(port.writes, [session.proof.command()] +
                                 ([(host.keyboard_command if kind=='keyboard' else host.desktop_command)(nonce)]
                                  if enabled and valid else []))
                self.assertEqual((Path(directory)/'console-tx.bin').read_bytes(), b''.join(port.writes))
                receipt = json.loads((Path(directory)/'console-result.json').read_text())
                self.assertEqual(receipt['console_tx_sha256'], image.sha(b''.join(port.writes)))
                self.assertEqual(receipt['console_tx_bytes'], sum(map(len, port.writes)))

    def test_virtual_prime_is_paced_on_empty_reads_and_stops_at_close(self):
        nonce = 'a' * 48
        chunks = [b'\n# ']
        primed = threading.Event()
        class Port:
            def __init__(self):
                self.writes = []
            def read(self, _):
                if chunks:
                    return chunks.pop(0)
                primed.wait(.01)
                return b''
            def write(self, data):
                self.writes.append(data)
                if len(self.writes) == 1:
                    chunks.append(b'\nDL9_BEGIN_' + nonce.encode() +
                                  b'\nDarwin\narm64\n25.5.0\nDL9_END_' + nonce.encode() + b'\n# ')
                elif len(self.writes) == 2:
                    chunks.append(b'\nDL10_INPUT_PRIME_' + nonce.encode() + b'\n')
                elif len(self.writes) == 7:
                    primed.set()
                return len(data)
            def close(self):
                pass
        with tempfile.TemporaryDirectory() as directory:
            port = Port()
            with patch.object(host.secrets, 'token_hex', return_value=nonce):
                session = host.ConsoleSession('/host-only-fake', Path(directory),
                    serial_factory=lambda *a, **kw: port, desktop=True, virtual_input=True)
            try:
                self.assertTrue(primed.wait(3))
            finally:
                session.close()
            self.assertEqual(port.writes[:2], [session.proof.command(), host.desktop_command(nonce)])
            self.assertEqual(len(port.writes), 7)
            self.assertEqual((Path(directory)/'console-tx.bin').read_bytes(), b''.join(port.writes))
            result = json.loads((Path(directory)/'virtual-input-result.json').read_text())
            self.assertEqual(result['phase'], 'await_ready')
            self.assertTrue(result['cancelled'])
            self.assertIsNone(result['error'])
            with self.assertRaises(ValueError):
                session.send(b'NO_WRITE_AFTER_CLOSE')
            self.assertEqual(len(port.writes), 7)

    def test_failed_final_virtual_write_invalidates_emission_and_never_retries(self):
        nonce = 'a' * 48
        final = b'\x01m 5128 3535 0\n'
        for failure in ('short', 'exception'):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as directory:
                # A post-barrier fixture isolates the last transport write:
                # tick has advanced to complete before serial.write fails.
                schedule = host.VirtualInput(nonce)
                schedule.queue('dragging', [final], 'complete', 0)
                chunks = [b'\n# ']
                class Port:
                    def __init__(self):
                        self.writes = []
                    def read(self, _):
                        if chunks:
                            return chunks.pop(0)
                        return b''
                    def write(self, data):
                        self.writes.append(data)
                        if len(self.writes) == 1:
                            chunks.append(b'\nDL9_BEGIN_' + nonce.encode() +
                                b'\nDarwin\narm64\n25.5.0\nDL9_END_' + nonce.encode() + b'\n# ')
                        if data == final:
                            if failure == 'exception':
                                raise OSError('synthetic final serial failure')
                            return len(data) - 1
                        return len(data)
                    def close(self):
                        pass
                port = Port()
                with patch.object(host.secrets, 'token_hex', return_value=nonce), \
                        patch.object(host, 'VirtualInput', return_value=schedule):
                    session = host.ConsoleSession('/host-only-fake', Path(directory),
                        serial_factory=lambda *a, **kw: port, desktop=True, virtual_input=True)
                session.thread.join(2)
                self.assertFalse(session.thread.is_alive())
                session.close()
                self.assertEqual(port.writes, [session.proof.command(), host.desktop_command(nonce), final])
                self.assertEqual((Path(directory)/'console-tx.bin').read_bytes(), b''.join(port.writes))
                result = json.loads((Path(directory)/'virtual-input-result.json').read_text())
                receipt = json.loads((Path(directory)/'console-result.json').read_text())
                self.assertEqual(result['phase'], 'failed')
                self.assertFalse(result['schedule_completed'])
                self.assertIsNotNone(result['error'])
                self.assertFalse(receipt['command_line_observed'])
                self.assertIsNotNone(receipt['error'])

    def test_observation_failure_cancels_input_but_preserves_later_raw_tail(self):
        nonce = 'a' * 48
        schedule = host.VirtualInput(nonce)
        schedule.phase, schedule.deadline = 'await_ready', 0
        chunks = [b'\n# ']
        drained = threading.Event()
        tail = b'\ndarwin-wayland-client-input: diagnostic after timeout\nSCRIPT_EXIT_DETAILS\n'
        class Port:
            def __init__(self):
                self.writes = []
            def read(self, _):
                if chunks:
                    return chunks.pop(0)
                drained.set()
                drained.wait(.001)
                return b''
            def write(self, data):
                self.writes.append(data)
                if len(self.writes) == 1:
                    chunks.append(b'\nDL9_BEGIN_' + nonce.encode() +
                        b'\nDarwin\narm64\n25.5.0\nDL9_END_' + nonce.encode() + b'\n# ')
                elif len(self.writes) == 2:
                    chunks.append(tail)
                return len(data)
            def close(self):
                pass
        with tempfile.TemporaryDirectory() as directory:
            port = Port()
            with patch.object(host.secrets, 'token_hex', return_value=nonce), \
                    patch.object(host, 'VirtualInput', return_value=schedule):
                session = host.ConsoleSession('/host-only-fake', Path(directory),
                    serial_factory=lambda *a, **kw: port, desktop=True, virtual_input=True)
            try:
                self.assertTrue(drained.wait(2))
            finally:
                session.close()
            self.assertTrue((Path(directory)/'console.bin').read_bytes().endswith(tail))
            self.assertEqual(port.writes, [session.proof.command(), host.desktop_command(nonce)])
            result = json.loads((Path(directory)/'virtual-input-result.json').read_text())
            receipt = json.loads((Path(directory)/'console-result.json').read_text())
            self.assertEqual(result['phase'], 'failed')
            self.assertTrue(result['cancelled'])
            self.assertFalse(result['schedule_completed'])
            self.assertIn('VirtualInputError', receipt['error'])
            self.assertFalse(receipt['command_line_observed'])
    def test_decorated_prompt_sends_one_challenge_and_preserves_raw_console(self):
        chunks = [b'warning: no shell yet\r\n',
                  b'\x1b[1m\0\0\x1b[7m#\x1b[m\0\0\r \r\r\x1b[J\0\0# \x1b[K\0\x1b[?200',
                  b'4h']
        raw = b''.join(chunks)
        sent = threading.Event()
        class Port:
            def __init__(self):
                self.writes = []
            def read(self, _):
                if chunks:
                    return chunks.pop(0)
                sent.wait(.001)
                return b''
            def write(self, data):
                self.writes.append(data)
                sent.set()
                return len(data)
            def close(self):
                pass
        port = Port()
        with tempfile.TemporaryDirectory() as directory:
            session = host.ConsoleSession('/host-only-fake', Path(directory),
                                          serial_factory=lambda *a, **kw: port)
            try:
                self.assertTrue(sent.wait(2), 'decorated prompt never triggered the command')
            finally:
                session.close()
            self.assertEqual(port.writes, [session.proof.command()])
            self.assertEqual((Path(directory)/'console.bin').read_bytes(), raw)
            receipt = json.loads((Path(directory)/'console-result.json').read_text())
            self.assertTrue(receipt['challenge_sent'])
            self.assertFalse(receipt['command_line_observed'])
            self.assertIsNone(receipt['error'])

    def test_pair_selects_interfaces_and_requires_one_enumeration(self):
        roots = usb_fixture()
        pair = host.cdc_pair(plistlib.dumps(roots), '/dev/cu.usbmodemPRIMARY')
        self.assertEqual(pair['console'], '/dev/cu.usbmodemVUART')
        for change in ('absent', 'wrong-location', 'primary-as-console', 'duplicate'):
            value = copy.deepcopy(roots)
            if change == 'absent':
                value[0]['IORegistryEntryChildren'].pop()
            elif change == 'wrong-location':
                value[0]['locationID'] += 1
            elif change == 'primary-as-console':
                value[0]['IORegistryEntryChildren'][1]['IORegistryEntryChildren'][0]['IOCalloutDevice'] = pair['primary']
            else:
                value *= 2
            with self.subTest(change=change), self.assertRaises(ValueError):
                host.cdc_pair(plistlib.dumps(value), pair['primary'])

    def test_echo_prechallenge_and_non_darwin_responses_do_not_prove_shell(self):
        p = host.ConsoleProof('0123456789ab'*4)
        response = b'\r\nDL9_BEGIN_' + p.nonce.encode() + b'\r\nDarwin\r\narm64\r\n25.5.0\r\nDL9_END_' + p.nonce.encode() + b'\r\n'
        self.assertFalse(p.feed(response))
        p.sent()
        self.assertNotIn(p.nonce.encode(), p.command())
        self.assertIn(b'{ uname -s; uname -m; uname -r; } 2>/dev/null;', p.command())
        self.assertFalse(p.feed(p.command()))
        self.assertFalse(p.feed(response.replace(b'Darwin', b'Linux')))
        self.assertFalse(p.feed(response.replace(b'arm64', b'x86_64')))
        self.assertFalse(p.feed(response.replace(b'\r\narm64',
            b'\r\nuname(1) malloc: diagnostic must not be filtered\r\narm64')))
        # tty ONLCR can add a second CR to already-CRLF guest output.
        self.assertFalse(p.feed(p.command().replace(b'\r', b'\r\r\n')))
        for byte in response:
            p.feed(bytes([byte]).replace(b'\r', b'\r\r'))
        self.assertTrue(p.complete)
        with self.assertRaises(ValueError):
            p.sent()

    def test_default_plan_does_not_query_usb_open_serial_or_start_process(self):
        with patch.object(host, 'run_worker') as worker, patch.object(host.subprocess, 'run') as command:
            with patch('sys.stdout', new_callable=io.StringIO) as out:
                self.assertEqual(host.main([]), 0)
            self.assertFalse(json.loads(out.getvalue())['device_contacted'])
            worker.assert_not_called()
            command.assert_not_called()


class CallbackTests(unittest.TestCase):
    def test_console_and_core_survive_blocked_optional_diagnostics(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            class Port:
                def read(self, _):
                    threading.Event().wait(.001)
                    return b''
                def close(self):
                    pass
            console = host.ConsoleSession('/host-only-fake', directory,
                                          serial_factory=lambda *a, **kw: Port())
            backend = SimpleNamespace(iface=Mock(), p=SimpleNamespace(P_HV_START=1),
                                      proxy_module=SimpleNamespace(ExcInfo=Mock()))
            backend.proxy_module.ExcInfo.parse.side_effect = ValueError('fake context')
            cap = host.ProgressCapture(SimpleNamespace(), backend, None,
                dict(entry=1, guest_base=2, bootargs_off=3), directory, 10, console=console)
            cap.raw = b'HOST-ONLY CONTEXT'
            entered, release = threading.Event(), threading.Event()
            def blocking():
                entered.set()
                if not release.wait(2):
                    raise AssertionError('test did not release diagnostics')
                return dict(complete=True)
            cap.diagnostics = blocking
            outcomes = []
            def stopping():
                try:
                    cap.stop('test pause')
                except BaseException as error:
                    outcomes.append(error)
            thread = threading.Thread(target=stopping)
            thread.start()
            try:
                self.assertTrue(entered.wait(1))
                self.assertTrue((directory/'console-result.json').exists())
                core = json.loads((directory/'capture-core.json').read_text())
                self.assertTrue(core['console_capture_finalized'])
                self.assertIsNone(core['console_close_error'])
                self.assertFalse(core['diagnostics_complete'])
                self.assertFalse((directory/'exception-context.json').exists())
            finally:
                release.set()
                thread.join(2)
                console.close()
            self.assertFalse(thread.is_alive())
            self.assertEqual(len(outcomes), 1)
            self.assertIsInstance(outcomes[0], host.old.BoundedStop)

    def test_failed_console_close_blocks_diagnostics_and_certification(self):
        with tempfile.TemporaryDirectory() as directory:
            console = SimpleNamespace(close=Mock(side_effect=OSError('finalization failed')))
            backend = SimpleNamespace(iface=Mock(), p=SimpleNamespace(P_HV_START=1),
                                      proxy_module=SimpleNamespace(ExcInfo=Mock()))
            cap = host.ProgressCapture(SimpleNamespace(), backend, None,
                dict(entry=1, guest_base=2, bootargs_off=3), Path(directory), 10, console=console)
            cap.diagnostics = Mock()
            with self.assertRaises(host.old.BoundedStop):
                cap.stop('test close failure')
            core = json.loads((Path(directory)/'capture-core.json').read_text())
            self.assertFalse(core['console_capture_finalized'])
            self.assertIn('finalization failed', core['console_close_error'])
            cap.diagnostics.assert_not_called()

    def test_diagnostic_step_marks_start_and_completion_and_requires_pause(self):
        with tempfile.TemporaryDirectory() as directory:
            backend = SimpleNamespace(iface=Mock(), p=SimpleNamespace(P_HV_START=1),
                                      proxy_module=SimpleNamespace(ExcInfo=Mock()))
            cap = host.ProgressCapture(SimpleNamespace(_in_handler=True), backend, None,
                dict(entry=1, guest_base=2, bootargs_off=3), Path(directory), 10)
            cap.raw, cap.dispatch_depth = b'HOST-ONLY CONTEXT', 1
            with self.assertRaisesRegex(OSError, 'blocked read'):
                cap.diagnostic_read('blocked', Mock(side_effect=OSError('blocked read')))
            self.assertTrue((Path(directory)/'diagnostic-01-started.json').exists())
            self.assertFalse((Path(directory)/'diagnostic-01-completed.json').exists())
            self.assertEqual(cap.diagnostic_read('done', lambda: 7), 7)
            self.assertTrue((Path(directory)/'diagnostic-02-completed.json').exists())
            cap.dispatch_depth = 0
            operation = Mock()
            with self.assertRaises(ValueError):
                cap.diagnostic_read('not-paused', operation)
            operation.assert_not_called()

    def test_keyboard_diagnostics_can_skip_framebuffer_reads(self):
        with tempfile.TemporaryDirectory() as directory:
            ctx = SimpleNamespace(sp=[0, 0, 0], regs=[0]*31)
            backend = SimpleNamespace(iface=Mock(), p=SimpleNamespace(P_HV_START=1),
                u=SimpleNamespace(mrs=Mock(return_value=0)), display_console=True,
                proxy_module=SimpleNamespace(ExcInfo=SimpleNamespace(parse=lambda _: ctx)))
            inputs = SimpleNamespace(report={'kernel':dict(vmin=0, segments=[], diagnostic_symbols={})})
            cap = host.ProgressCapture(SimpleNamespace(_in_handler=True), backend, inputs,
                dict(entry=1, phys_base=0x800000000, guest_base=0x801000000, bootargs_off=0),
                Path(directory), 10, capture_framebuffer=False)
            cap.raw, cap.dispatch_depth = b'HOST-ONLY CONTEXT', 1
            result = cap.diagnostics()
            self.assertTrue(result['complete'])
            self.assertFalse(result['framebuffer_requested'])
            self.assertNotIn('framebuffer', result)
            backend.iface.readmem.assert_not_called()
            self.assertEqual(backend.u.mrs.call_count, 7)

    def test_framebuffer_capture_requires_owned_pause_and_excludes_mcc(self):
        with tempfile.TemporaryDirectory() as directory:
            fields = dict(base=0x900000000, stride=16, width=4, height=2, depth=30)
            pixels = bytes(range(32))
            ctx = SimpleNamespace(regs=[0]*32, sp=[0, 0, 0])
            iface = Mock(); iface.readmem.return_value = pixels
            backend = SimpleNamespace(iface=iface, p=SimpleNamespace(P_HV_START=1),
                u=SimpleNamespace(mrs=lambda _: 1), display_console={},
                ba=SimpleNamespace(phys_base=0x802078000, mem_size_actual=0x400000000,
                                   video=SimpleNamespace(**fields)),
                proxy_module=SimpleNamespace(ExcInfo=SimpleNamespace(parse=lambda _: ctx)))
            inputs = SimpleNamespace(report={'kernel': dict(vmin=image.KC_LINK, segments=[])})
            layout = dict(entry=0x821000000, guest_base=0x821000000,
                          phys_base=0x820000000, bootargs_off=0xc000)
            hv = SimpleNamespace(_in_handler=False)
            cap = host.ProgressCapture(hv, backend, inputs, layout, Path(directory), 10)
            cap.raw = b'FAKE CONTEXT'; cap.dispatch_depth = 1
            with patch.object(host, 'read_carveouts', return_value=[]) as carveouts:
                self.assertEqual(cap.diagnostics(), {})
                carveouts.assert_not_called(); iface.readmem.assert_not_called()
                hv._in_handler = True
                result = cap.diagnostics()
            iface.readmem.assert_called_once_with(fields['base'], len(pixels))
            self.assertEqual((Path(directory)/'framebuffer.bin').read_bytes(), pixels)
            self.assertEqual(result['framebuffer']['sha256'], image.sha(pixels))
            self.assertFalse(result['framebuffer']['physical_panel_observed'])
            iface.reset_mock()
            with patch.object(host, 'read_carveouts', return_value=[(fields['base'], 16)]), \
                    self.assertRaisesRegex(ValueError, 'MCC carveout'):
                cap.diagnostics()
            iface.readmem.assert_not_called()

    def test_visual_text_console_preserves_geometry_and_serial_recovery(self):
        fields = dict(base=0x900000000, stride=10240, width=2560, height=1600, depth=0x1001e)
        backend = SimpleNamespace(ba=SimpleNamespace(phys_base=0x802078000, mem_size_actual=0x400000000))
        for mode in (0, 1, 2):
            video = SimpleNamespace(display=mode, **fields)
            report = host.configure_display_console(SimpleNamespace(tba=SimpleNamespace(video=video)), backend, [])
            self.assertEqual(video.display, 2)
            self.assertEqual({name: getattr(video, name) for name in fields}, fields)
            self.assertFalse(report['physical_panel_observed'])
        for name, value in [('base', 0), ('base', 3), ('depth', 16), ('stride', 100),
                            ('width', 8193), ('height', 8193)]:
            video = SimpleNamespace(display=0, **{**fields, name: value})
            with self.subTest(field=name, value=value), self.assertRaisesRegex(ValueError, 'geometry'):
                host.configure_display_console(SimpleNamespace(tba=SimpleNamespace(video=video)), backend, [])
            self.assertEqual(video.display, 0)
        video = SimpleNamespace(display=0, **fields)
        with self.assertRaisesRegex(ValueError, 'MCC carveout'):
            host.configure_display_console(SimpleNamespace(tba=SimpleNamespace(video=video)),
                                           backend, [(fields['base'], 16384)])
        self.assertEqual(video.display, 0)
        for arg in ('serial=3', 'disable-uart-irq=1', 'serial_video_mirror=1', 'pdtextboot=1'):
            self.assertIn(arg, host.BOOTARGS.split())

    def test_kernel_text_policy_matches_declared_guest_build_and_has_exact_wire_size(self):
        policy = dict(implementation_selectors_omitted=[
            'KERNEL_INTEGRITY_CTRR', 'XNU_MONITOR', 'PMAP_CS', 'PMAP_CS_ENABLE'],
            kernel_text_hardware_locked=False, PPL_protection_claimed=False)
        def inputs(value):
            return SimpleNamespace(contents={'build_record': json.dumps(dict(components=dict(
                native_kernel=dict(development_protection_configuration=value)))).encode()})
        for previous in (None, b'\x01\0\0\0'):
            chosen = SimpleNamespace(_types={})
            if previous is not None:
                chosen.kernel_ctrr_to_be_enabled = previous
            hv = SimpleNamespace(adt={'chosen': chosen})
            result = host.configure_kernel_text_policy(hv, inputs(policy))
            self.assertEqual(chosen.kernel_ctrr_to_be_enabled, struct.pack('<I', 0))
            self.assertEqual(chosen._types['kernel-ctrr-to-be-enabled'], (None, False))
            self.assertEqual(result['bytes'], 4)
            self.assertEqual(result['value'], 0)
            self.assertTrue(result['guest_only'])
        bad = [{}, {**policy, 'kernel_text_hardware_locked': True},
               {**policy, 'kernel_text_hardware_locked': 0},
               {**policy, 'PPL_protection_claimed': True},
               {**policy, 'implementation_selectors_omitted': ['XNU_MONITOR']},
               {**policy, 'implementation_selectors_omitted': None}]
        for value in bad:
            chosen = SimpleNamespace(_types={})
            with self.subTest(policy=value), self.assertRaisesRegex(ValueError, 'build receipt'):
                host.configure_kernel_text_policy(SimpleNamespace(adt={'chosen': chosen}), inputs(value))
            self.assertFalse(hasattr(chosen, 'kernel_ctrr_to_be_enabled'))

    def test_entropy_is_256_fresh_os_bytes_and_failure_stops(self):
        chosen = SimpleNamespace(_types={})
        hv = SimpleNamespace(adt={'chosen': chosen})
        with patch.object(host.secrets, 'token_bytes', side_effect=[b'A'*256, b'B'*256, b'bad']) as random:
            self.assertEqual(host.configure_entropy(hv)['bytes'],256)
            self.assertEqual(chosen.random_seed,b'A'*256)
            self.assertEqual(chosen._types['random-seed'],(None,False))
            host.configure_entropy(hv)
            self.assertEqual(chosen.random_seed,b'B'*256)
            with self.assertRaisesRegex(ValueError,'entropy unavailable'):
                host.configure_entropy(hv)
            self.assertEqual(random.call_args_list,[((256,),),((256,),),((256,),)])

    def test_diagnostics_only_read_loaded_data_in_owned_paused_callback(self):
        with tempfile.TemporaryDirectory() as directory:
            base, guest, phys = image.KC_LINK, 0x821000000, 0x820000000
            runtime = 0xfffffe0010000000 + (guest-phys)
            ctx = SimpleNamespace(regs=[0]*32, sp=[0,0,0])
            ctx.regs[29] = runtime+0x7000
            iface = Mock();iface.readmem.side_effect=lambda addr,size: b'\x01'+b'\0'*(size-1)
            backend = SimpleNamespace(iface=iface, p=SimpleNamespace(P_HV_START=1),
                                      u=SimpleNamespace(mrs=lambda _: 1),
                                      proxy_module=SimpleNamespace(ExcInfo=SimpleNamespace(parse=lambda _: ctx)))
            report = dict(vmin=base, segments=[dict(vmaddr=base+0x4000,vmsize=0x8000,initprot=3)],
                          diagnostic_symbols={'_darwin_lab_m1_progress':dict(address=base+0x5000,bytes=8)})
            inputs = SimpleNamespace(report={'kernel':report})
            layout = dict(entry=guest,guest_base=guest,phys_base=phys,bootargs_off=0xc000)
            hv = SimpleNamespace(_in_handler=False)
            cap=host.ProgressCapture(hv,backend,inputs,layout,Path(directory),10)
            cap.raw=b'FAKE CONTEXT';cap.event=dict(reason=3,code=3,info=1)
            self.assertEqual(cap.diagnostics(),{})
            iface.readmem.assert_not_called()
            hv._in_handler=True;cap.dispatch_depth=1
            result=cap.diagnostics()
            self.assertEqual(result['globals']['_darwin_lab_m1_progress'],1)
            self.assertEqual(result['bootstrap_stack']['bytes'],8192)
            for call in iface.readmem.call_args_list:
                address,size=call.args
                self.assertTrue(guest+0x4000<=address<address+size<=guest+0xc000)
            report['diagnostic_symbols']['_darwin_lab_m1_progress']['address']=base+0x1000
            iface.reset_mock()
            with self.assertRaisesRegex(ValueError,'outside loaded kernel data'):
                cap.diagnostics()
            iface.readmem.assert_not_called()

    def test_mcc_table_is_bounded_and_guest_reservation_excludes_intersections(self):
        backend = SimpleNamespace(p=Mock(), iface=Mock(),
                                  ba=SimpleNamespace(phys_base=0x800000000, mem_size=8 << 30,
                                                     mem_size_actual=16 << 30, devtree_size=0x60000),
                                  snapshot={'memory_map': {'TrustCache': {'size': 0x8000}}})
        backend.p.mcc_get_carveouts.return_value = 0x800100000
        backend.iface.readmem.return_value = struct.pack('<10Q', 0x810000000, 0x4000, *([0]*8))
        ranges = host.read_carveouts(backend)
        self.assertEqual(ranges, [(0x810000000, 0x4000)])
        backend.iface.readmem.return_value = struct.pack('<10Q', 0xb10000000, 0x4000, *([0]*8))
        self.assertEqual(host.read_carveouts(backend), [(0xb10000000, 0x4000)])
        inputs = SimpleNamespace(contents={'ramdisk': b'ROOT'})
        layout = dict(phys_base=0x820000000, guest_base=0x821000000, bootargs_off=0x8000)
        value = host.check_reservation(backend, inputs, layout, ranges)
        for address, size, passes in [(value['start']-0x4000, 0x4000, True),
                                      (value['end'], 0x4000, True), (value['start'], 1, False),
                                      (value['end']-1, 1, False)]:
            if passes:
                host.check_reservation(backend, inputs, layout, [(address, size)])
            else:
                with self.assertRaisesRegex(ValueError, 'intersects'):
                    host.check_reservation(backend, inputs, layout, [(address, size)])
        backend.iface.readmem.return_value = struct.pack('<10Q', *([0x810000000,0x4000]*5))
        with self.assertRaisesRegex(ValueError, 'sentinel'):
            host.read_carveouts(backend)

    def test_context_parse_failure_preserves_raw_bytes_and_event(self):
        with tempfile.TemporaryDirectory() as directory:
            hv = SimpleNamespace(_info_data=b'ORIGINAL UNPARSEABLE CONTEXT')
            backend = SimpleNamespace(iface=Mock(), p=SimpleNamespace(P_HV_START=1),
                                      proxy_module=SimpleNamespace(ExcInfo=Mock()))
            backend.proxy_module.ExcInfo.parse.side_effect = ValueError('synthetic parse failure')
            layout = dict(entry=1, guest_base=2, bootargs_off=3)
            cap = host.ProgressCapture(hv, backend, None, layout, Path(directory), 90)
            cap.event = dict(reason=2, code=0, info=0x800100000)
            cap.original_load_context = Mock(side_effect=ValueError('synthetic parse failure'))
            with self.assertRaises(host.old.BoundedStop):
                cap.load_context()
            self.assertEqual((Path(directory)/'exception-context.bin').read_bytes(), hv._info_data)
            record = json.loads((Path(directory)/'exception-context.json').read_text())
            self.assertEqual(record['event'], cap.event)
            self.assertIn('synthetic parse failure', record['context_error'])

    def test_nested_diagnostic_callback_cannot_replace_original_context(self):
        with tempfile.TemporaryDirectory() as directory:
            backend=SimpleNamespace(iface=Mock(),p=SimpleNamespace(P_HV_START=1),
                                    proxy_module=SimpleNamespace(ExcInfo=Mock()))
            backend.proxy_module.ExcInfo.parse.side_effect=ValueError('synthetic raw context')
            cap=host.ProgressCapture(SimpleNamespace(),backend,None,
                    dict(entry=1,guest_base=2,bootargs_off=3),Path(directory),10)
            original=dict(reason=3,code=3,info=0x800100000)
            cap.event=original;cap.raw=b'ORIGINAL CONTEXT'
            def nested():
                self.assertEqual((Path(directory)/'exception-context.bin').read_bytes(),b'ORIGINAL CONTEXT')
                cap.event=dict(reason=1,code=0,info=0x800200000)
                cap.raw=b'NESTED CONTEXT'
                raise host.old.BoundedStop('synthetic nested callback')
            cap.diagnostics=nested
            with self.assertRaises(host.old.BoundedStop):
                cap.stop('original stop')
            record=json.loads((Path(directory)/'exception-context.json').read_text())
            self.assertEqual(record['event'],original)
            self.assertEqual(record['raw_context_sha256'],image.sha(b'ORIGINAL CONTEXT'))
            self.assertTrue(cap.closed)

    def test_adt_and_trust_cache_are_preserved_after_kernel_before_ramdisk(self):
        pair_codec = SimpleNamespace(build=lambda pair: struct.pack('<QQ', *pair))
        mm = SimpleNamespace(TrustCache=(0x820100000, 0x8000), _types={'TrustCache': (pair_codec,False)})
        class ADT(dict):
            def build(self):
                return b'HOST-ONLY ADT BYTES'
        tree = ADT(chosen={'memory-map': mm})
        tba = SimpleNamespace(phys_base=0x820000000, virt_base=0xfffffe0010000000, revision=2)
        hv = SimpleNamespace(adt=tree, tba=tba, guest_base=0x821000000, bootargs_off=0x8000)
        p, u, iface = Mock(), Mock(), Mock()
        backend = SimpleNamespace(p=p, u=u, iface=iface,
                                  ba=SimpleNamespace(phys_base=0x800000000, mem_size=8 << 30, devtree_size=0x60000),
                                  types=SimpleNamespace(BootArgs_r2=SimpleNamespace(build=lambda _: b'BOOTARGS')))
        result = host.load_payloads(hv, backend, b'RAM-ROOT')
        self.assertEqual(hv.adt_base, 0x82100c000)
        self.assertEqual(mm.DeviceTree, (hv.adt_base, 0x60000))
        self.assertEqual(mm.TrustCache, (hv.adt_base+0x60000, 0x8000))
        self.assertEqual(mm.RAMDisk, (hv.adt_base+0x68000, 8))
        self.assertEqual(tba.top_of_kernel_data, hv.adt_base+0x6c000)
        self.assertEqual(tba.devtree, hv.adt_base-tba.phys_base+tba.virt_base)
        self.assertEqual(tba.devtree_size, len(tree.build()))
        p.memcpy8.assert_called_once_with(mm.TrustCache[0], 0x820100000, 0x8000)
        iface.readmem.assert_not_called()
        self.assertFalse(result['trust_cache']['read_by_host'])
        # An out-of-RAM payload must fail before *any* data transfer.
        u.reset_mock(); p.reset_mock(); iface.reset_mock()
        backend.ba.mem_size = 0x100000
        with self.assertRaises(host.old.Error):
            host.load_payloads(hv, backend, b'RAM-ROOT')
        p.memcpy8.assert_not_called()
        u.compressed_writemem.assert_not_called()
        iface.writemem.assert_not_called()

    def test_handled_sysreg_and_mmio_only_acknowledgements_resume(self):
        for event, ret, permitted in [((3,1),2,True), ((2,0),2,True), ((2,2),2,True),
                                      ((3,3),2,False), ((3,7),2,False), ((3,1),3,False)]:
            with self.subTest(event=event, ret=ret), tempfile.TemporaryDirectory() as directory:
                iface = SimpleNamespace(REQ_PROXY=0xaa)
                proxy = SimpleNamespace(P_HV_START=0xc00, P_EXIT=0x30)
                backend = SimpleNamespace(iface=iface, p=proxy, proxy_module=SimpleNamespace(ExcInfo=None))
                hv = SimpleNamespace(started=True, _sigint_pending=False)
                layout = dict(entry=0x820000000, guest_base=0x820000000, bootargs_off=0x4000)
                cap = host.ProgressCapture(hv, backend, None, layout, Path(directory), 90)
                cap.event = dict(reason=event[0], code=event[1], info=0)
                cap.original_cmd = Mock(return_value='ack')
                payload = struct.pack('<7Q', proxy.P_EXIT, ret, 0, 0, 0, 0, 0)
                if permitted:
                    self.assertEqual(cap.cmd(iface.REQ_PROXY, payload), 'ack')
                else:
                    with self.assertRaises(host.old.BoundedStop):
                        cap.cmd(iface.REQ_PROXY, payload)
                    cap.original_cmd.assert_not_called()


if __name__ == '__main__':
    unittest.main()
