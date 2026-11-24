#!/usr/bin/env python3
"""Host-only scheduler tests; no transport or device is opened."""
import unittest
from virtual_input_progress import VirtualInput, keyboard, pointer

N = 'a' * 48


class ScheduleTests(unittest.TestCase):
    def setUp(self):
        self.s = VirtualInput(N)
        self.now = 0.

    def line(self, line):
        self.s.feed((line + '\r\n').encode(), self.now)

    def drain(self):
        out = []
        while self.s.pending:
            value = self.s.tick(self.now)
            if value:
                out.append(value)
            self.now += .2
        return out

    def prime(self):
        self.line('DL10_INPUT_PRIME_' + N)
        return self.drain()

    def ready(self):
        self.prime()
        self.line(f'DL10_INPUT_READY {N} {self.s.READY}')
        self.drain()

    def key(self, state):
        self.line(f'DL10_INPUT_KEY {N} origin=host_pdserialhid serial=2 time=1 key=30 state={state}')

    def point(self, x=200, y=100):
        self.line(f'DL10_INPUT_POINTER_ENTER {N} origin=host_pdserialhid serial=3 x={x:.4f} y={y:.4f}')

    def after_keys(self):
        self.ready()
        self.key('pressed')
        self.key('released')
        self.drain()

    def test_release_shift_prime_without_printable_key_or_tty_delimiter(self):
        packets = self.prime()
        self.assertEqual(packets[:3], [keyboard(), keyboard(modifiers=2), keyboard()])
        self.assertTrue(all(p.startswith(b'\x01') and p.endswith(b'\n') and b'\r' not in p for p in packets))
        self.assertEqual(self.s.phase, 'await_ready')

    def test_no_barrier_or_wrong_nonce_never_sends(self):
        self.s.feed(('DL10_INPUT_PRIME_' + 'b' * 48 + '\n').encode(), 0)
        self.assertIsNone(self.s.tick(999))
        self.assertEqual(self.s.actions, [])

    def test_fragmented_lines_and_no_data_ticks_are_paced(self):
        packet = ('DL10_INPUT_PRIME_' + N + '\n').encode()
        for byte in packet:
            self.s.feed(bytes([byte]), 0)
        self.assertEqual(self.s.tick(0), keyboard())
        self.assertIsNone(self.s.tick(.149))
        self.assertEqual(self.s.tick(.15), keyboard(modifiers=2))

    def test_key_release_before_press_is_failure(self):
        self.ready()
        self.key('released')
        self.assertIsNotNone(self.s.error)
        self.assertIsNone(self.s.tick(self.now + 1))

    def test_only_expected_key_is_accepted(self):
        self.ready()
        self.line(f'DL10_INPUT_KEY {N} origin=host_pdserialhid serial=1 time=1 key=31 state=pressed')
        self.assertIsNotNone(self.s.error)

    def test_coordinate_mismatch_stops_before_button(self):
        self.after_keys()
        count = len(self.s.actions)
        self.point(202, 100)
        self.assertIsNotNone(self.s.error)
        self.assertIsNone(self.s.tick(self.now))
        self.assertEqual(len(self.s.actions), count)

    def test_completion_marker_cannot_skip_actual_barriers(self):
        self.prime()
        self.line(f'DL10_INPUT_OBSERVED {N} {self.s.OBSERVED}')
        self.assertIsNotNone(self.s.error)
        self.assertFalse(self.s.report()['schedule_completed'])

    def test_repeated_priming_is_not_a_retry(self):
        self.prime()
        self.line('DL10_INPUT_PRIME_' + N)
        self.assertIsNotNone(self.s.error)
        self.assertFalse(self.s.pending)

    def test_deadline_and_close_cancel_all_future_sends(self):
        self.prime()
        self.assertIsNone(self.s.tick(30))
        self.assertIsNotNone(self.s.error)
        self.setUp()
        self.line('DL10_INPUT_PRIME_' + N)
        self.s.cancel()
        self.assertIsNone(self.s.tick(0))
        self.assertEqual(self.s.actions, [])

    def test_ready_cannot_resurrect_expired_or_fragmented_schedule(self):
        for when in (30., 31.):
            self.setUp()
            self.prime()
            count = len(self.s.actions)
            line = f'DL10_INPUT_READY {N} {self.s.READY}\n'.encode()
            self.s.feed(line[:-1], 29.)
            self.s.feed(line[-1:], when)
            self.assertIsNotNone(self.s.error)
            self.assertIsNone(self.s.tick(when))
            self.assertEqual(len(self.s.actions), count)

    def test_press_observation_requires_down_emission(self):
        self.prime()
        self.line(f'DL10_INPUT_READY {N} {self.s.READY}')
        self.key('pressed')
        self.assertIsNotNone(self.s.error)
        self.assertIsNone(self.s.tick(self.now))

    def test_contradictory_motion_cancels_click_in_same_chunk(self):
        for point in ('500.0000', '..', 'NaN', 'Inf'):
            self.setUp()
            self.after_keys()
            data = (f'DL10_INPUT_POINTER_ENTER {N} origin=host_pdserialhid serial=3 x=200.0000 y=100.0000\n'
                    f'DL10_INPUT_POINTER_MOTION {N} origin=host_pdserialhid time=4 x={point} y=100.0000\n')
            count = len(self.s.actions)
            self.s.feed(data.encode(), self.now)
            self.assertIsNotNone(self.s.error)
            self.assertIsNone(self.s.tick(self.now))
            self.assertEqual(len(self.s.actions), count)

    def test_malformed_enter_is_sticky_without_throwing(self):
        for value in ('..', '1.2.3', 'NaN', 'Inf', '-1.0000', '99999.0000'):
            self.setUp()
            self.after_keys()
            self.line(f'DL10_INPUT_POINTER_ENTER {N} origin=host_pdserialhid serial=3 x={value} y=100.0000')
            self.assertIsNotNone(self.s.error)
            self.point()
            self.assertIsNone(self.s.tick(self.now))

    def test_same_point_motion_keeps_click_and_endpoints_are_bounded(self):
        self.after_keys()
        self.point()
        self.line(f'DL10_INPUT_POINTER_MOTION {N} origin=host_pdserialhid time=4 x=200.0000 y=100.0000')
        self.assertEqual(self.drain(), [pointer(201, 124, 1), pointer(201, 124, 0)])
        for x, y in [(0, 0), (2559, 1599)]:
            values = pointer(x, y, 0)[3:-1].split()
            self.assertTrue(all(0 <= int(v) <= 32767 for v in values[:2]))

    def test_valid_sequence_is_bounded_and_finishes_released(self):
        self.after_keys()
        self.point()
        self.assertEqual(self.drain(), [pointer(201, 124, 1), pointer(201, 124, 0)])
        self.line(f'DL10_INPUT_OBSERVED {N} {self.s.OBSERVED}')
        drag = self.drain()
        self.assertEqual(drag[-1], pointer(400, 172, 0))
        self.assertEqual(len(drag), 6)
        self.assertTrue(self.s.report()['schedule_completed'])
        self.assertLessEqual(len(self.s.actions), 24)
        self.assertLessEqual(sum(x['bytes'] for x in self.s.actions), 1024)
        self.assertFalse(self.s.report()['physical_keyboard_claimed'])

    def test_bad_client_line_and_oversized_partial_line_fail(self):
        for data in (b'darwin-wayland-client-input: failed\n', b'x' * 8193):
            s = VirtualInput(N)
            s.feed(data, 0)
            self.assertIsNotNone(s.error)

    def test_complete_lines_are_removed_before_partial_line_bound(self):
        self.s.feed(b'x' * 6000, 0)
        self.s.feed(b'x' * 2000 + b'\n' + b'ignored\n' * 200, 0)
        self.assertIsNone(self.s.error)
        self.line('DL10_INPUT_PRIME_' + N)
        self.assertEqual(self.s.tick(0), keyboard())

    def test_framing_rejects_noninteger_boolean_and_arbitrary_keys(self):
        for args in [(True, 0), (4, 2), (0x28, 0), ('4', 0)]:
            with self.assertRaises(ValueError):
                keyboard(*args)
        for args in [(2560, 0, 0), (0, 1600, 0), (-1, 0, 0), (0, 0, 2), (True, 0, 0)]:
            with self.assertRaises(ValueError):
                pointer(*args)


if __name__ == '__main__':
    unittest.main()
