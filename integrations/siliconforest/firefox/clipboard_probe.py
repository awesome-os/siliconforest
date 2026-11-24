# SPDX-License-Identifier: MIT
"""Native Firefox copy/paste with a RAM-only, conditional clipboard restore."""
import hashlib
import json
from pathlib import Path
import secrets
import select
import subprocess


def measure(client, helper, chord):
    if not helper.startswith('/private/var/db/sffx/') or '..' in Path(helper).parts:
        raise ValueError('Clipboard guard must be in the private prefix')
    copy = 'SF-FX-copy-' + secrets.token_hex(8) + ' — δ'
    paste = 'SF-FX-paste-' + secrets.token_hex(8) + ' — λ'
    process = subprocess.Popen([helper, copy, paste], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.DEVNULL, text=True)
    start = subprocess.check_output(['ps', '-p', str(process.pid), '-o', 'lstart='], text=True).strip()
    result = dict(synthetic_input=True, original_clipboard_data_logged=False)

    def reply():
        if not select.select([process.stdout], [], [], 4)[0]:
            raise RuntimeError('Clipboard guard response timeout')
        return json.loads(process.stdout.readline())

    def command(value):
        process.stdin.write(value + '\n')
        process.stdin.flush()
        return reply()

    def content(source, args=None):
        client.command('Marionette:SetContext', {'value': 'content'})
        return client.command('WebDriver:ExecuteScript', dict(script=source, args=args or [], newSandbox=False))

    try:
        result['guard'] = reply()
        if not result['guard'].get('ready'):
            return dict(result, complete=False)
        content('const e=document.querySelector("#typing");e.value=arguments[0];e.focus();e.select();return true;', [copy])
        chord(0x06)  # Command-C traverses the compositor seat and AppKit.
        result['copy'] = command('check-copy')
        result['write'] = command('write-paste')
        if not result['write'].get('written'):
            raise RuntimeError('Owned paste text was not written; refused paste')
        content('const e=document.querySelector("#typing");e.value="";e.focus();return true;')
        chord(0x19)  # Command-V
        actual = content('return document.querySelector("#typing").value;')
        result['paste'] = dict(matches=actual == paste, utf8_bytes=len(actual.encode()),
                               sha256=hashlib.sha256(actual.encode()).hexdigest())
    finally:
        if process.poll() is None and result.get('guard', {}).get('ready'):
            try:
                result['restore'] = command('restore')
            except (OSError, RuntimeError, ValueError) as error:
                result['restore_error'] = str(error)
        process.stdin.close()
        try:
            result['guard_status'] = process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            current = subprocess.run(['ps', '-p', str(process.pid), '-o', 'lstart='], capture_output=True, text=True).stdout.strip()
            if current == start:
                process.kill()
            result['guard_status'] = process.wait(timeout=2)
    result['complete'] = bool(result.get('copy', {}).get('copy_matches') and result.get('write', {}).get('written') and
        result.get('paste', {}).get('matches') and result.get('restore', {}).get('restored') and result['guard_status'] == 0)
    return result
