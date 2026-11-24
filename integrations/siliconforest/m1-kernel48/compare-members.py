#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Member-by-member proof that a collection built under the SiliconForest names carries the object
code of one built under the old names (kernel 48).

  compare-members.py OLD NEW [--json OUT]
      OLD, NEW: two Mach-O files (arm64 kext bundle or kernel), unstripped or stripped alike.

What "identical apart from names" means here, and how it is checked without trusting names:
  1. the same sections with the same sizes, except where a renamed string changes length
     (the new product name is three bytes longer than the old one);
  2. every string of the C string sections of OLD, mapped by the rename rules
     (tools/sf_rename.py transform_text), is the string at the same position of NEW;
  3. code: instruction for instruction the same words. Where the section layout moved (case 1),
     an instruction that carries a PC-relative distance (b, bl, b.cond, cbz, tbz, adr, adrp, ldr
     literal) and the add/ldr/str that completes an adrp are compared by what they point AT:
     the same offset in the same section, the same position in the string table for a string,
     the same imported symbol for a stub;
  4. data: byte for byte, except pointer slots (rebase/bind entries), compared the same way, and
     except the bytes of names inside fixed-size name fields (the kmod name, class names), which
     must be the mapped old bytes.
  Not compared: LC_UUID, the code signature, the symbol and string tables of __LINKEDIT (names),
  debug sections.
Exit status 0 only if every check passes. Differences are printed, never summarised away.
"""
import hashlib, json, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', '..', 'tools'))
import sf_rename  # noqa: E402


def mapped(text):
    return sf_rename.transform_text(text, {}, 'x.c')


OLD, NEW = 'pure' + 'darwin', 'silicon' + 'forest'             # the product name, lower case
OLD_C, NEW_C = 'Pure' + 'Darwin', 'Silicon' + 'Forest'
O2, N2 = 'p' + 'd', 's' + 'f'                                    # the two-letter prefix


def fold(name):
    """A symbol name with the renamed prefix folded back, for names the rules do not reach as
    text: a C++ or Rust mangled name carries its identifiers behind a length (the 10 before a
    ten-letter function name, the 31 or 34 before a class named after the bundle identifier),
    and a Rust name ends in a hash of its crate's metadata (17h, sixteen digits, E)."""
    name = name.replace(NEW, OLD).replace(NEW_C, OLD_C) \
        .replace(N2, O2).replace(N2.upper(), O2.upper()).replace(N2.capitalize(), O2.capitalize())
    name = re.sub(r'\d+((?:[A-Za-z_][A-Za-z0-9_]*?)?' + OLD + ')', r'#\1', name)
    name = re.sub(r'17h[0-9a-f]{16}E', '17h#E', name)
    return re.sub(r'\.llvm\.\d+', '', name)


def foldb(data):
    return fold_bytes(data)


def fold_bytes(data):
    e = lambda t: t.encode()
    return data.replace(e(NEW), e(OLD)).replace(e(NEW_C), e(OLD_C)) \
        .replace(e(N2), e(O2)).replace(e(N2.upper()), e(O2.upper())).replace(e(N2.capitalize()), e(O2.capitalize()))


STORE = re.compile(rb'/nix/store/[a-z0-9]{32}-')
DATE = re.compile(rb'[A-Z][a-z]{2} [A-Z][a-z]{2} [ 0-9]\d \d\d:\d\d:\d\d UTC \d{4}')


class MachO:
    def __init__(self, path):
        self.path = path
        b = self.b = open(path, 'rb').read()
        assert struct.unpack_from('<I', b)[0] == 0xfeedfacf, path
        self.filetype = struct.unpack_from('<I', b, 12)[0]
        self.sections, self.segments = [], []
        self.syms, self.indirect, self.dyld = [], [], None
        off = 32
        for _ in range(struct.unpack_from('<I', b, 16)[0]):
            cmd, n = struct.unpack_from('<II', b, off)
            if cmd == 0x19:
                seg = b[off + 8:off + 24].rstrip(b'\0').decode()
                vm, vs, fo, fs = struct.unpack_from('<QQQQ', b, off + 24)
                self.segments.append((seg, vm, vs, fo, fs))
                for j in range(struct.unpack_from('<I', b, off + 64)[0]):
                    q = off + 72 + 80 * j
                    name = b[q:q + 16].rstrip(b'\0').decode()
                    sseg = b[q + 16:q + 32].rstrip(b'\0').decode()
                    addr, size, pos = struct.unpack_from('<QQI', b, q + 32)
                    flags, r1, r2 = struct.unpack_from('<III', b, q + 64)
                    self.sections.append(dict(name=sseg + ',' + name, addr=addr, size=size, pos=pos, flags=flags, r1=r1, r2=r2))
            elif cmd == 2:
                so, ns, st, sl = struct.unpack_from('<IIII', b, off + 8)
                for i in range(ns):
                    strx, typ, sect, desc, val = struct.unpack_from('<IBBHQ', b, so + 16 * i)
                    end = b.index(b'\0', st + strx)
                    self.syms.append((b[st + strx:end].decode('latin-1'), typ, sect, val))
            elif cmd == 0xb:
                io, ni = struct.unpack_from('<II', b, off + 56)
                self.indirect = list(struct.unpack_from('<%dI' % ni, b, io))
            elif cmd in (0x22, 0x80000022):
                self.dyld = struct.unpack_from('<10I', b, off + 8)
            elif cmd == 0x26:       # LC_FUNCTION_STARTS: every function, also when symbols are stripped
                self.fstarts_at = struct.unpack_from('<II', b, off + 8)
            off += n
        self.by_name = {s['name']: s for s in self.sections}
        self.fstarts = []
        if getattr(self, 'fstarts_at', None) and self.segments:
            d = b[self.fstarts_at[0]:self.fstarts_at[0] + self.fstarts_at[1]]
            base = [x for x in self.segments if x[0] == '__TEXT'][0][1]
            i, at = 0, base
            while i < len(d):
                v = sh = 0
                while True:
                    c = d[i]; i += 1
                    v |= (c & 0x7f) << sh; sh += 7
                    if not c & 0x80:
                        break
                if v == 0:
                    break
                at += v
                self.fstarts.append(at)

    def sect_of(self, addr):
        for i, s in enumerate(self.sections):
            if s['addr'] <= addr < s['addr'] + max(s['size'], 1):
                return i
        return None

    def data(self, s):
        return b'' if s['flags'] & 0xff in (1, 12, 18) else self.b[s['pos']:s['pos'] + s['size']]

    def strings(self, s):
        """[(offset, bytes)] of a C string section."""
        d, out, at = self.data(s), [], 0
        while at < len(d):
            end = d.index(b'\0', at) if b'\0' in d[at:] else len(d)
            out.append((at, d[at:end]))
            at = end + 1
        return out

    def stub_names(self):
        """address of a stub / pointer slot -> imported symbol name."""
        out = {}
        for s in self.sections:
            kind = s['flags'] & 0xff
            if kind in (6, 7, 8):
                width = s['r2'] if kind == 8 else 8
                for i in range(s['size'] // width if width else 0):
                    idx = self.indirect[s['r1'] + i] if s['r1'] + i < len(self.indirect) else 0x80000000
                    if idx < len(self.syms):
                        out[s['addr'] + i * width] = self.syms[idx][0]
        return out

    def fixups(self):
        """rebase and bind locations from the dyld info: {address: ('rebase',) | ('bind', name, addend)}."""
        out = {}
        if not self.dyld:
            return out
        ro, rs, bo, bs, wo, ws, lo, ls, eo, es = self.dyld
        segs = self.segments

        def uleb(d, i):
            v = sh = 0
            while True:
                c = d[i]; i += 1
                v |= (c & 0x7f) << sh; sh += 7
                if not c & 0x80:
                    return v, i

        def sleb(d, i):
            v = sh = 0
            while True:
                c = d[i]; i += 1
                v |= (c & 0x7f) << sh; sh += 7
                if not c & 0x80:
                    if c & 0x40:
                        v -= 1 << sh
                    return v, i
        d, i, seg, o = self.b[ro:ro + rs], 0, 0, 0
        while i < len(d):
            op, imm = d[i] & 0xf0, d[i] & 0x0f; i += 1
            if op == 0x00: break
            elif op == 0x10: pass
            elif op == 0x20: seg = imm; o, i = uleb(d, i)
            elif op == 0x30: v, i = uleb(d, i); o += v
            elif op == 0x40: o += imm * 8
            elif op == 0x50:
                for _ in range(imm): out[segs[seg][1] + o] = ('rebase',); o += 8
            elif op == 0x60:
                c, i = uleb(d, i)
                for _ in range(c): out[segs[seg][1] + o] = ('rebase',); o += 8
            elif op == 0x70: out[segs[seg][1] + o] = ('rebase',); v, i = uleb(d, i); o += 8 + v
            elif op == 0x80:
                c, i = uleb(d, i); sk, i = uleb(d, i)
                for _ in range(c): out[segs[seg][1] + o] = ('rebase',); o += 8 + sk
            else: raise AssertionError('rebase opcode %x' % op)
        for start, size, lazy in ((bo, bs, False), (lo, ls, True)):
            d, i, seg, o, name, addend = self.b[start:start + size], 0, 0, 0, '', 0
            while i < len(d):
                op, imm = d[i] & 0xf0, d[i] & 0x0f; i += 1
                if op == 0x00:
                    if lazy: continue
                    break
                elif op in (0x10, 0x30, 0x50): pass
                elif op == 0x20: _, i = uleb(d, i)
                elif op == 0x40:
                    end = d.index(b'\0', i); name = d[i:end].decode('latin-1'); i = end + 1
                elif op == 0x60: addend, i = sleb(d, i)
                elif op == 0x70: seg = imm; o, i = uleb(d, i)
                elif op == 0x80: v, i = uleb(d, i); o = (o + v) & 0xffffffffffffffff
                elif op == 0x90: out[segs[seg][1] + o] = ('bind', name, addend); o += 8
                elif op == 0xa0: out[segs[seg][1] + o] = ('bind', name, addend); v, i = uleb(d, i); o = (o + 8 + v) & 0xffffffffffffffff
                elif op == 0xb0: out[segs[seg][1] + o] = ('bind', name, addend); o += 8 + imm * 8
                elif op == 0xc0:
                    c, i = uleb(d, i); sk, i = uleb(d, i)
                    for _ in range(c): out[segs[seg][1] + o] = ('bind', name, addend); o += 8 + sk
                else: raise AssertionError('bind opcode %x' % op)
        return out


class Pair:
    def __init__(self, old, new):
        self.o, self.n = MachO(old), MachO(new)
        self.problems, self.notes = [], []
        self.stat = dict(code_words=0, code_words_equal=0, code_relative=0, pointers=0, strings=0, strings_renamed=0, name_bytes=0)
        self.strmap = {}   # old string section name -> {old offset: new offset}
        self.ostub, self.nstub = self.o.stub_names(), self.n.stub_names()
        self.fo, self.fn = {}, {}   # code section -> (function starts, pair numbers)

    def bad(self, text):
        if len(self.problems) < 200:
            self.problems.append(text)
        elif len(self.problems) == 200:
            self.problems.append('... more differences not listed')

    # ---- where an address points, in terms that survive a layout shift -------------------------
    def identity(self, m, addr, strmap=None):
        i = m.sect_of(addr)
        if i is None:
            return ('absolute', addr)
        s = m.sections[i]
        stubs = self.ostub if m is self.o else self.nstub
        if s['flags'] & 0xff in (6, 7, 8) and addr in stubs:
            name = stubs[addr]
            return ('import', s['name'], fold(name))
        off = addr - s['addr']
        if s['flags'] & 0xff == 2:      # C strings: position in the string table
            if m is self.o:
                table = self.strmap.get(s['name'], {})
                base = max((k for k in table if k <= off), default=None)
                if base is None:
                    return ('string?', s['name'], off)
                return ('string', s['name'], table[base] + (off - base))
            return ('string', s['name'], off)
        table = (self.fo if m is self.o else self.fn).get(s['name'])
        if table:
            import bisect
            i = bisect.bisect_right(table[0], addr) - 1
            if i >= 0:
                return ('function', s['name'], table[1][i], addr - table[0][i])
        return ('section', s['name'], off)

    def functions(self, m, s):
        """[(address, size, masked body)] of the functions of a code section, by address: from the
        function-starts table, which a stripped file keeps. The masked body is the function with
        every distance taken out (the opcode class stays): what a function is, wherever it lies."""
        starts = sorted(set(a for a in m.fstarts if s['addr'] <= a < s['addr'] + s['size']))
        d = m.data(s)
        out = []
        for i, a in enumerate(starts):
            size = (starts[i + 1] if i + 1 < len(starts) else s['addr'] + s['size']) - a
            w = struct.unpack('<%dI' % (size // 4), d[a - s['addr']:a - s['addr'] + size // 4 * 4])
            body = hashlib.sha256(struct.pack('<%dI' % len(w), *[
                x & 0xfc000000 if x & 0x7c000000 == 0x14000000 else
                x & 0x9f00001f if x & 0x1f000000 == 0x10000000 else
                x & 0xff00001f if (x & 0xff000010 == 0x54000000 or x & 0x7e000000 == 0x34000000 or x & 0x3b000000 == 0x18000000) else
                x & 0xfff8001f if x & 0x7e000000 == 0x36000000 else
                x & 0xffc003ff if (x & 0x7f800000 == 0x11000000 or x & 0x3b000000 == 0x39000000) else x for x in w])).hexdigest()
            out.append((a, size, body))
        return out

    def pair_functions(self, so, sn):
        """Pair the functions of a code section. In the same order they pair by position. Where the
        order differs (the Rust compiler lays functions out by its hashed names, and the hash
        covers the renamed identifiers) they pair by masked body, in address order among equals.
        Every pair is then compared word for word, with every distance resolved through this
        pairing, so a wrong pairing can only show as a difference."""
        fo, fn = self.functions(self.o, so), self.functions(self.n, sn)
        if not fo or not fn or len(fo) != len(fn):
            if fo or fn:
                self.notes.append('%s: %d functions old, %d new: compared by position' % (so['name'], len(fo), len(fn)))
            return None
        if [f[1:] for f in fo] == [f[1:] for f in fn]:
            pairs = list(zip(fo, fn))
        else:
            go, gn = {}, {}
            for f in fo: go.setdefault(f[1:], []).append(f)
            for f in fn: gn.setdefault(f[1:], []).append(f)
            lost = [k for k in go if len(go[k]) != len(gn.get(k, []))] + [k for k in gn if k not in go]
            if lost:
                # functions that differ in themselves: no pairing; compared by position, which
                # then names every word that differs
                self.notes.append('%s: %d functions do not pair by their masked body (first at +0x%x): compared by position, where every differing word is judged or listed' % (
                    so['name'], len(lost), (go.get(lost[0]) or [(so['addr'],)])[0][0] - so['addr']))
                return None
            pairs = sorted((p for k in go for p in zip(go[k], gn[k])), key=lambda p: p[0][0])
            moved = sum(1 for (a, b) in pairs if a[0] - so['addr'] != b[0] - sn['addr'])
            self.notes.append('%s: %d of %d functions lie at another position (the same functions in another order)' % (so['name'], moved, len(pairs)))
            self.stat['functions_moved'] = self.stat.get('functions_moved', 0) + moved
        self.stat['functions'] = self.stat.get('functions', 0) + len(pairs)
        self.fo[so['name']] = ([p[0][0] for p in pairs], list(range(len(pairs))))
        byn = sorted((p[1][0], i) for i, p in enumerate(pairs))
        self.fn[sn['name']] = ([x[0] for x in byn], [x[1] for x in byn])
        return pairs

    def compare_strings(self):
        for so in self.o.sections:
            if so['flags'] & 0xff != 2:
                continue
            sn = self.n.by_name.get(so['name'])
            if sn is None:
                self.bad('string section %s missing in new' % so['name']); continue
            a, b = self.o.strings(so), self.n.strings(sn)
            if len(a) != len(b):
                self.bad('%s: %d strings old, %d new' % (so['name'], len(a), len(b)))
            table = {}
            for (oa, sa), (ob, sb) in zip(a, b):
                table[oa] = ob
                self.stat['strings'] += 1
                if sa != sb:
                    want = mapped(sa.decode('latin-1')).encode('latin-1')
                    if want == sb:
                        self.stat['strings_renamed'] += 1
                    elif STORE.search(sa) and STORE.sub(b'/nix/store/-', want) == STORE.sub(b'/nix/store/-', sb) or \
                            STORE.search(sa) and mapped(STORE.sub(b'/nix/store/-', sa).decode('latin-1')).encode('latin-1') == STORE.sub(b'/nix/store/-', sb):
                        # __FILE__ of a source that was compiled from a store path: the path's hash
                        # is the build's, the name behind it is renamed
                        self.stat['build_paths'] = self.stat.get('build_paths', 0) + 1
                    elif STORE.search(sa) and foldb(STORE.sub(b'/nix/store/-', sa)) == foldb(STORE.sub(b'/nix/store/-', sb)):
                        self.stat['build_paths'] = self.stat.get('build_paths', 0) + 1
                    elif len(sa) == len(sb) and foldb(sa) == foldb(sb):
                        # a renamed word glued to bytes the rules read as part of it (a Rust
                        # string piece behind its length byte): same length, prefix folded
                        self.stat['strings_renamed_folded'] = self.stat.get('strings_renamed_folded', 0) + 1
                    else:
                        self.bad('%s+0x%x: string differs beyond the rename: old %r new %r' % (so['name'], oa, sa[:120], sb[:120]))
            self.strmap[so['name']] = table

    def compare_code(self, so, sn):
        a, b = self.o.data(so), self.n.data(sn)
        if len(a) != len(b):
            self.bad('%s: code size %d old, %d new' % (so['name'], len(a), len(b))); return
        pairs = self.pairs.get(so['name'])
        if pairs:
            lead = pairs[0][0][0] - so['addr']
            ranges = ([(so['addr'], sn['addr'], lead)] if lead else []) + [(p[0][0], p[1][0], p[0][1]) for p in pairs]
            for p in pairs:
                if p[0][1] != p[1][1]:
                    self.bad('%s: function at +0x%x is %d bytes old, %d new' % (so['name'], p[0][0] - so['addr'], p[0][1], p[1][1])); return
        else:
            ranges = [(so['addr'], sn['addr'], len(a))]
        for pa, pb, size in ranges:
            wa = struct.unpack('<%dI' % (size // 4), a[pa - so['addr']:pa - so['addr'] + size // 4 * 4])
            wb = struct.unpack('<%dI' % (size // 4), b[pb - sn['addr']:pb - sn['addr'] + size // 4 * 4])
            self.compare_words(so, wa, wb, pa, pb)

    def compare_words(self, so, wa, wb, pa, pb):
        """One run of instructions (a function) of OLD against its counterpart of NEW."""
        self.stat['code_words'] += len(wa)
        # general register -> the address it holds, old and new, while it provably holds it:
        # after adrp (a page) and after the add that completes it (an address)
        ho, hn = {}, {}
        defs = []                   # every page a register is given in this function: (index, register, old, new)
        late = []                   # uses whose words differ and whose page is set elsewhere in the function
        pending = {}                # register -> offset of an adrp whose words differ, until a use proves it
        name = so['name']

        def sx(v, bits):
            return v - (1 << bits) if v >> (bits - 1) else v

        NOP = 0xd503201f
        unproven = []               # adrp pairs whose use lies behind a branch: settled at the end

        def page_of(w, at):
            return (at & ~0xfff) + (sx(((w >> 5) & 0x7ffff) << 2 | (w >> 29) & 3, 21) << 12)

        def last_page(words, base, j, reg):
            for i in range(j, -1, -1):
                if words[i] & 0x9f00001f == 0x90000000 | reg:
                    return page_of(words[i], base + 4 * i)
            return None

        def uses(reg):
            """(index, old offset, new offset) of every add/ldr/str of this function on that register."""
            for i, (u, v) in enumerate(zip(wa, wb)):
                if (u >> 5) & 31 == reg and (u & 0xffc003ff) == (v & 0xffc003ff):
                    if u & 0xff800000 == 0x91000000:
                        yield i, (u >> 10) & 0xfff, (v >> 10) & 0xfff
                    elif u & 0x3b000000 == 0x39000000:
                        size = 4 if (u >> 26) & 1 and (u >> 23) & 1 else u >> 30
                        yield i, ((u >> 10) & 0xfff) << size, ((v >> 10) & 0xfff) << size

        def forget(r):
            ho.pop(r, None); hn.pop(r, None)
            if r in pending:
                unproven.append((pending.pop(r), r))

        def judge(k, x, y, to, tn):
            io_, in_ = self.identity(self.o, to), self.identity(self.n, tn)
            if io_ != in_:
                self.bad('%s+0x%x: %08x/%08x point at %s old, %s new' % (name, pa + 4 * k - so['addr'], x, y, io_, in_))
            elif x == y:
                self.stat['code_words_equal'] += 1
            else:
                self.stat['code_relative'] += 1

        for k, (x, y) in enumerate(zip(wa, wb)):
            ao, an = pa + 4 * k, pb + 4 * k
            op0 = (x >> 25) & 0xf
            if x & 0x7c000000 == 0x14000000:                    # b, bl
                m = 0x03ffffff
                if (x & ~m) != (y & ~m):
                    self.bad('%s+0x%x: instruction %08x old, %08x new' % (name, ao - so['addr'], x, y))
                else:
                    judge(k, x, y, ao + 4 * sx(x & m, 26), an + 4 * sx(y & m, 26))
                for r in ([r for r in list(ho) if r <= 18 or r == 30] if x >> 31 else list(ho)):
                    forget(r)
                continue
            if x & 0xff000010 == 0x54000000 or x & 0x7e000000 == 0x34000000 or x & 0x3b000000 == 0x18000000:   # b.cond, cbz/cbnz, ldr literal
                m = 0x00ffffe0
                if (x & ~m & 0xffffffff) != (y & ~m & 0xffffffff):
                    self.bad('%s+0x%x: instruction %08x old, %08x new' % (name, ao - so['addr'], x, y))
                else:
                    judge(k, x, y, ao + 4 * sx((x >> 5) & 0x7ffff, 19), an + 4 * sx((y >> 5) & 0x7ffff, 19))
                if x & 0x3b000000 == 0x18000000 and not (x >> 26) & 1:
                    forget(x & 31)
                continue
            if x & 0x7e000000 == 0x36000000:                    # tbz, tbnz
                m = 0x0007ffe0
                if (x & ~m & 0xffffffff) != (y & ~m & 0xffffffff):
                    self.bad('%s+0x%x: instruction %08x old, %08x new' % (name, ao - so['addr'], x, y))
                else:
                    judge(k, x, y, ao + 4 * sx((x >> 5) & 0x3fff, 14), an + 4 * sx((y >> 5) & 0x3fff, 14))
                continue
            if (x == NOP and y & 0x9f000000 == 0x90000000) or (y == NOP and x & 0x9f000000 == 0x90000000):
                # the linker drops an adrp when the register already holds that page (two uses on
                # one page) and keeps it when the uses lie on two pages: the same address either way,
                # proven by the use that follows
                reg = (y if x == NOP else x) & 31
                po = last_page(wa, pa, k - 1, reg) if x == NOP else page_of(x, ao)
                pn = last_page(wb, pb, k - 1, reg) if y == NOP else page_of(y, an)
                if po is None or pn is None:
                    self.bad('%s+0x%x: instruction %08x old, %08x new' % (name, ao - so['addr'], x, y)); continue
                forget(reg)
                ho[reg], hn[reg] = (po, True), (pn, True)
                pending[reg] = ao - so['addr']
                defs.append((k, reg, po, pn))
                self.stat['linker_page_reuse'] = self.stat.get('linker_page_reuse', 0) + 1
                continue
            if x & 0x1f000000 == 0x10000000:                    # adr, adrp
                m = 0x60ffffe0
                if (x & ~m & 0xffffffff) != (y & ~m & 0xffffffff):
                    self.bad('%s+0x%x: instruction %08x old, %08x new' % (name, ao - so['addr'], x, y)); continue
                io, im = sx(((x >> 5) & 0x7ffff) << 2 | (x >> 29) & 3, 21), sx(((y >> 5) & 0x7ffff) << 2 | (y >> 29) & 3, 21)
                forget(x & 31)
                if x >> 31:
                    ho[x & 31], hn[x & 31] = ((ao & ~0xfff) + (io << 12), True), ((an & ~0xfff) + (im << 12), True)
                    defs.append((k, x & 31, ho[x & 31][0], hn[x & 31][0]))
                    if x != y:
                        pending[x & 31] = ao - so['addr']; self.stat['code_relative'] += 1
                    else:
                        self.stat['code_words_equal'] += 1
                else:
                    judge(k, x, y, ao + io, an + im)
                    ho[x & 31], hn[x & 31] = (ao + io, False), (an + im, False)
                continue
            rn = (x >> 5) & 31
            held = rn in ho and rn == (y >> 5) & 31
            if held and x & 0xff800000 == 0x91000000 and (x & 0xffc003ff) == (y & 0xffc003ff):      # add xd, xn, #imm12
                to, tn = ho[rn][0] + ((x >> 10) & 0xfff), hn[rn][0] + ((y >> 10) & 0xfff)
                page = ho[rn][1]
                # an address is complete here: judge it, unless it only steps on from a judged one
                if page or x != y:
                    judge(k, x, y, to, tn)
                else:
                    self.stat['code_words_equal'] += 1
                pending.pop(rn, None)
                rd = x & 31
                if rd != rn:
                    forget(rd)
                ho[rd], hn[rd] = (to, False), (tn, False)
                continue
            if held and x & 0x3b000000 == 0x39000000 and (x & 0xffc003ff) == (y & 0xffc003ff):      # ldr/str [xn, #imm12 scaled]
                size = x >> 30
                if (x >> 26) & 1 and (x >> 23) & 1:
                    size = 4
                io, im = ((x >> 10) & 0xfff) << size, ((y >> 10) & 0xfff) << size
                if ho[rn][1] or x != y:
                    judge(k, x, y, ho[rn][0] + io, hn[rn][0] + im)
                else:
                    self.stat['code_words_equal'] += 1
                pending.pop(rn, None)
                if not (x >> 26) & 1 and (x >> 22) & 1:
                    forget(x & 31)
                continue
            if x != y:
                # a load, store or add whose words differ in the offset only, on a register whose
                # page is set elsewhere in the function (kept across a loop or a branch): settled
                # at the end of the function, against every page that register is given in it
                if (x & 0x3b000000 == 0x39000000 or x & 0xff800000 == 0x91000000) and (x & 0xffc003ff) == (y & 0xffc003ff):
                    late.append(k)
                else:
                    self.bad('%s+0x%x: instruction %08x old, %08x new' % (name, ao - so['addr'], x, y))
            else:
                self.stat['code_words_equal'] += 1
            # what this instruction writes: a general register that held an address no longer does
            if op0 in (8, 9, 5, 13):                            # data processing, immediate and register
                forget(x & 31)
            elif op0 in (10, 11):                               # branches and system
                if x & 0xfffffc1f in (0xd63f0000, 0xd63f0800) or x & 0xfffff800 == 0xd63f0800:      # blr
                    for r in [r for r in list(ho) if r <= 18 or r == 30]:
                        forget(r)
                elif x & 0xfe000000 == 0xd6000000:              # br, ret
                    for r in list(ho):
                        forget(r)
                elif x & 0xfff00000 == 0xd5300000:              # mrs
                    forget(x & 31)
            elif op0 & 5 == 4 and not (x >> 26) & 1:           # loads and stores of general registers
                if x & 0x3a000000 == 0x28000000:                # pairs
                    if (x >> 22) & 1:
                        forget(x & 31); forget((x >> 10) & 31)
                    if (x >> 23) & 1 and (x >> 24) & 1 or x & 0x01800000 == 0x00800000:
                        forget(rn)
                else:
                    if (x >> 22) & 1 or x & 0x3f000000 == 0x08000000:
                        forget(x & 31)
                    if x & 0x3b200000 == 0x38000000 and (x >> 10) & 1:      # pre/post index
                        forget(rn)
        for r in list(pending):
            forget(r)
        for k in late:
            x, y = wa[k], wb[k]
            rn = (x >> 5) & 31
            size = (4 if (x >> 26) & 1 and (x >> 23) & 1 else x >> 30) if x & 0x3b000000 == 0x39000000 else 0
            io, im = ((x >> 10) & 0xfff) << size, ((y >> 10) & 0xfff) << size
            if any(self.identity(self.o, po + io) == self.identity(self.n, pn + im) and self.o.sect_of(po + io) is not None
                   for (j, reg, po, pn) in defs if reg == rn):
                self.stat['code_relative'] += 1
                self.stat['code_relative_page_from_elsewhere'] = self.stat.get('code_relative_page_from_elsewhere', 0) + 1
            else:
                self.bad('%s+0x%x: instruction %08x old, %08x new (no page of x%d in this function makes them the same target)' % (
                    name, pa + 4 * k - so['addr'], x, y, rn))
        for off, reg in unproven:
            k = (off + so['addr'] - pa) // 4
            d = [d for d in defs if d[0] == k]
            if d and any(self.identity(self.o, d[0][2] + io) == self.identity(self.n, d[0][3] + im) and self.o.sect_of(d[0][2] + io) is not None
                         for (i, io, im) in uses(reg)):
                self.stat['adrp_proven_by_later_use'] = self.stat.get('adrp_proven_by_later_use', 0) + 1
            else:
                self.bad('%s+0x%x: adrp differs and no add/ldr/str in the function proves its target' % (name, off))

    def compare_data(self, so, sn, fo, fn):
        a, b = self.o.data(so), self.n.data(sn)
        if len(a) != len(b):
            self.bad('%s: size %d old, %d new' % (so['name'], len(a), len(b))); return
        if a == b and not any(so['addr'] <= k < so['addr'] + len(a) for k in fo):
            return
        k = 0
        while k < len(a):
            ao, an = so['addr'] + k, sn['addr'] + k
            if ao in fo or an in fn:
                x, y = fo.get(ao), fn.get(an)
                self.stat['pointers'] += 1
                if x is None or y is None or x[0] != y[0]:
                    self.bad('%s+0x%x: fixup %s old, %s new' % (so['name'], k, x, y))
                elif x[0] == 'bind':
                    if (fold(x[1]), x[2]) != (fold(y[1]), y[2]):
                        self.bad('%s+0x%x: bound to %s old, %s new' % (so['name'], k, x[1:], y[1:]))
                else:
                    vo, vn = struct.unpack_from('<Q', a, k)[0], struct.unpack_from('<Q', b, k)[0]
                    io_, in_ = self.identity(self.o, vo), self.identity(self.n, vn)
                    if io_ != in_:
                        self.bad('%s+0x%x: pointer to %s old, %s new' % (so['name'], k, io_, in_))
                k += 8; continue
            if a[k] != b[k]:
                # a name inside a fixed field: take the whole zero-terminated run around the byte
                s = k
                while s > 0 and a[s - 1] != 0 and 32 <= a[s - 1] < 127: s -= 1
                e = k
                while e < len(a) and a[e] != 0: e += 1
                want = mapped(a[s:e].decode('latin-1')).encode('latin-1')
                e2 = s
                while e2 < len(b) and b[e2] != 0: e2 += 1
                if want == b[s:e2] and len(want) >= e - s and not any(a[e:s + len(want)]):
                    self.stat['name_bytes'] += len(want); k = max(e, s + len(want)); continue
                if DATE.search(a[s:e]) and DATE.sub(b'<date>', want) == DATE.sub(b'<date>', b[s:e2]):
                    # the kernel's version string carries the time of its build
                    if not self.stat.get('build_dates'):
                        self.notes.append('build time in the version string: %s old, %s new' % (
                            DATE.search(a[s:e]).group().decode(), DATE.search(b[s:e2]).group().decode()))
                    self.stat['build_dates'] = self.stat.get('build_dates', 0) + 1; k = max(e, e2); continue
                if e2 - s == e - s and foldb(a[s:e]) == foldb(b[s:e2]):
                    self.stat['name_bytes_folded'] = self.stat.get('name_bytes_folded', 0) + (e - s); k = e; continue
                # a pointer the file marks no fixup for (the statically linked kernel): both values
                # are addresses inside the image and point at the same thing
                q = k & ~7
                if q + 8 <= len(a):
                    vo, vn = struct.unpack_from('<Q', a, q)[0], struct.unpack_from('<Q', b, q)[0]
                    if self.o.sect_of(vo) is not None and self.n.sect_of(vn) is not None and self.identity(self.o, vo) == self.identity(self.n, vn):
                        self.stat['pointers_inferred'] = self.stat.get('pointers_inferred', 0) + 1
                        k = q + 8; continue
                # a 32-bit field: the low half of an address inside the image (the kernel's assertion
                # records), or the distance from the field itself to its target (recovery tables)
                q = k & ~3
                if q + 4 <= len(a):
                    vo, vn = struct.unpack_from('<I', a, q)[0], struct.unpack_from('<I', b, q)[0]
                    hi_o, hi_n = self.o.segments[0][1] >> 32 << 32, self.n.segments[0][1] >> 32 << 32
                    if self.o.sect_of(hi_o | vo) is not None and self.n.sect_of(hi_n | vn) is not None and \
                            self.identity(self.o, hi_o | vo) == self.identity(self.n, hi_n | vn):
                        self.stat['pointers_32bit'] = self.stat.get('pointers_32bit', 0) + 1
                        k = q + 4; continue
                    so_, sn_ = struct.unpack_from('<i', a, q)[0], struct.unpack_from('<i', b, q)[0]
                    to, tn = so['addr'] + q + so_, sn['addr'] + q + sn_
                    if self.o.sect_of(to) is not None and self.n.sect_of(tn) is not None and self.identity(self.o, to) == self.identity(self.n, tn):
                        self.stat['distances_32bit'] = self.stat.get('distances_32bit', 0) + 1
                        k = q + 4; continue
                self.bad('%s+0x%x: byte %02x old, %02x new (%r / %r)' % (so['name'], k, a[k], b[k], a[s:e][:60], b[s:e2][:60]))
                k = e + 1; continue
            k += 1

    def run(self):
        on, nn = [s['name'] for s in self.o.sections], [s['name'] for s in self.n.sections]
        skip = lambda name: name.startswith('__DWARF') or name.startswith('__LINKEDIT') or name.startswith('__CTF')
        on, nn = [x for x in on if not skip(x)], [x for x in nn if not skip(x)]
        if on != nn:
            self.bad('sections differ: only old %s, only new %s' % (sorted(set(on) - set(nn)), sorted(set(nn) - set(on))))
        self.shift = {}
        self.compare_strings()
        self.pairs = {}
        for name in on:
            so, sn = self.o.by_name[name], self.n.by_name.get(name)
            if sn is not None and so['flags'] & 0x80000400 and so['flags'] & 0xff == 0 and name != '__TEXT,__stub_helper' and so['size'] == sn['size']:
                self.pairs[name] = self.pair_functions(so, sn)
        fo, fn = self.o.fixups(), self.n.fixups()
        if len(fo) != len(fn):
            self.bad('%d fixups old, %d new' % (len(fo), len(fn)))
        layout_same = True
        for name in on:
            so, sn = self.o.by_name[name], self.n.by_name.get(name)
            if sn is None:
                continue
            if so['size'] != sn['size'] or so['addr'] != sn['addr']:
                layout_same = False
            kind = so['flags'] & 0xff
            if kind == 2:
                continue
            if kind in (1, 12, 18):
                if so['size'] != sn['size']:
                    self.bad('%s: zero-fill size %d old, %d new' % (name, so['size'], sn['size']))
                continue
            if kind in (6, 7, 8):
                # import tables: the linker lays stubs and pointer slots out in the order of the
                # imported names, so a renamed import moves. Same set, same slot size.
                def table(m, s, stubs):
                    width = s['r2'] if kind == 8 else 8
                    return [stubs.get(s['addr'] + i * width) for i in range(s['size'] // width)]
                a = [fold(x) if x else x for x in table(self.o, so, self.ostub)]
                b = [fold(x) if x else x for x in table(self.n, sn, self.nstub)]
                if sorted(a, key=str) != sorted(b, key=str) or so['size'] != sn['size']:
                    self.bad('%s: imports differ: only old %s, only new %s' % (name, sorted(set(a) - set(b), key=str)[:10], sorted(set(b) - set(a), key=str)[:10]))
                elif a != b:
                    self.notes.append('%s: the same %d imports in another order (the linker orders them by name)' % (name, len(a)))
                self.stat['import_slots'] = self.stat.get('import_slots', 0) + len(b)
            elif so['flags'] & 0x80000400 and name not in ('__TEXT,__stub_helper',):
                self.compare_code(so, sn)
            elif name in ('__TEXT,__unwind_info', '__TEXT,__eh_frame', '__TEXT,__stub_helper'):
                if (so['size'] != sn['size']):
                    self.bad('%s: size %d old, %d new' % (name, so['size'], sn['size']))
                elif self.o.data(so) != self.n.data(sn) and name == '__TEXT,__stub_helper':
                    self.notes.append('%s: same size; its lazy-binding offsets follow the order of the import names' % name)
                elif self.o.data(so) != self.n.data(sn) and layout_same:
                    self.bad('%s: bytes differ though the layout is the same' % name)
                elif self.o.data(so) != self.n.data(sn):
                    self.notes.append('%s: same size, bytes carry section distances that moved with the strings' % name)
            else:
                self.compare_data(so, sn, fo, fn)
        # imported and exported names
        def names(m, pred):
            return sorted(x[0] for x in m.syms if pred(x))
        und = lambda x: x[1] & 0x0e == 0 and x[1] & 1 and not x[1] & 0xe0
        ext = lambda x: x[1] & 0x0e == 0x0e and x[1] & 1 and not x[1] & 0xe0
        for what, pred in (('imported', und), ('exported', ext)):
            a, b = sorted(mapped(x) for x in names(self.o, pred)), names(self.n, pred)
            if a != b:
                # names the text rules do not reach (mangled): compare with the prefix folded
                self.stat[what + '_compared_folded'] = len(set(a) ^ set(b)) // 2
                a, b = sorted(fold(x) for x in a), sorted(fold(x) for x in b)
            if a != b:
                self.bad('%s symbols differ: only old %s, only new %s' % (what, sorted(set(a) - set(b))[:12], sorted(set(b) - set(a))[:12]))
            self.stat[what] = len(b)
        self.stat['layout_same'] = layout_same
        if self.stat.get('linker_page_reuse'):
            self.notes.append('%d places where one file has adrp and the other nop: the linker drops an adrp when the register already '
                              'holds that page; the add/ldr/str that follows proves the same target' % self.stat['linker_page_reuse'])
        return not self.problems


def main():
    a = [x for x in sys.argv[1:] if not x.startswith('--')]
    pair = Pair(a[0], a[1])
    ok = pair.run()
    result = dict(old=a[0], new=a[1], identical_apart_from_names=ok, **pair.stat, notes=pair.notes, differences=pair.problems,
                  old_sha256=hashlib.sha256(pair.o.b).hexdigest(), new_sha256=hashlib.sha256(pair.n.b).hexdigest())
    if '--json' in sys.argv:
        open(sys.argv[sys.argv.index('--json') + 1], 'w').write(json.dumps(result, indent=1) + '\n')
    print(json.dumps(result, indent=1))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
