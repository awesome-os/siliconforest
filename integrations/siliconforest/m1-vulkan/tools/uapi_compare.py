#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare two asahi_drm.h headers by ABI, not by text.

For each header: every `struct drm_asahi_*` is laid out by clang (-fdump-record-layouts-simple:
size, alignment and the offset of every field, nested and anonymous members included), every
`DRM_IOCTL_ASAHI_*` request number and every `DRM_ASAHI_*` integer constant (enum members and
#defines) is evaluated by a compiled C program. The two results are compared and printed as a
Markdown table of differences.

  uapi_compare.py <kernel asahi_drm.h> <mesa asahi_drm.h> [--names KERNEL MESA]

Each header is compiled with the drm.h next to it. Host-only: nothing is run on a device.
The layouts are those of the host (x86-64 LP64); the structs use fixed-width types only, so
they equal arm64's (checked: no `long`, pointer or bitfield in either header).
"""
import os, re, subprocess, sys, tempfile


def names(text):
    structs = sorted(set(re.findall(r'\bstruct\s+(drm_asahi_\w+)\s*\{', text)))
    ioctls = sorted(set(re.findall(r'#define\s+(DRM_IOCTL_ASAHI_\w+)\s', text)) |
                    set(re.findall(r'^\s*(DRM_IOCTL_ASAHI_\w+)\s*=', text, re.M)))
    consts = set(re.findall(r'^\s*(DRM_ASAHI_\w+)\s*(?:=|,|$)', text, re.M))
    consts |= {n for n in re.findall(r'#define\s+(DRM_ASAHI_\w+)\s+\S', text)
               if not re.search(r'#define\s+' + n + r'\(', text)}
    return structs, ioctls, sorted(consts)


def probe(header):
    text = open(header).read()
    for bad in (r'\blong\b', r'\*\s*\w+\s*;', r':\s*\d+\s*;'):
        if re.search(bad, re.sub(r'/\*.*?\*/', '', text, flags=re.S)):
            sys.exit(f'{header}: has {bad}: host layout may differ from arm64')
    structs, ioctls, consts = names(text)
    d = os.path.dirname(os.path.abspath(header))
    with tempfile.TemporaryDirectory() as t:
        if not os.path.exists(os.path.join(d, 'drm.h')):
            # a sparse checkout (sources/gpu-plan/linux-aurora): drm.h of the same commit
            os.mkdir(os.path.join(t, 'inc'))
            for h in ('drm.h', 'drm_mode.h'):
                body = subprocess.run(['git', '-C', d, 'show', 'HEAD:./' + h], check=True,
                                      capture_output=True, text=True).stdout
                open(os.path.join(t, 'inc', h), 'w').write(body)
            d = os.path.join(t, 'inc')
        c = os.path.join(t, 'p.c')
        with open(c, 'w') as f:
            f.write('#include <stdio.h>\n#include "%s"\n' % os.path.abspath(header))
            for s in structs:
                f.write('struct %s v_%s;\n' % (s, s))
            f.write('int main(void){\n')
            for i in ioctls:
                f.write('printf("ioctl %s 0x%%08lx\\n",(unsigned long)(%s));\n' % (i, i))
            for k in consts:
                f.write('printf("const %s %%lld\\n",(long long)(%s));\n' % (k, k))
            f.write('return 0;}\n')
        exe = os.path.join(t, 'p')
        subprocess.run(['clang', '-w', '-D__user=', '-I', d, '-o', exe, c], check=True)
        values = subprocess.run([exe], check=True, capture_output=True, text=True).stdout
        dump = subprocess.run(['clang', '-w', '-D__user=', '-I', d, '-c', '-o', os.devnull, '-Xclang',
                               '-fdump-record-layouts-simple', c],
                              check=True, capture_output=True, text=True).stdout
    layouts = {}
    for block in dump.split('*** Dumping AST Record Layout'):
        m = re.search(r'Type: struct (drm_asahi_\w+)\n', block)
        if not m:
            continue
        size = re.search(r'Size:(\d+)', block).group(1)
        fields = re.findall(r'FieldOffsets: \[([^\]]*)\]', block)
        layouts[m.group(1)] = (int(size) // 8, fields[0] if fields else '')
    out = {}
    for line in values.splitlines():
        kind, n, v = line.split()
        out[(kind, n)] = v
    for s, (size, offs) in layouts.items():
        out[('sizeof', s)] = str(size)
        out[('offsets', s)] = offs
    return out


def main():
    a = sys.argv[1:]
    labels = ['kernel', 'mesa']
    if '--names' in a:
        i = a.index('--names'); labels = a[i + 1:i + 3]; del a[i:i + 3]
    k, m = probe(a[0]), probe(a[1])
    keys = sorted(set(k) | set(m))
    same = sum(1 for x in keys if k.get(x) == m.get(x))
    print(f'| item | {labels[0]} | {labels[1]} |\n|---|---|---|')
    for x in keys:
        if k.get(x) != m.get(x):
            kv, mv = k.get(x, '(absent)'), m.get(x, '(absent)')
            if x[0] == 'offsets':
                kv = mv = 'differ' if x in k and x in m else (kv if x in k else '(absent)')
                if x in k and x in m:
                    kv = f'{len(k[x].split(","))} fields'; mv = f'{len(m[x].split(","))} fields'
                elif x not in k:
                    mv = 'present'
                else:
                    kv = 'present'
            print(f'| {x[0]} `{x[1]}` | {kv} | {mv} |')
    n = lambda d, kind: sum(1 for x in d if x[0] == kind)
    print(f'\n{same} of {len(keys)} items equal. '
          f'{labels[0]}: {n(k, "sizeof")} structs, {n(k, "ioctl")} ioctls, {n(k, "const")} constants; '
          f'{labels[1]}: {n(m, "sizeof")} structs, {n(m, "ioctl")} ioctls, {n(m, "const")} constants.')


if __name__ == '__main__':
    main()
