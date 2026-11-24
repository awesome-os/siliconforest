#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Where an application's Objective-C methods are, and what a method calls.

  sf_methods.py <binary> [--arch arm64e] list [regex]      address  -[Class selector]
  sf_methods.py <binary> [--arch arm64e] calls <regex>     for each method matching: the calls it
                                                           makes, in order (imported functions,
                                                           selectors through objc_msgSend stubs,
                                                           own methods), from llvm-objdump
  sf_methods.py <binary> [--arch arm64e] dis <regex>       the annotated disassembly of those methods

Reads only; prints names and addresses. The output of `dis` is a disassembly of the application
and is not to be committed (docs/roadmap/issues/SF-Y-terminal-app.md, "Hard limits")."""
import os, re, struct, subprocess, sys, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'homebrew'))
import hb_macho


def method_table(m):
    """{imp address: '-[Class sel]'} of every class and category of the image."""
    out = {}

    def methods(list_va, fmt):
        o = m.off_of(list_va) if list_va else None
        if o is None:
            return
        ent, count = struct.unpack_from('<II', m.data, o)
        size = ent & 0xffff
        if count > 100000 or size not in (12, 24):
            return
        for i in range(count):
            at = o + 8 + i * size
            if ent & 0x80000000:
                rel, _t, irel = struct.unpack_from('<iii', m.data, at)
                if ent & 0x40000000:
                    name = hb_macho.cstr(m.data, at + rel)
                else:
                    so = m.off_of(m.pointer(at + rel))
                    name = hb_macho.cstr(m.data, so) if so is not None else '?'
                imp_off = at + 8 + irel
                imp = next((va + imp_off - fo for _n, va, _vs, fo, fs in m.segments if fo <= imp_off < fo + fs), None)
            else:
                so = m.off_of(m.pointer(at))
                name = hb_macho.cstr(m.data, so) if so is not None else '?'
                imp = m.pointer(at + 16)
            if imp:
                out[imp] = fmt % name

    def klass(va, sign):
        o = m.off_of(va)
        if o is None:
            return None
        ro = m.off_of(m.pointer(o + 32) & ~7)
        if ro is None:
            return None
        no = m.off_of(m.pointer(ro + 24))
        name = hb_macho.cstr(m.data, no) if no is not None else '?'
        methods(m.pointer(ro + 32), sign + '[' + name + ' %s]')
        return name

    s = m._section('__objc_classlist')
    if s:
        for off in range(s[2], s[2] + s[1], 8):
            va = m.pointer(off)
            if klass(va, '-') is not None:
                co = m.off_of(va)
                klass(m.pointer(co), '+')
    s = m._section('__objc_catlist')
    if s:
        for off in range(s[2], s[2] + s[1], 8):
            o = m.off_of(m.pointer(off))
            if o is None:
                continue
            no = m.off_of(m.pointer(o))
            cat = hb_macho.cstr(m.data, no) if no is not None else '?'
            methods(m.pointer(o + 16), '-[(' + cat + ') %s]')
            methods(m.pointer(o + 24), '+[(' + cat + ') %s]')
    return out


def stub_table(m):
    """{stub address: name}: import stubs (through the bound pointer they load) and
    objc_msgSend selector stubs (through the selector reference they load)."""
    out = {}
    imports = m.chained_imports()
    text = next((va for n, va, _vs, _fo, _fs in m.segments if n == '__TEXT'), 0)

    def adrp_target(pc, insn):
        imm = ((insn >> 5) & 0x7ffff) << 2 | (insn >> 29) & 3
        if imm & (1 << 20):
            imm -= 1 << 21
        return (pc & ~0xfff) + (imm << 12)

    def slot(pc, off):
        a, b = struct.unpack_from('<II', m.data, off)
        if a & 0x9f000000 != 0x90000000:
            return None
        page = adrp_target(pc, a)
        if b & 0xffc00000 == 0xf9400000:            # ldr xN, [xM, #imm]
            return page + ((b >> 10) & 0xfff) * 8
        if b & 0xff800000 == 0x91000000:            # add xN, xM, #imm
            return page + ((b >> 10) & 0xfff)
        return None

    for (seg, sect), (addr, size, off) in m.sections.items():
        if sect in ('__stubs', '__auth_stubs'):
            step = 16 if sect == '__auth_stubs' else 12
            for i in range(0, size, step):
                va = slot(addr + i, off + i)
                o = m.off_of(va) if va else None
                if o is None:
                    continue
                raw = struct.unpack_from('<Q', m.data, o)[0]
                if raw >> 62 & 1:
                    n = raw & 0xffffff
                    if n < len(imports):
                        out[addr + i] = imports[n][0]
        elif sect == '__objc_stubs':
            for i in range(0, size, 32):
                va = slot(addr + i, off + i)
                o = m.off_of(va) if va else None
                if o is None:
                    continue
                so = m.off_of(m.pointer(o))
                if so is not None:
                    out[addr + i] = '[' + hb_macho.cstr(m.data, so) + ']'
    return out


def main():
    a = sys.argv[1:]
    arch = None
    if '--arch' in a:
        i = a.index('--arch'); arch = a[i + 1]; del a[i:i + 2]
    path, mode = a[0], a[1]
    rx = re.compile(a[2]) if len(a) > 2 else None
    m = hb_macho.load(path, arch)
    meths = method_table(m)
    if mode == 'list':
        for imp in sorted(meths):
            if rx is None or rx.search(meths[imp]):
                print('%x  %s' % (imp, meths[imp]))
        return
    stubs = stub_table(m)
    with tempfile.NamedTemporaryFile(suffix='.thin') as t:
        t.write(m.data); t.flush()
        dis = subprocess.run(['llvm-objdump', '--macho', '-d', '--no-show-raw-insn', t.name], capture_output=True, text=True).stdout
    show = False
    line_rx = re.compile(r'^([0-9a-f]+):\t(\S+)\t?(.*)$')
    for line in dis.splitlines():
        g = line_rx.match(line)
        if not g:
            continue
        pc, op, args = int(g.group(1), 16), g.group(2), g.group(3)
        if pc in meths:
            show = rx.search(meths[pc]) is not None
            if show:
                print('\n== %x %s' % (pc, meths[pc]))
        elif op in ('pacibsp',) and show and mode == 'calls':
            show = False                               # the next function, not a method
        if not show:
            continue
        note = ''
        if op in ('bl', 'b'):
            try:
                target = int(args.split()[0], 16)
            except ValueError:
                target = None
            if target in stubs:
                note = stubs[target]
            elif target in meths:
                note = meths[target]
            elif op == 'bl':
                note = 'sub_%x' % target if target else ''
        if mode == 'dis':
            print('%x:\t%s\t%s%s' % (pc, op, args, ('\t; ' + note) if note else ''))
        elif note:
            print('  %x %s' % (pc, note))


if __name__ == '__main__':
    main()
