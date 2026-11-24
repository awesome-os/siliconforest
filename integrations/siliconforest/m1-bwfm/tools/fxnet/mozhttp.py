#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# FXNET: per-request timeline from a MOZ_LOG with nsHttp:3: for each channel (this=) its uri, Init time, first
# response-headers time and stop time, relative to a start uri. argv: log start_uri_substring [end_s]
import re, sys, datetime
log, start_pat = sys.argv[1], sys.argv[2]
end_s = float(sys.argv[3]) if len(sys.argv) > 3 else 30
ts_re = re.compile(r'^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+) UTC - \[[^\]]*\]: (.*)$')
def ms(s):
    return datetime.datetime.strptime(s[:26], '%Y-%m-%d %H:%M:%S.%f').timestamp() * 1000
ch = {}
last_init = None
t0 = None
for line in open(log, errors='replace'):
    m = ts_re.match(line)
    if not m:
        continue
    t, msg = ms(m.group(1)), m.group(2)
    if 'nsHttpChannel::Init [this=' in msg:
        last_init = re.search(r'this=([0-9a-f]+)', msg).group(1)
        ch[last_init] = {'init': t}
        continue
    if msg.startswith('E/nsHttp uri=') and last_init and 'uri' not in ch[last_init]:
        ch[last_init]['uri'] = msg[len('E/nsHttp uri='):].strip()
        if t0 is None and start_pat in ch[last_init]['uri']:
            t0 = ch[last_init]['init']
        continue
    m2 = re.search(r'nsHttpChannel::OnStartRequest \[this=([0-9a-f]+)', msg)
    if m2 and m2.group(1) in ch:
        ch[m2.group(1)].setdefault('start', t)
    m3 = re.search(r'nsHttpChannel::OnStopRequest \[this=([0-9a-f]+)', msg)
    if m3 and m3.group(1) in ch:
        ch[m3.group(1)].setdefault('stop', t)
rows = []
for k, c in ch.items():
    if t0 is None or 'uri' not in c or c['init'] < t0 or c['init'] - t0 > end_s * 1000:
        continue
    rows.append((c['init'] - t0, (c.get('start', 0) - c['init']) if 'start' in c else -1,
                 (c.get('stop', 0) - t0) if 'stop' in c else -1, c['uri']))
rows.sort()
for r in rows:
    print('init %8.0f  ttfb %7.0f  stop %8.0f  %s' % (r[0], r[1], r[2], r[3][:110]))
