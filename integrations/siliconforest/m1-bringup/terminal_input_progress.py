"""Separate, finite host-origin typing for a real PTY terminal qualification.

Not wired to the launcher until the real terminal and PTY prerequisite pass.
Guest output opens fixed barriers; it never chooses a command or a key.
"""
import hashlib
import re
from virtual_input_progress import VirtualInput


def command(nonce):
    if not isinstance(nonce, str) or not re.fullmatch('[0-9a-f]{48}', nonce):
        raise ValueError('Invalid terminal nonce')
    a, b = nonce[:24], nonce[24:]
    out = ("printf '\\nDLT_%s%s\\n' '" + a + "' '" + b +
           "'; /bin/uname -s; /bin/uname -m; /bin/uname -r\n").encode('ascii')
    if nonce.encode() in out or len(out) > 180:
        raise ValueError('Terminal challenge exceeds its fixed contract')
    return out


def us_key(character):
    """USB boot-keyboard usages for this fixed ASCII command, US layout only."""
    if 'a' <= character <= 'z':
        return 0, 4 + ord(character) - ord('a')
    if 'A' <= character <= 'Z':
        return 2, 4 + ord(character) - ord('A')
    if '1' <= character <= '9':
        return 0, 0x1e + ord(character) - ord('1')
    table = {'0': (0, 0x27), ' ': (0, 0x2c), '-': (0, 0x2d),
             '_': (2, 0x2d), '\\': (0, 0x31), ';': (0, 0x33),
             "'": (0, 0x34), '%': (2, 0x22), '/': (0, 0x38), '\n': (0, 0x28)}
    if character not in table:
        raise ValueError('Character outside fixed terminal command alphabet')
    return table[character]


def key_report(modifiers=0, usage=0):
    if type(modifiers) is not int or type(usage) is not int or modifiers not in (0, 2) or not 0 <= usage <= 0x38:
        raise ValueError('Invalid bounded terminal key report')
    return f'\x01k {modifiers} {usage} 0 0 0 0 0\n'.encode('ascii')


def reports(nonce):
    out = []
    for character in command(nonce).decode('ascii'):
        out.extend([key_report(*us_key(character)), key_report()])
    if len(out) > 360 or sum(map(len, out)) > 8192:
        raise ValueError('Fixed terminal typing budget exceeded')
    return out


class TerminalInput(VirtualInput):
    READY = 'origin=host_pdserialhid pty=ready keymap=xkb_v1 focus=own_surface frame=ready'
    RESPONSE = 'origin=pty_shell system=Darwin machine=arm64 release=25.5.0'

    def __init__(self, nonce):
        super().__init__(nonce)
        self.typed_reports = reports(nonce)
        self.response_observed = False

    def line(self, line, now):
        n = self.nonce
        if line == 'DL11_TERMINAL_PRIME_' + n:
            if self.phase != 'await_prime':
                self.fail('Repeated terminal priming barrier')
                return
            self.deadline = now + 30.
            self.queue('priming', [key_report(), key_report(2), key_report(),
                                  b'\x01m 16384 16384 0\n', b'\x01m 16385 16384 0\n'], 'await_ready', now)
        elif line == f'DL11_TERMINAL_READY {n} {self.READY}':
            if self.phase != 'await_ready':
                self.fail('Terminal ready outside its barrier')
                return
            self.deadline = now + 55.
            self.queue('typing', self.typed_reports, 'await_response', now)
        elif line == f'DL11_TERMINAL_RESPONSE {n} {self.RESPONSE}':
            typed = sum(a['phase'] == 'typing' for a in self.actions)
            # The shell may answer after Enter-down but before its release.
            # Still send the final neutral release before reporting completion.
            if (self.phase not in ('typing', 'await_response', 'complete') or
                    typed < len(self.typed_reports) - 1 or self.response_observed):
                self.fail('Uncaused or repeated terminal response')
                return
            self.response_observed = True
            if self.phase == 'await_response':
                self.phase = 'complete'
        elif line.startswith('darwin-wayland-terminal:'):
            self.fail('Guest terminal failed')

    def tick(self, now):
        if self.cancelled or self.error or self.phase == 'complete':
            return None
        if self.deadline is not None and now >= self.deadline:
            self.fail('Terminal input deadline expired')
            return None
        if not self.pending or now < self.next_at:
            return None
        payload = self.pending.pop(0)
        if len(self.actions) >= 400 or sum(a['bytes'] for a in self.actions) + len(payload) > 8192:
            self.fail('Terminal input budget exceeded')
            return None
        self.actions.append({'phase': self.phase, 'at': now, 'bytes': len(payload)})
        self.next_at = now + (.15 if self.phase == 'priming' else .08)
        if not self.pending:
            self.phase = self.after
            if self.phase == 'await_response' and self.response_observed:
                self.phase = 'complete'
        return payload

    def report(self):
        return {**super().report(), 'kind': 'fixed_PTY_terminal_challenge',
                'command_sha256': hashlib.sha256(command(self.nonce)).hexdigest(),
                'command_bytes': len(command(self.nonce)),
                'response_observed': self.response_observed,
                'scope': 'Host emission and guest observation bookkeeping; rendered PTY pixels and launch/recovery are separate evidence.'}
