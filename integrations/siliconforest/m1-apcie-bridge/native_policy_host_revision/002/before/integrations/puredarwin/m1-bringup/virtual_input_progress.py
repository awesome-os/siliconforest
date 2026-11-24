"""Finite host-origin PDSerialHID qualification schedule; no transport access.

Only ConsoleSession's existing secondary-CDC owner may send returned bytes.
Guest lines select barriers, never commands, keys, coordinates or retry counts.
This module makes no physical input or device acceptance claim.
"""
import math
import re


def keyboard(usage=0, modifiers=0):
    if type(usage) is not int or usage not in (0, 4) or type(modifiers) is not int or modifiers not in (0, 2):
        raise ValueError('Only release, Shift priming, and A are allowed')
    if usage and modifiers:
        raise ValueError('Modified A is not allowed')
    return f'\x01k {modifiers} {usage} 0 0 0 0 0\n'.encode('ascii')


def pointer(x, y, buttons):
    if any(type(v) is not int for v in (x, y, buttons)) or not (0 <= x < 2560 and 0 <= y < 1600) or buttons not in (0, 1):
        raise ValueError('Invalid fixed-output pointer point')
    # Centre of the requested pixel, rounded to nearest positive integer.
    qx = min(32767, math.floor((x + .5) * 32767 / 2559 + .5))
    qy = min(32767, math.floor((y + .5) * 32767 / 1599 + .5))
    return f'\x01m {qx} {qy} {buttons}\n'.encode('ascii')


class VirtualInput:
    """One schedule, explicit barriers, bounded time/bytes and no retry path."""
    READY = 'origin=host_pdserialhid capabilities=keyboard_pointer keymap=xkb_v1 focus=own_surface modifiers=0 point=200,100'
    OBSERVED = 'origin=host_pdserialhid key=30 presses=1 releases=1 button=272 presses=1 releases=1 point=200,100 tolerance=1'

    def __init__(self, nonce):
        if not isinstance(nonce, str) or not re.fullmatch('[0-9a-f]{48}', nonce):
            raise ValueError('Invalid virtual-input nonce')
        self.nonce = nonce
        self.phase = 'await_prime'
        self.pending = []
        self.after = None
        self.next_at = 0.
        self.deadline = None
        self.error = None
        self.buffer = bytearray()
        self.actions = []
        self.key_pressed = False
        self.cancelled = False

    def fail(self, reason):
        self.error = self.error or reason
        self.pending.clear()
        self.phase = 'failed'

    def queue(self, phase, payloads, after, now):
        self.phase, self.pending, self.after = phase, list(payloads), after
        self.next_at = now

    def feed(self, data, now):
        if self.cancelled or self.error:
            return
        if self.deadline is not None and now >= self.deadline and self.phase != 'complete':
            self.fail('Input schedule deadline expired')
            return
        self.buffer.extend(data.replace(b'\r', b''))
        while b'\n' in self.buffer:
            line, _, rest = self.buffer.partition(b'\n')
            self.buffer[:] = rest
            if len(line) > 8192:
                self.fail('Oversized input barrier line')
                return
            self.line(line.decode('ascii', 'replace'), now)
        if len(self.buffer) > 8192:
            self.fail('Unterminated/oversized input barrier line')

    def line(self, line, now):
        n = self.nonce
        if line == 'DL10_INPUT_PRIME_' + n:
            if self.phase != 'await_prime':
                self.fail('Repeated priming barrier')
                return
            self.deadline = now + 30.
            self.queue('priming', [keyboard(), keyboard(modifiers=2), keyboard(),
                                  b'\x01m 16384 16384 0\n', b'\x01m 16385 16384 0\n'], 'await_ready', now)
        elif line == f'DL10_INPUT_READY {n} {self.READY}':
            if self.phase != 'await_ready':
                self.fail('Input readiness outside its barrier')
                return
            self.deadline = now + 10.
            self.queue('keying', [keyboard(4), keyboard()], 'await_key_release', now)
        elif line.startswith(f'DL10_INPUT_KEY {n} '):
            match = re.fullmatch(r'DL10_INPUT_KEY ' + n + r' origin=host_pdserialhid serial=\d+ time=\d+ key=30 state=(pressed|released)', line)
            if self.phase not in ('keying', 'await_key_release') or not match:
                self.fail('Unexpected key observation')
            elif match[1] == 'pressed' and not self.key_pressed and any(a['phase'] == 'keying' for a in self.actions):
                self.key_pressed = True
            elif match[1] == 'released' and self.key_pressed and self.phase == 'await_key_release':
                self.queue('pointing', [pointer(201, 124, 0)], 'await_point', now)
            else:
                self.fail('Wrong key observation order')
        elif line.startswith(f'DL10_INPUT_POINTER_ENTER {n} '):
            coordinate = r'([0-9]{1,4}(?:\.[0-9]{1,4})?)'
            match = re.fullmatch(r'DL10_INPUT_POINTER_ENTER ' + n + r' origin=host_pdserialhid serial=\d+ x=' + coordinate + ' y=' + coordinate, line)
            if self.phase != 'await_point' or not match:
                self.fail('Pointer entered before the bounded content-point barrier')
                return
            x, y = float(match[1]), float(match[2])
            if not (math.isfinite(x) and math.isfinite(y) and abs(x - 200) <= 1 and abs(y - 100) <= 1):
                self.fail('Pointer coordinate mismatch; no button sent')
                return
            self.queue('clicking', [pointer(201, 124, 1), pointer(201, 124, 0)], 'await_observed', now)
        elif line.startswith(f'DL10_INPUT_POINTER_MOTION {n} ') and self.phase in ('pointing', 'await_point', 'clicking'):
            coordinate = r'([0-9]{1,4}(?:\.[0-9]{1,4})?)'
            match = re.fullmatch(r'DL10_INPUT_POINTER_MOTION ' + n + r' origin=host_pdserialhid time=\d+ x=' + coordinate + ' y=' + coordinate, line)
            if not match or not (abs(float(match[1]) - 200) <= 1 and abs(float(match[2]) - 100) <= 1):
                self.fail('Contradictory/malformed content-point motion; no further button sent')
        elif line == f'DL10_INPUT_OBSERVED {n} {self.OBSERVED}':
            if self.phase != 'await_observed':
                self.fail('Input completion outside its barrier')
                return
            self.queue('dragging', [pointer(x, y, b) for x, y, b in
                       [(200, 12, 0), (200, 12, 1), (250, 52, 1),
                        (300, 92, 1), (400, 172, 1), (400, 172, 0)]], 'complete', now)
        elif line.startswith('darwin-wayland-client-input:'):
            self.fail('Guest input client failed')

    def tick(self, now):
        if self.cancelled or self.error or self.phase == 'complete':
            return None
        if self.deadline is not None and now >= self.deadline:
            self.fail('Input schedule deadline expired')
            return None
        if not self.pending or now < self.next_at:
            return None
        payload = self.pending.pop(0)
        if len(self.actions) >= 24 or sum(x['bytes'] for x in self.actions) + len(payload) > 1024:
            self.fail('Input schedule bound exceeded')
            return None
        self.actions.append({'phase': self.phase, 'at': now, 'bytes': len(payload)})
        self.next_at = now + .15
        if not self.pending:
            self.phase = self.after
        return payload

    def cancel(self):
        self.cancelled = True
        self.pending.clear()

    def report(self):
        return {'origin': 'host_pdserialhid', 'phase': self.phase, 'error': self.error,
                'actions': self.actions, 'cancelled': self.cancelled,
                'schedule_completed': self.phase == 'complete' and self.error is None,
                'physical_keyboard_claimed': False, 'physical_trackpad_claimed': False}
