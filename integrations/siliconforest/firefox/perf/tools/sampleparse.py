#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Parse sf-sample (sample(1)-format) call graphs: per thread, samples not in a wait, top self and inclusive
functions over the busy samples.  sampleparse.py <sample.txt> [nthreads] [nframes]"""
import collections, re, sys

WAIT = re.compile(r'mach_msg2_trap|__psynch_cvwait|__psynch_mutexwait|kevent|__select|__semwait|\bread\b|__workq_kernreturn|'
                  r'__ulock_wait|poll\b|__wait4|__sigsuspend|nanosleep|mach_wait_until|__recvfrom|__pthread_kill|'
                  r'thread_suspend|__read_nocancel|__accept|semaphore_wait_trap|semaphore_timedwait_trap|__psynch_rw')
line_re = re.compile(r'^(\s*[+!:| ]*)(\d+) (.*)$')


def parse(path):
    threads = []
    cur = None
    stack = []
    for raw in open(path, errors='replace'):
        if raw.startswith('Total number in stack') or raw.startswith('Sort by top of stack'):
            break
        m = line_re.match(raw.rstrip('\n'))
        if not m:
            continue
        indent = len(m.group(1)); n = int(m.group(2)); text = m.group(3).strip()
        if indent <= 4 and text.startswith('Thread_'):
            cur = dict(name=text, nodes=[]); threads.append(cur); stack = [(indent, None)]
            continue
        if cur is None:
            continue
        fn = re.sub(r'\s+\(in ([^)]+)\)\s+\+\s+\d+\s+\[0x[0-9a-f]+\].*$', r' (\1)', text)
        fn = re.sub(r'\s+\[0x[0-9a-f]+\].*$', '', fn)
        while stack and stack[-1][0] >= indent:
            stack.pop()
        parent = stack[-1][1] if stack else None
        node = dict(fn=fn, n=n, parent=parent, children=0)
        if parent is not None:
            parent['children'] += n
        cur['nodes'].append(node); stack.append((indent, node))
    return threads


def path_of(node):
    out = []
    while node is not None:
        out.append(node['fn']); node = node['parent']
    return out  # leaf first


def main():
    path = sys.argv[1]; nt = int(sys.argv[2]) if len(sys.argv) > 2 else 8; nf = int(sys.argv[3]) if len(sys.argv) > 3 else 12
    res = []
    for t in parse(path):
        total = sum(nd['n'] for nd in t['nodes'] if nd['parent'] is None)
        busy = 0; selfc = collections.Counter(); incl = collections.Counter()
        for nd in t['nodes']:
            s = nd['n'] - nd['children']
            if s <= 0:
                continue
            p = path_of(nd)
            if WAIT.search(p[0]):
                continue
            busy += s; selfc[p[0]] += s
            for f in set(p):
                incl[f] += s
        res.append((busy, total, t['name'], selfc, incl))
    res.sort(key=lambda r: -r[0])
    for busy, total, name, selfc, incl in res[:nt]:
        print('\n== %s busy=%d/%d samples' % (name, busy, total))
        for f, c in selfc.most_common(nf):
            print('  self %5d  %s' % (c, f[:140]))
        for f, c in incl.most_common(nf * 2):
            print('  incl %5d  %s' % (c, f[:140]))


if __name__ == '__main__':
    main()
