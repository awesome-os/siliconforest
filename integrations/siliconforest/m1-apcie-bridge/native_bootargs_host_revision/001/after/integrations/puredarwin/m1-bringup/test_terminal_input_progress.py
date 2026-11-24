#!/usr/bin/env python3
"""Host-only typing barriers and real US XKB translation, no device access."""
import ctypes
import ctypes.util
import unittest
from terminal_input_progress import TerminalInput, command, reports, us_key, key_report

N = '0123456789abcdef' * 3


class TerminalScheduleTests(unittest.TestCase):
    def setUp(self):
        self.s, self.now = TerminalInput(N), 0.

    def line(self, line):
        self.s.feed((line + '\n').encode(), self.now)

    def drain(self, remaining=0):
        out = []
        while len(self.s.pending) > remaining:
            p = self.s.tick(self.now)
            if p:
                out.append(p)
            self.now += .1
        return out

    def ready(self):
        self.line('DL11_TERMINAL_PRIME_' + N)
        self.drain()
        self.line(f'DL11_TERMINAL_READY {N} {self.s.READY}')

    def response(self):
        self.line(f'DL11_TERMINAL_RESPONSE {N} {self.s.RESPONSE}')

    def test_command_is_fixed_split_nonce_and_bounded(self):
        c = command(N)
        self.assertNotIn(N.encode(), c)
        self.assertEqual(c.count(b'\n'), 1)
        self.assertTrue(c.endswith(b'/bin/uname -r\n'))
        self.assertEqual(len(reports(N)), 2 * len(c))
        self.assertLessEqual(sum(map(len, reports(N))), 8192)
        for bad in ('', 'A' * 48, 'x' * 48, N + ';', None):
            with self.assertRaises(ValueError):
                command(bad)

    def test_enter_occurs_only_at_the_end_and_every_key_is_released(self):
        p = reports(N)
        self.assertEqual(p[-2:], [key_report(0, 0x28), key_report()])
        self.assertEqual(sum(x == key_report(0, 0x28) for x in p), 1)
        self.assertTrue(all(x == key_report() for x in p[1::2]))
        self.assertTrue(all(x.startswith(b'\x01k ') and x.endswith(b'\n') and b'\r' not in x for x in p))

    def test_wrong_nonce_and_unsolicited_commands_do_not_trigger_typing(self):
        self.line('DL11_TERMINAL_PRIME_' + 'a' * 48)
        self.line('DL11_TERMINAL_READY ' + N + ' command=anything')
        self.assertIsNone(self.s.tick(1))
        self.assertEqual(self.s.actions, [])

    def test_expired_fragmented_ready_cannot_reset_deadline(self):
        self.line('DL11_TERMINAL_PRIME_' + N)
        self.drain()
        data = f'DL11_TERMINAL_READY {N} {self.s.READY}\n'.encode()
        self.s.feed(data[:-1], 29.)
        self.s.feed(data[-1:], 30.)
        self.assertIsNotNone(self.s.error)
        self.assertIsNone(self.s.tick(30.))

    def test_response_before_enter_press_fails(self):
        self.ready()
        self.drain(2)
        self.response()
        self.assertIsNotNone(self.s.error)
        self.assertIsNone(self.s.tick(self.now))

    def test_fast_response_still_requires_final_neutral_release(self):
        self.ready()
        self.drain(1)  # Enter-down was emitted; release is still pending.
        self.response()
        self.assertFalse(self.s.report()['schedule_completed'])
        self.assertEqual(self.drain(), [key_report()])
        self.assertTrue(self.s.report()['schedule_completed'])
        self.assertTrue(self.s.response_observed)

    def test_response_after_release_and_later_error(self):
        self.ready()
        self.drain()
        self.assertEqual(self.s.phase, 'await_response')
        self.assertFalse(self.s.report()['schedule_completed'])
        self.response()
        self.assertTrue(self.s.report()['schedule_completed'])
        self.line('darwin-wayland-terminal: late failure')
        self.assertFalse(self.s.report()['schedule_completed'])

    def test_timeout_and_cancel_never_send_a_guessed_suffix(self):
        self.ready()
        first = self.s.tick(self.now)
        self.assertIsNotNone(first)
        count = len(self.s.actions)
        self.assertIsNone(self.s.tick(self.now + 56))
        self.assertIsNotNone(self.s.error)
        self.assertEqual(len(self.s.actions), count)
        self.setUp()
        self.ready()
        self.s.cancel()
        self.assertIsNone(self.s.tick(self.now))

    def test_no_retry_or_duplicate_proof(self):
        self.ready()
        self.line('DL11_TERMINAL_PRIME_' + N)
        self.assertIsNotNone(self.s.error)
        self.setUp()
        self.ready()
        self.drain()
        self.response()
        self.response()
        self.assertFalse(self.s.report()['schedule_completed'])

    def test_report_keeps_physical_and_pixel_claims_separate(self):
        self.ready()
        self.drain()
        self.response()
        r = self.s.report()
        self.assertFalse(r['physical_keyboard_claimed'])
        self.assertFalse(r['physical_trackpad_claimed'])
        self.assertIn('separate evidence', r['scope'])
        self.assertLessEqual(len(r['actions']), 400)
        for character in ('\x00', '\t', 'é', '`'):
            with self.assertRaises(ValueError):
                us_key(character)


class XkbReferenceTests(unittest.TestCase):
    def test_fixed_command_translates_through_real_us_keymap(self):
        name = ctypes.util.find_library('xkbcommon')
        if not name:
            self.skipTest('Host libxkbcommon unavailable; no reference translation evidence')
        lib = ctypes.CDLL(name)
        ptr, u = ctypes.c_void_p, ctypes.c_uint32
        class Names(ctypes.Structure):
            _fields_ = [(n, ctypes.c_char_p) for n in ('rules', 'model', 'layout', 'variant', 'options')]
        for function, args, result in [
            ('xkb_context_new', [u], ptr), ('xkb_keymap_new_from_names', [ptr, ctypes.POINTER(Names), u], ptr),
            ('xkb_state_new', [ptr], ptr), ('xkb_keymap_mod_get_index', [ptr, ctypes.c_char_p], u),
            ('xkb_state_update_mask', [ptr, u, u, u, u, u, u], u),
            ('xkb_state_key_get_utf8', [ptr, u, ctypes.c_void_p, ctypes.c_size_t], ctypes.c_int),
            ('xkb_state_unref', [ptr], None), ('xkb_keymap_unref', [ptr], None), ('xkb_context_unref', [ptr], None)]:
            f = getattr(lib, function); f.argtypes = args; f.restype = result
        context = lib.xkb_context_new(0)
        self.assertTrue(context)
        keymap = state = None
        try:
            names = Names(b'evdev', b'pc105', b'us', b'', b'')
            keymap = lib.xkb_keymap_new_from_names(context, ctypes.byref(names), 0)
            self.assertTrue(keymap)
            state = lib.xkb_state_new(keymap)
            self.assertTrue(state)
            shift = lib.xkb_keymap_mod_get_index(keymap, b'Shift')
            self.assertLess(shift, 32)
            # HID -> evdev contract from the frozen real PDHID decoder.
            letters = [30,48,46,32,18,33,34,35,23,36,37,38,50,49,24,25,16,19,31,20,22,47,17,45,21,44]
            extra = {0x27:11, 0x28:28, 0x2c:57, 0x2d:12, 0x31:43, 0x33:39, 0x34:40, 0x38:53}
            translated = bytearray()
            for character in command(N).decode():
                mods, usage = us_key(character)
                code = letters[usage-4] if 4 <= usage <= 0x1d else usage-0x1e+2 if 0x1e <= usage <= 0x26 else extra[usage]
                lib.xkb_state_update_mask(state, (1 << shift) if mods else 0, 0, 0, 0, 0, 0)
                buffer = ctypes.create_string_buffer(32)
                length = lib.xkb_state_key_get_utf8(state, code+8, buffer, len(buffer))
                self.assertGreater(length, 0)
                self.assertLess(length, len(buffer))
                translated.extend(buffer.raw[:length])
            self.assertEqual(bytes(translated), command(N).replace(b'\n', b'\r'))
        finally:
            if state: lib.xkb_state_unref(state)
            if keymap: lib.xkb_keymap_unref(keymap)
            lib.xkb_context_unref(context)


if __name__ == '__main__':
    unittest.main()
