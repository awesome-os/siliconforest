#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Boot a Cocoa run image in QEMU (arm64 `virt`, TCG, headless) and drive it from a script.

The machine, the QMP and serial plumbing are the QEMU stream's (qd_run.py: `qemu_command`,
`Qmp`, `Serial`, imported). What is added here is a step list, because Mac applications are
checked from outside: the pointer is a USB tablet (absolute positions, so a click lands where
the script says), keys are QMP key events, and every check is a regular expression that must
appear on the guest console (the applications print one line per observable event).

  cocoa_run.py --image runs/cocoa/<run>/disk.img --out runs/cocoa/<run> --script steps.json

steps.json is a list of steps, run in order after the session marker; the first failure ends
the run (later steps are reported as not run):
  {"wait": "REGEX", "timeout": 120}            a console line (searched after the previous match)
  {"sleep": 2.5}
  {"shot": "name"}                              runs/cocoa/<run>/shots/name.png
  {"move": [x, y]}                              pointer to framebuffer pixel x,y
  {"click": [x, y], "button": "left"}           move, press, release
  {"drag": [x0, y0, x1, y1]}
  {"key": ["ctrl", "q"]}                        one chord (QMP qcodes)
  {"type": "text"}                              letters, digits, space, a few punctuation keys
  {"expect": "REGEX", "timeout": 20, "name": "button"}   as wait, and recorded as a named check
      with "assert": "PYTHON EXPRESSION" the check also needs the expression to hold; g is the list
      of the match's groups (numbers where they are numbers), W and H the framebuffer size
      with "save": "NAME" the groups are kept under that name for later expressions
  {"count": "REGEX", "equals": 2, "name": "..."}  how often the pattern has appeared so far: a check
  {"check": "EXPRESSION", "name": "..."}        an expression over saved groups and n('REGEX') (how
                                                often a pattern has appeared so far): a check
  {"all": "REGEX", "assert": "EXPRESSION", "name": "..."}   the expression must hold for the groups (g)
                                                of every appearance of the pattern so far: a check
  {"click_px": ["X EXPR", "Y EXPR"], "button": "left"}   a click at a framebuffer pixel worked out from
  {"move_px": ["X EXPR", "Y EXPR"]}             saved groups: names from "save", S (pixels per point of
                                                the last located rectangle), W, H
  {"absent": "REGEX", "name": "no crash"}       the pattern must not have appeared so far
  {"locate": "view", "rgb": [r, g, b], "points_wide": 380, "scale": 2.0}
                                                finds a rectangle of that colour on the screen (the
                                                application draws it), measures pixels per point
                                                and the ink height of the text lines inside it
  {"click_in": "view", "points": [dx, dy]}      a click placed from that rectangle's top left corner, in points
  {"move_in": "view", "points": [dx, dy]}       the pointer placed the same way, no button
  {"drag_in": "view", "from": [dx, dy], "to": [dx, dy], "hold": 0.8}   press at from, hold, move to to, release
Exit 0: every step done and every check passed. 1: a check failed. 2: could not run.
result.json: status, times, per-step results, screenshots.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import threading
import time

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'qemu-dev'))
import qd_run  # noqa: E402

QCODES = {' ': 'spc', '\n': 'ret', '.': 'dot', ',': 'comma', '-': 'minus', '=': 'equal', '/': 'slash', ';': 'semicolon',
          "'": 'apostrophe', '\t': 'tab'}
SHIFTED = {':': 'semicolon', '_': 'minus', '+': 'equal', '?': 'slash', '!': '1', '@': '2', '#': '3', '$': '4', '%': '5',
           '&': '7', '*': '8', '(': '9', ')': '0', '"': 'apostrophe', '>': 'dot', '<': 'comma', '|': 'backslash'}


class Driver:
    def __init__(self, qmp, ser, proc, out, size):
        self.qmp, self.ser, self.proc, self.out = qmp, ser, proc, out
        self.w, self.h = size
        self.pos = 0
        self.shots = []

    def move(self, x, y):
        # QEMU's absolute axes run 0..0x7fff over the framebuffer.
        self.qmp.cmd('input-send-event', events=[
            {'type': 'abs', 'data': {'axis': 'x', 'value': int(x * 0x7fff / max(self.w - 1, 1))}},
            {'type': 'abs', 'data': {'axis': 'y', 'value': int(y * 0x7fff / max(self.h - 1, 1))}}])
        time.sleep(0.15)

    def click(self, x, y, button='left', hold=0.12):
        self.move(x, y)
        self.move(x, y)
        self.qmp.button(button, True)
        time.sleep(hold)
        self.qmp.button(button, False)
        time.sleep(0.15)

    def drag(self, x0, y0, x1, y1, steps=12):
        self.move(x0, y0)
        self.qmp.button('left', True)
        time.sleep(0.2)
        for i in range(1, steps + 1):
            self.move(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps)
        time.sleep(0.2)
        self.qmp.button('left', False)

    def type(self, text):
        for c in text:
            if c in SHIFTED:
                self.qmp.keys('shift', SHIFTED[c])
            elif c.isupper():
                self.qmp.keys('shift', c.lower())
            else:
                self.qmp.keys(QCODES.get(c, c))
            time.sleep(0.12)

    def shot(self, name):
        path = self.out / 'shots' / (name + '.png')
        if self.qmp.shot(path):
            self.shots.append(str(path))
            return True
        return False

    def pixels(self):
        """The framebuffer now: (width, height, RGB bytes), through a PPM screendump."""
        path = self.out / 'shots' / '.probe.ppm'
        r = self.qmp.cmd('screendump', filename=str(path))
        if 'return' not in r or not path.exists():
            return None
        data = path.read_bytes()
        path.unlink()
        fields, pos = [], 0
        while len(fields) < 4:                              # P6, width, height, maxval
            end = pos
            while data[end:end + 1] not in (b' ', b'\n', b'\t', b'\r'):
                end += 1
            if end > pos:
                fields.append(data[pos:end])
            pos = end + 1
        return int(fields[1]), int(fields[2]), data[pos:]

    def locate(self, rgb, tolerance=10):
        """Bounding box (x0, y0, x1, y1, inclusive) of the pixels within `tolerance` of a colour.
        Rows and columns that hold only a few such pixels (under a twentieth of the fullest, and
        at most 2) are left out: the edge pixels of other text can fall within the tolerance (the
        amber-phosphor theme draws window titles in an amber whose anti-aliased edges match
        macOS's orange operator keys, and the box took in the Calculator's title bar)."""
        got = self.pixels()
        if got is None:
            return None
        w, h, px = got
        rows, cols, hits = {}, {}, []
        for y in range(h):
            row = px[y * w * 3:(y + 1) * w * 3]
            for x in range(w):
                if abs(row[x * 3] - rgb[0]) <= tolerance and abs(row[x * 3 + 1] - rgb[1]) <= tolerance and abs(row[x * 3 + 2] - rgb[2]) <= tolerance:
                    rows[y] = rows.get(y, 0) + 1
                    cols[x] = cols.get(x, 0) + 1
                    hits.append((x, y))
        if not hits:
            return None
        keep_rows = {y for y, n in rows.items() if n > 2 or n * 20 >= max(rows.values())}
        keep_cols = {x for x, n in cols.items() if n > 2 or n * 20 >= max(cols.values())}
        kept = [(x, y) for x, y in hits if y in keep_rows and x in keep_cols]
        if not kept:
            return None
        xs, ys = [x for x, _ in kept], [y for _, y in kept]
        return (min(xs), min(ys), max(xs), max(ys))

    def text_rows(self, box, dark=110):
        """Heights in pixels of the runs of rows that hold dark (text) pixels strictly inside a
        box, top to bottom: the first is the first line of text's ink height."""
        got = self.pixels()
        if got is None:
            return []
        w, h, px = got
        x0, y0, x1, y1 = box
        inset = max(3, (x1 - x0) // 60)
        runs, run = [], 0
        for y in range(y0 + inset, y1 - inset + 1):
            row = px[y * w * 3:(y + 1) * w * 3]
            ink = any(row[x * 3] < dark and row[x * 3 + 1] < dark and row[x * 3 + 2] < dark for x in range(x0 + inset, x1 - inset + 1))
            if ink:
                run += 1
            elif run:
                runs.append(run)
                run = 0
        if run:
            runs.append(run)
        return runs

    def wait(self, pattern, timeout):
        # In slices, so that a session that has ended is noticed instead of waited out.
        deadline = time.time() + timeout
        while True:
            m = self.ser.wait(pattern.encode(), min(deadline, time.time() + 2), self.proc, self.pos)
            if m:
                self.pos = m.end()
                return m
            if time.time() >= deadline or self.proc.poll() is not None or b'QDDESK_END' in self.ser.text():
                return None


def numbers(match):
    """The groups of a match, as numbers where they are numbers."""
    groups = []
    for g in match.groups():
        text = (g or b'').decode(errors='replace')
        try:
            groups.append(float(text) if '.' in text else int(text))
        except ValueError:
            groups.append(text)
    return groups


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--image', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--script', type=Path, required=True)
    p.add_argument('--resolution', default='1024x768')
    p.add_argument('--timeout', type=int, default=900)
    p.add_argument('--qemu', default='qemu-system-aarch64')
    p.add_argument('--cpu', default='neoverse-n2')
    p.add_argument('--smp', type=int, default=2)
    p.add_argument('--memory', type=int, default=2048)
    p.add_argument('--qemu-arg', action='append', default=[])
    a = p.parse_args()
    a.window, a.pointer = False, 'tablet'
    out = a.out.resolve()
    (out / 'shots').mkdir(parents=True, exist_ok=True)
    for stale in ('serial.sock', 'qmp.sock', 'serial.log', 'result.json'):
        (out / stale).unlink(missing_ok=True)
    steps = json.loads(a.script.read_text())
    size = tuple(int(v) for v in a.resolution.split('x'))
    t0 = time.time()
    end_by = t0 + a.timeout
    env = {k: v for k, v in os.environ.items() if k not in ('DISPLAY', 'WAYLAND_DISPLAY')}
    proc = subprocess.Popen(qd_run.qemu_command(a, out), stdin=subprocess.DEVNULL, stdout=open(out / 'qemu.log', 'wb'),
                            stderr=subprocess.STDOUT, env=env, cwd=out)
    result = dict(status='ERROR', times={}, steps=[], checks={}, measured={}, host_load=os.getloadavg()[0])
    places = {}
    saved = {}

    def evaluate(expression, **more):
        scope = dict(saved, W=size[0], H=size[1], abs=abs, min=min, max=max, len=len,
                     S=next(reversed(places.values()))['per_point'] if places else 1.0)
        scope.update(more)
        # n('REGEX'): how often a pattern has appeared on the console so far
        scope['n'] = lambda pattern: len(re.findall(pattern.encode(), drv.ser.text()))
        return eval(expression, {'__builtins__': {}}, scope)

    drv = None
    try:
        qmp = qd_run.Qmp(out / 'qmp.sock', t0 + 20)
        ser = qd_run.Serial(out / 'serial.sock', out / 'serial.log', t0 + 20)
        drv = Driver(qmp, ser, proc, out, size)
        # QEMU writes the console to this socket as well as to the log file; if nobody reads
        # it while a step sleeps, QEMU's main loop blocks on the write, the guest's console
        # stalls and QMP stops answering. One thread reads it for the whole run.
        def drain():
            while proc.poll() is None:
                ser.drain()
                time.sleep(0.05)
        threading.Thread(target=drain, daemon=True).start()
        if not ser.wait(rb'QDDESK_BEGIN', end_by, proc):
            raise RuntimeError('the session did not start')
        result['times']['session'] = round(time.time() - t0, 1)
        while not ser.wait(rb'QDDESK_INPUT', time.time() + 0.5, proc) and time.time() < end_by and proc.poll() is None:
            qmp.wake_input('tablet')
        ok = True
        for i, step in enumerate(steps):
            rec = dict(step=step, at=round(time.time() - t0, 1))
            if not ok or proc.poll() is not None or time.time() > end_by:
                rec['result'] = 'not run'
                result['steps'].append(rec)
                continue
            if 'wait' in step or 'expect' in step:
                pattern = step.get('wait') or step['expect']
                m = drv.wait(pattern, step.get('timeout', 60))
                rec['result'] = 'ok' if m else 'TIMEOUT'
                if m:
                    rec['line'] = m.group(0).decode(errors='replace')[:200]
                good = bool(m)
                if m and 'save' in step:
                    saved[step['save']] = numbers(m)
                    rec['saved'] = saved[step['save']]
                if m and 'assert' in step:
                    try:
                        good = bool(evaluate(step['assert'], g=numbers(m)))
                    except Exception as e:                    # noqa: BLE001 - a broken expression is a failed check
                        good = False
                        rec['assert_error'] = str(e)
                    rec['groups'] = numbers(m)
                    if not good:
                        rec['result'] = 'ASSERT FAILED'
                if 'expect' in step:
                    result['checks'][step.get('name', pattern)] = good
                ok = good or step.get('optional', False)
            elif 'absent' in step:
                found = re.search(step['absent'].encode(), ser.text())
                rec['result'] = 'ok' if not found else 'PRESENT'
                result['checks'][step.get('name', 'absent ' + step['absent'])] = not found
            elif 'count' in step:
                seen = len(re.findall(step['count'].encode(), ser.text()))
                rec.update(seen=seen, result='ok')
                result['checks'][step.get('name', 'count ' + step['count'])] = seen == step['equals']
                result['measured'][step.get('name', 'count ' + step['count'])] = dict(seen=seen, wanted=step['equals'])
            elif 'check' in step:
                try:
                    good = bool(evaluate(step['check']))
                except Exception as e:                        # noqa: BLE001
                    good = False
                    rec['error'] = str(e)
                rec['result'] = 'ok'
                rec['holds'] = good
                result['checks'][step.get('name', step['check'])] = good
            elif 'all' in step:
                bad, total = [], 0
                for m in re.finditer(step['all'].encode(), ser.text()):
                    total += 1
                    try:
                        if not evaluate(step['assert'], g=numbers(m)):
                            bad.append(numbers(m))
                    except Exception as e:                    # noqa: BLE001
                        bad.append(str(e))
                rec.update(matches=total, failing=bad[:8], result='ok')
                result['checks'][step.get('name', 'all ' + step['all'])] = total > 0 and not bad
                result['measured'][step.get('name', 'all ' + step['all'])] = dict(matches=total, failing=bad[:8])
            elif 'click_px' in step or 'move_px' in step:
                try:
                    x, y = (evaluate(e) for e in (step.get('click_px') or step['move_px']))
                except Exception as e:                        # noqa: BLE001
                    rec['result'] = 'bad expression: %s' % e
                    ok = False
                else:
                    rec['at_pixel'] = [round(x), round(y)]
                    if 'click_px' in step:
                        drv.click(x, y, button=step.get('button', 'left'))
                    else:
                        drv.move(x, y)
                        drv.move(x, y)
                    rec['result'] = 'ok'
            elif 'sleep' in step:
                time.sleep(step['sleep'])
                rec['result'] = 'ok'
            elif 'shot' in step:
                rec['result'] = 'ok' if drv.shot(step['shot']) else 'no screenshot'
            elif 'locate' in step:
                # {"locate": "view", "rgb": [r, g, b], "points_wide": 380[, "scale": 2.0]}: finds a
                # coloured rectangle the application draws, remembers where it is and how many
                # pixels a point takes there; with "scale" the measured value must match it.
                box = None
                for _ in range(int(step.get('tries', 20))):
                    box = drv.locate(step['rgb'], step.get('tolerance', 10))
                    if box and box[2] - box[0] > 8:
                        break
                    time.sleep(1.5)
                if box:
                    per_point = (box[2] - box[0] + 1) / step.get('points_wide', box[2] - box[0] + 1)
                    places[step['locate']] = dict(box=box, per_point=per_point)
                    rec.update(box=box, pixels_per_point=round(per_point, 4), text_rows=drv.text_rows(box)[:4])
                    rec['result'] = 'ok'
                    if 'scale' in step:
                        good = abs(per_point - step['scale']) <= step.get('scale_tolerance', 0.02)
                        result['checks'][step.get('name', 'scale of ' + step['locate'])] = good
                        result['measured'][step.get('name', 'scale of ' + step['locate'])] = dict(
                            pixels_per_point=round(per_point, 4), box=box, text_rows=rec['text_rows'])
                else:
                    rec['result'] = 'NOT FOUND'
                    ok = step.get('optional', False)
            elif 'pixel_in' in step:
                # {"pixel_in": "ops", "points": [dx, dy], "rgb": [r, g, b], "name": "..."}: the colour of one
                # pixel, placed like click_in; a check
                place, got = places.get(step['pixel_in']), drv.pixels()
                if place is None or got is None:
                    rec['result'] = 'not located'
                    ok = False
                else:
                    w, h, px = got
                    x = min(max(round(place['box'][0] + step['points'][0] * place['per_point']), 0), w - 1)
                    y = min(max(round(place['box'][1] + step['points'][1] * place['per_point']), 0), h - 1)
                    seen = list(px[(y * w + x) * 3:(y * w + x) * 3 + 3])
                    good = all(abs(a - b) <= step.get('tolerance', 12) for a, b in zip(seen, step['rgb']))
                    rec.update(at_pixel=[x, y], seen=seen, result='ok')
                    result['checks'][step.get('name', 'pixel in ' + step['pixel_in'])] = good
                    result['measured'][step.get('name', 'pixel in ' + step['pixel_in'])] = dict(at=[x, y], seen=seen, wanted=step['rgb'])
            elif 'watch_scale' in step:
                # {"watch_scale": "view", "rgb": [...], "points_wide": 378, "seconds": 90, "until": "REGEX",
                #  "scales": [1, 1.25, ...]}: measures pixels per point of the rectangle again and again
                # while something else changes the display scale; every expected scale must be seen.
                seen, end_at = [], time.time() + step.get('seconds', 60)
                until = re.compile(step['until'].encode()) if 'until' in step else None
                while time.time() < end_at and proc.poll() is None:
                    box = drv.locate(step['rgb'], step.get('tolerance', 10))
                    if box and box[2] - box[0] > 8:
                        per_point = round((box[2] - box[0] + 1) / step['points_wide'], 3)
                        if not seen or seen[-1][1] != per_point:
                            seen.append((round(time.time() - t0, 1), per_point, drv.text_rows(box)[:2]))
                            drv.shot('%s-%s' % (step.get('shots', 'scale'), str(per_point).replace('.', '_')))
                        places[step['watch_scale']] = dict(box=box, per_point=per_point)
                    if until is not None and until.search(ser.text()):
                        end_at = min(end_at, time.time() + step.get('after', 8))
                        until = None
                tolerance = step.get('scale_tolerance', 0.02)
                missing = [e for e in step.get('scales', []) if not any(abs(v - e) <= tolerance for _, v, _ in seen)]
                rec.update(seen=seen, missing=missing)
                rec['result'] = 'ok'
                name = step.get('name', 'scales seen live')
                result['checks'][name] = not missing
                result['measured'][name] = dict(seen=seen, missing=missing)
            elif 'click_in' in step:
                # {"click_in": "view", "points": [dx, dy]}: dx right of and dy below the located
                # rectangle's top left corner, in points (negative dy is above it)
                place = places.get(step['click_in'])
                if place is None:
                    rec['result'] = 'not located'
                    ok = False
                else:
                    x = place['box'][0] + step['points'][0] * place['per_point']
                    y = place['box'][1] + step['points'][1] * place['per_point']
                    rec['at_pixel'] = [round(x), round(y)]
                    # "hold": keys kept down around the click; "count": 2 for a double click
                    # "until": a console line the click must produce; the click is repeated ("tries",
                    # each waiting "each" seconds) because a double click's timing does not always
                    # survive emulation. The attempts used are recorded.
                    for attempt in range(1, int(step.get('tries', 1)) + 1):
                        for key in step.get('hold', []):
                            qmp.cmd('input-send-event', events=[{'type': 'key', 'data': {'down': True, 'key': {'type': 'qcode', 'data': key}}}])
                        time.sleep(0.2 if step.get('hold') else 0)
                        for n in range(step.get('count', 1)):
                            drv.click(x, y, button=step.get('button', 'left'), hold=0.05 if step.get('count', 1) > 1 else 0.12)
                        for key in reversed(step.get('hold', [])):
                            qmp.cmd('input-send-event', events=[{'type': 'key', 'data': {'down': False, 'key': {'type': 'qcode', 'data': key}}}])
                        rec['attempts'] = attempt
                        hit = 'until' not in step or bool(drv.wait(step['until'], step.get('each', 30)))
                        if hit:
                            break
                    if 'until' in step and 'name' in step:
                        result['checks'][step['name']] = hit
                        result['measured'][step['name']] = dict(attempts=rec['attempts'])
                    rec['result'] = 'ok'
            elif 'move_in' in step or 'drag_in' in step:
                # {"move_in": "view", "points": [dx, dy]}: the pointer placed like click_in, no button.
                # {"drag_in": "view", "from": [dx, dy], "to": [dx, dy], "button": "left", "hold": 0.8}: the
                # button goes down at `from`, is held, the pointer goes to `to` in steps, the button comes up
                place = places.get(step.get('move_in') or step['drag_in'])
                if place is None:
                    rec['result'] = 'not located'
                    ok = False
                else:
                    at = lambda pt: (place['box'][0] + pt[0] * place['per_point'], place['box'][1] + pt[1] * place['per_point'])
                    if 'move_in' in step:
                        drv.move(*at(step['points']))
                        drv.move(*at(step['points']))
                    else:
                        (x0, y0), (x1, y1) = at(step['from']), at(step['to'])
                        drv.move(x0, y0)
                        drv.move(x0, y0)
                        qmp.button(step.get('button', 'left'), True)
                        time.sleep(step.get('hold', 0.8))
                        for i in range(1, 9):
                            drv.move(x0 + (x1 - x0) * i / 8, y0 + (y1 - y0) * i / 8)
                        time.sleep(step.get('settle', 1.0))
                        qmp.button(step.get('button', 'left'), False)
                        time.sleep(0.15)
                    rec['result'] = 'ok'
            elif 'move' in step:
                drv.move(*step['move'])
                rec['result'] = 'ok'
            elif 'click' in step:
                drv.click(*step['click'], button=step.get('button', 'left'))
                rec['result'] = 'ok'
            elif 'drag' in step:
                drv.drag(*step['drag'])
                rec['result'] = 'ok'
            elif 'key' in step:
                qmp.keys(*step['key'])
                rec['result'] = 'ok'
            elif 'type' in step:
                drv.type(step['type'])
                rec['result'] = 'ok'
            else:
                rec['result'] = 'unknown step'
                ok = False
            rec['done'] = round(time.time() - t0, 1)
            print('cocoa-run: %-60s %s' % (json.dumps(step)[:60], rec['result']), flush=True)
            result['steps'].append(rec)
        result['times']['end'] = round(time.time() - t0, 1)
        done = all(s['result'] == 'ok' or s['step'].get('optional') for s in result['steps'])
        result['status'] = 'PASS' if done and all(result['checks'].values()) else 'FAIL'
    except Exception as e:                                    # noqa: BLE001 - reported, then exit 2
        result['error'] = str(e)
    finally:
        try:
            if proc.poll() is None:
                qmp.cmd('quit')
        except Exception:                                     # noqa: BLE001
            pass
        try:
            proc.wait(10)
        except subprocess.TimeoutExpired:
            proc.kill()
        for s in ('serial.sock', 'qmp.sock'):
            (out / s).unlink(missing_ok=True)
    result['shots'] = drv.shots if drv else []
    (out / 'result.json').write_text(json.dumps(result, indent=1) + '\n')
    print('cocoa-run: %s  (%s)' % (result['status'], out))
    for name, passed in result['checks'].items():
        print('cocoa-run:   %-40s %s' % (name, 'ok' if passed else 'FAILED'))
    if 'error' in result:
        print('cocoa-run: ' + result['error'], file=sys.stderr)
        return 2
    return 0 if result['status'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
