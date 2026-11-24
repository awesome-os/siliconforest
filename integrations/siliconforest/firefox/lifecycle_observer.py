# SPDX-License-Identifier: MIT
"""Observe an isolated audio document independently of its browser window.

Heartbeat loss is not audio-stop proof. Pair these records with the media
owner's per-process HAL observer and native close/reopen/connection-loss tests.
Only a loopback server is opened; no installed browser or profile is touched.
"""
import io
import ast
import json
import math
import re
from pathlib import Path
import secrets
import socket
import struct
import threading
import time
import wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def window_button(dump, window_id, kind):
    """Use the compositor's actual button center for this admitted window."""
    if kind not in ['Close', 'Miniaturize', 'Maximize']:
        raise ValueError('Unknown compositor button')
    rows = [line for line in dump.splitlines()
            if re.match(r'window %d ' % window_id, line) and 'mapped=true' in line]
    if len(rows) != 1:
        raise ValueError('Expected exactly one mapped owned window')
    frame = re.search(r'frame=Some\((.*?)\)', rows[0])
    bounds = list(map(int, re.findall(r'-?\d+', frame[1]))) if frame else []
    buttons = re.search(r'buttons=(\[[^\]]*\])', rows[0])
    points = [item[1:] for item in ast.literal_eval(buttons[1]) if item[0] == kind] if buttons else []
    if len(bounds) != 4 or len(points) != 1 or len(points[0]) != 2:
        raise ValueError('Owned button geometry absent or ambiguous')
    x, y, width, height = bounds
    tx, ty = points[0]
    if not (type(tx) is int and type(ty) is int and 0 <= tx <= 2559 and 0 <= ty <= 1599 and
            x <= tx < x + width and y <= ty < y + height):
        raise ValueError('Owned button is outside its frame or panel')
    return tx, ty


def close_button(dump, window_id):
    return window_button(dump, window_id, 'Close')


class AudioFixture:
    def __init__(self, directory):
        self.directory = Path(directory)
        self.token = 'sffx-' + secrets.token_hex(8)
        self.events = []
        self.lock = threading.Lock()
        page = Path(__file__).with_name('lifecycle-page.html').read_bytes()
        stream = io.BytesIO()
        with wave.open(stream, 'wb') as out:
            out.setparams((1, 2, 48000, 0, 'NONE', 'not compressed'))
            out.writeframes(b''.join(struct.pack('<h', round(1000 * math.sin(2 * math.pi * 440 * i / 48000)))
                                     for i in range(24000)))
        tone = stream.getvalue()
        fixture = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                path = self.path.partition('?')[0]
                if path == '/lifecycle-page.html':
                    content, kind = page, 'text/html; charset=utf-8'
                elif path == '/tone.wav':
                    content, kind = tone, 'audio/wav'
                else:
                    self.send_error(404)
                    return
                self.send_response(200)
                self.send_header('Content-Type', kind)
                self.send_header('Content-Length', str(len(content)))
                self.end_headers()
                self.wfile.write(content)

            def do_POST(self):
                size = int(self.headers.get('Content-Length', '0'))
                if self.path != '/events' or not 1 <= size <= 16384:
                    self.send_error(400)
                    return
                try:
                    row = json.loads(self.rfile.read(size))
                    if not isinstance(row, dict) or row.get('token') != fixture.token:
                        raise ValueError('Different document token')
                    row['observer_wall_ms'] = time.time() * 1000
                    with fixture.lock:
                        fixture.events.append(row)
                        with (fixture.directory / 'document-events.jsonl').open('a') as out:
                            out.write(json.dumps(row) + '\n')
                except (ValueError, TypeError):
                    self.send_error(400)
                    return
                self.send_response(204)
                self.end_headers()

        self.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.url = 'http://127.0.0.1:%d/lifecycle-page.html?token=%s' % (self.server.server_port, self.token)

    def snapshot(self):
        with self.lock:
            rows = list(self.events)
        return dict(url=self.url, token=self.token, events=rows,
                    heartbeat_is_audio_stop_proof=False)

    def close(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)


def native_menu(pid, command='menu 1'):
    """Caller must first admit this exact PID as its isolated Firefox parent."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(2)
        connection.connect('/private/var/run/gpu-desktop/app-menu/%d' % pid)
        connection.sendall((command + '\n').encode())
        connection.shutdown(socket.SHUT_WR)
        data = bytearray()
        while len(data) < 1024 * 1024:
            block = connection.recv(8192)
            if not block:
                return data.decode('utf-8')
            data.extend(block)
        raise RuntimeError('Oversized owned-process menu reply')


def gecko_windows(client):
    client.command('Marionette:SetContext', {'value': 'chrome'})
    return client.command('WebDriver:ExecuteScript', dict(script='''
      return [...Services.wm.getEnumerator('navigator:browser')].map(w=>({
        closed:w.closed,title:w.document.title,focused:w.document.hasFocus(),
        minimized:w.windowState===w.STATE_MINIMIZED,
        tabs:[...w.gBrowser.browsers].map(b=>({uri:b.currentURI.spec,
          contentPid:b.browsingContext.currentWindowGlobal?.osPid??null}))}));''',
      args=[], newSandbox=False))
