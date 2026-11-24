# SPDX-License-Identifier: MIT
"""Mach-O reading and the two edits a Homebrew pour makes, in plain Python (host side).

Reading: load commands (linked libraries with their ordinals, build version, rpaths, fixup
format, code signature), imported symbols with the library each is bound to (two-level
namespace ordinal in the symbol table), exported symbols (export trie, re-exports named).

Editing, as `brew` does after unpacking a bottle:
  * rewrite the `@@HOMEBREW_PREFIX@@` / `@@HOMEBREW_CELLAR@@` placeholders in LC_ID_DYLIB,
    LC_LOAD_*_DYLIB and LC_RPATH (the job of install_name_tool / ruby-macho),
  * bring the ad-hoc code signature back in line with the changed bytes (the job of
    `codesign --sign - --force`): the page hashes of every CodeDirectory are recomputed in
    place. Nothing else of the signature changes (identifier, flags, special slots stay).

Only 64-bit little-endian images; a universal file is read through its arm64 slice (arm64e
when that is the only arm64 one, or when asked for) and is refused for editing.

For chained-fixup images: the import table of LC_DYLD_CHAINED_FIXUPS (what dyld binds, with
library ordinal and weakness) and the chains themselves (which binds are authenticated, with
which key and diversity: arm64e). Objective-C metadata: classes and categories an image
defines with the selectors they implement, and the selectors an image sends (__objc_selrefs).
"""
import hashlib
import struct

MH_MAGIC_64 = 0xfeedfacf
FAT_MAGIC, FAT_MAGIC_64 = 0xcafebabe, 0xcafebabf
CPU_ARM64 = 0x0100000c
LC_SEGMENT_64, LC_SYMTAB, LC_DYSYMTAB = 0x19, 0x2, 0xb
LC_LOAD_DYLIB, LC_ID_DYLIB, LC_LOAD_WEAK_DYLIB = 0xc, 0xd, 0x80000018
LC_REEXPORT_DYLIB, LC_LAZY_LOAD_DYLIB, LC_LOAD_UPWARD_DYLIB = 0x8000001f, 0x20, 0x80000023
LC_RPATH, LC_CODE_SIGNATURE = 0x8000001c, 0x1d
LC_DYLD_INFO, LC_DYLD_INFO_ONLY = 0x22, 0x80000022
LC_VERSION_MIN_MACOSX, LC_BUILD_VERSION = 0x24, 0x32
LC_DYLD_EXPORTS_TRIE, LC_DYLD_CHAINED_FIXUPS = 0x80000033, 0x80000034
DYLIB_KINDS = {LC_LOAD_DYLIB: 'load', LC_LOAD_WEAK_DYLIB: 'weak', LC_REEXPORT_DYLIB: 'reexport',
               LC_LAZY_LOAD_DYLIB: 'lazy', LC_LOAD_UPWARD_DYLIB: 'upward'}
FILETYPES = {1: 'object', 2: 'execute', 6: 'dylib', 7: 'dylinker', 8: 'bundle', 9: 'dylib_stub', 10: 'dsym', 11: 'kext'}
PLATFORMS = {1: 'macos', 2: 'ios', 6: 'maccatalyst'}


def is_macho(head):
    """True for a thin 64-bit image or a universal file (not a Java class: those share the
    universal magic but have a tiny 'architecture count' that is really a version)."""
    if len(head) < 8:
        return False
    if struct.unpack_from('<I', head)[0] == MH_MAGIC_64:
        return True
    magic, n = struct.unpack_from('>II', head)
    return magic in (FAT_MAGIC, FAT_MAGIC_64) and 0 < n < 20


def version(v):
    return '%d.%d' % (v >> 16, (v >> 8) & 0xff) + ('.%d' % (v & 0xff) if v & 0xff else '')


def cstr(data, off):
    end = data.find(b'\0', off)
    return data[off:end if end >= 0 else len(data)].decode('utf-8', 'replace')


def uleb(data, off):
    result = shift = 0
    while True:
        b = data[off]
        off += 1
        result |= (b & 0x7f) << shift
        shift += 7
        if not b & 0x80:
            return result, off


class MachO:
    def __init__(self, data, path='', prefer=None):
        """prefer: 'arm64e' or 'arm64' picks that slice of a universal file when both are there
        (default: plain arm64 first, as a SiliconForest dyld without arm64e would)."""
        self.path = path
        self.fat = False
        self.slices = []
        self.base = 0
        if struct.unpack_from('>I', data)[0] in (FAT_MAGIC, FAT_MAGIC_64):
            self.fat = True
            wide = struct.unpack_from('>I', data)[0] == FAT_MAGIC_64
            n = struct.unpack_from('>I', data, 4)[0]
            found = None
            for i in range(n):
                if wide:
                    cpu, sub, off, size = struct.unpack_from('>iiQQ', data, 8 + i * 32)
                else:
                    cpu, sub, off, size = struct.unpack_from('>iiII', data, 8 + i * 20)
                name = {CPU_ARM64: 'arm64e' if sub & 0xff == 2 else 'arm64', 0x01000007: 'x86_64'}.get(cpu, hex(cpu & 0xffffffff))
                self.slices.append(name)
                if cpu == CPU_ARM64 and (found is None or name == (prefer or 'arm64')):
                    found = (off, size)
            if not found:
                raise ValueError('no arm64 slice')
            self.base = found[0]
            data = data[found[0]:found[0] + found[1]]
        self.data = data
        magic, self.cputype, self.cpusubtype, self.filetype, self.ncmds, self.sizeofcmds, self.flags, _ = struct.unpack_from('<IiiIIIII', data)
        if magic != MH_MAGIC_64:
            raise ValueError('not a 64-bit Mach-O')
        self.twolevel = bool(self.flags & 0x80)     # MH_TWOLEVEL; Go links flat: a symbol may come from any linked library
        self.arm64e = self.cputype == CPU_ARM64 and (self.cpusubtype & 0xff) == 2
        self.pac_abi = (self.cpusubtype >> 24) & 0xff      # 0x80 | ptrauth ABI version on arm64e
        self.segments = []          # (name, vmaddr, vmsize, fileoff, filesize)
        self.sections = {}          # (segname, sectname) -> (addr, size, offset)
        self.fixups = None          # (dataoff, datasize) of LC_DYLD_CHAINED_FIXUPS
        self.commands = []          # (cmd, offset, size)
        self.dylibs = []            # dict(kind, name, compat, current) in ordinal order
        self.id = None
        self.rpaths = []
        self.platform = self.minos = self.sdk = None
        self.chained = self.dyld_info = False
        self.codesig = self.symtab = self.dysymtab = self.exports = None
        self.first_section = None
        off = 32
        for _ in range(self.ncmds):
            cmd, size = struct.unpack_from('<II', data, off)
            self.commands.append((cmd, off, size))
            if cmd in DYLIB_KINDS or cmd == LC_ID_DYLIB:
                noff, _ts, cur, compat = struct.unpack_from('<IIII', data, off + 8)
                d = dict(kind=DYLIB_KINDS.get(cmd, 'id'), name=cstr(data, off + noff), current=version(cur), compat=version(compat))
                if cmd == LC_ID_DYLIB:
                    self.id = d
                else:
                    self.dylibs.append(d)
            elif cmd == LC_RPATH:
                self.rpaths.append(cstr(data, off + struct.unpack_from('<I', data, off + 8)[0]))
            elif cmd == LC_BUILD_VERSION:
                plat, minos, sdk = struct.unpack_from('<III', data, off + 8)
                self.platform, self.minos, self.sdk = PLATFORMS.get(plat, str(plat)), version(minos), version(sdk)
            elif cmd == LC_VERSION_MIN_MACOSX:
                minos, sdk = struct.unpack_from('<II', data, off + 8)
                self.platform, self.minos, self.sdk = 'macos', version(minos), version(sdk)
            elif cmd == LC_CODE_SIGNATURE:
                self.codesig = struct.unpack_from('<II', data, off + 8)
            elif cmd == LC_SYMTAB:
                self.symtab = struct.unpack_from('<IIII', data, off + 8)
            elif cmd == LC_DYSYMTAB:
                self.dysymtab = struct.unpack_from('<IIIIII', data, off + 8)
            elif cmd == LC_DYLD_CHAINED_FIXUPS:
                self.chained = True
                self.fixups = struct.unpack_from('<II', data, off + 8)
            elif cmd == LC_DYLD_EXPORTS_TRIE:
                self.exports = struct.unpack_from('<II', data, off + 8)
            elif cmd in (LC_DYLD_INFO, LC_DYLD_INFO_ONLY):
                self.dyld_info = True
                f = struct.unpack_from('<10I', data, off + 8)
                if f[9]:
                    self.exports = (f[8], f[9])
            elif cmd == LC_SEGMENT_64:
                nsects = struct.unpack_from('<I', data, off + 64)[0]
                vmaddr, vmsize, fileoff, filesize = struct.unpack_from('<QQQQ', data, off + 24)
                segname = data[off + 8:off + 24].rstrip(b'\0').decode()
                self.segments.append((segname, vmaddr, vmsize, fileoff, filesize))
                for i in range(nsects):
                    s = off + 72 + i * 80
                    ssize, soff = struct.unpack_from('<QI', data, s + 40)
                    self.sections[(data[s + 16:s + 32].rstrip(b'\0').decode(), data[s:s + 16].rstrip(b'\0').decode())] = (
                        struct.unpack_from('<Q', data, s + 32)[0], ssize, soff)
                    stype = struct.unpack_from('<I', data, s + 64)[0] & 0xff
                    if soff and ssize and stype not in (1, 12, 18):      # zerofill kinds have no file bytes
                        self.first_section = soff if self.first_section is None else min(self.first_section, soff)
            off += size

    # ---- reading ---------------------------------------------------------------------
    def imports(self):
        """[(symbol, ordinal, weak)]: undefined external symbols. ordinal 1.. indexes
        self.dylibs; 0 self, 254 flat lookup, 255 main executable."""
        if self.fixups:
            return [(n, o, w) for n, o, w, _a in self.chained_imports()]
        if not self.symtab or not self.dysymtab:
            return []
        symoff, nsyms, stroff, _strsize = self.symtab
        iundef, nundef = self.dysymtab[4], self.dysymtab[5]
        out = []
        for i in range(iundef, min(iundef + nundef, nsyms)):
            strx, ntype, _sect, desc, _value = struct.unpack_from('<IBBHQ', self.data, symoff + i * 16)
            if ntype & 0xe0 or (ntype & 0x0e) != 0 or not ntype & 1:
                continue
            out.append((cstr(self.data, stroff + strx), (desc >> 8) & 0xff, bool(desc & 0x40)))
        return out

    # ---- chained fixups ----------------------------------------------------------------
    def chained_imports(self):
        """[(symbol, ordinal, weak, addend)] of the LC_DYLD_CHAINED_FIXUPS import table, in bind
        ordinal order. ordinal as imports(): 1.. a library, 0 self, 253 weak lookup, 254 flat,
        255 main executable."""
        if not self.fixups:
            return []
        base = self.fixups[0]
        _ver, _starts, imp_off, sym_off, count, fmt, _sfmt = struct.unpack_from('<7I', self.data, base)
        out = []
        for i in range(count):
            if fmt == 1:
                v = struct.unpack_from('<I', self.data, base + imp_off + i * 4)[0]
                o, weak, noff, addend = v & 0xff, bool(v >> 8 & 1), v >> 9, 0
            elif fmt == 2:
                v, addend = struct.unpack_from('<Ii', self.data, base + imp_off + i * 8)
                o, weak, noff = v & 0xff, bool(v >> 8 & 1), v >> 9
            else:
                v, addend = struct.unpack_from('<QQ', self.data, base + imp_off + i * 16)
                o, weak, noff = v & 0xffff, bool(v >> 16 & 1), v >> 32
                o = o & 0xff if o >= 0xfffd else o
            out.append((cstr(self.data, base + sym_off + noff), o, weak, addend))
        return out

    def chain_starts(self):
        """[(segment index, pointer format, page size, segment file offset, [page start ...])]"""
        if not self.fixups:
            return []
        base = self.fixups[0]
        starts = base + struct.unpack_from('<I', self.data, base + 4)[0]
        out = []
        for i in range(struct.unpack_from('<I', self.data, starts)[0]):
            so = struct.unpack_from('<I', self.data, starts + 4 + i * 4)[0]
            if not so:
                continue
            at = starts + so
            _size, page, fmt, segoff, _maxp, npages = struct.unpack_from('<IHHQIH', self.data, at)
            pages = struct.unpack_from('<%dH' % npages, self.data, at + 22)
            out.append((i, fmt, page, self.off_of(next((va for n, va, _vs, _fo, _fs in self.segments if n == '__TEXT'), 0) + segoff) if fmt else 0, list(pages), segoff))
        return out

    def fixup_stats(self):
        """dict: pointer formats seen, rebases, binds, authenticated rebases and binds, and per
        import ordinal whether any bind of it is authenticated (key, address diversity, diversity)."""
        st = dict(formats=set(), rebase=0, bind=0, auth_rebase=0, auth_bind=0, auth_imports={}, plain_imports=set())
        for _i, fmt, page, fileoff, pages, _so in self.chain_starts():
            st['formats'].add(fmt)
            e = fmt in (1, 9, 12)
            stride = 8 if e else 4
            for pi, start in enumerate(pages):
                if start == 0xffff:
                    continue
                off = fileoff + pi * page + start
                while True:
                    raw = struct.unpack_from('<Q', self.data, off)[0]
                    if e:
                        auth, bind, nxt = raw >> 63, raw >> 62 & 1, raw >> 51 & 0x7ff
                        if bind:
                            o = raw & (0xffffff if fmt == 12 else 0xffff)
                            if auth:
                                st['auth_bind'] += 1
                                st['auth_imports'].setdefault(o, set()).add((raw >> 49 & 3, raw >> 48 & 1, raw >> 32 & 0xffff))
                            else:
                                st['bind'] += 1
                                st['plain_imports'].add(o)
                        else:
                            st['auth_rebase' if auth else 'rebase'] += 1
                    else:
                        bind, nxt = raw >> 63, raw >> 51 & 0xfff
                        st['bind' if bind else 'rebase'] += 1
                        if bind:
                            st['plain_imports'].add(raw & 0xffffff)
                    if not nxt:
                        break
                    off += nxt * stride
        return st

    # ---- addresses and pointers ----------------------------------------------------------
    def off_of(self, vmaddr):
        for _n, va, vs, fo, fs in self.segments:
            if va <= vmaddr < va + vs and vmaddr - va < fs:
                return fo + vmaddr - va
        return None

    def _ptr_format(self):
        if not hasattr(self, '_pf'):
            self._pf = {fo: fmt for _i, fmt, _p, fo, _pg, _so in self.chain_starts()}
            self._segfmt = [(fo, fs, self._pf.get(fo, 0)) for _n, _va, _vs, fo, fs in self.segments]
            self._imgbase = next((va for n, va, _vs, _fo, _fs in self.segments if n == '__TEXT'), 0)
        return self._segfmt

    def pointer(self, off):
        """The vmaddr a pointer stored at file offset `off` holds once dyld has rebased it (0 for
        null or a bind)."""
        raw = struct.unpack_from('<Q', self.data, off)[0]
        fmt = next((f for fo, fs, f in self._ptr_format() if fo <= off < fo + fs), 0)
        if fmt == 0 or raw == 0:
            return raw & 0x0000ffffffffffff if not self.fixups else raw
        if fmt in (1, 9, 12):
            if raw >> 62 & 1:
                return 0
            t = raw & 0xffffffff if raw >> 63 else raw & ((1 << 43) - 1)
            return t + self._imgbase if fmt != 1 or raw >> 63 else t
        if raw >> 63:
            return 0
        t = raw & ((1 << 36) - 1)
        return t + self._imgbase if fmt == 6 else t

    # ---- Objective-C -----------------------------------------------------------------------
    def _section(self, sect):
        for seg in ('__DATA_CONST', '__DATA', '__AUTH_CONST', '__AUTH', '__DATA_DIRTY', '__TEXT'):
            if (seg, sect) in self.sections:
                return self.sections[(seg, sect)]
        return None

    def objc_selrefs(self):
        """{selector}: every selector this image names for a message send."""
        s = self._section('__objc_selrefs')
        out = set()
        if s:
            for off in range(s[2], s[2] + s[1], 8):
                o = self.off_of(self.pointer(off))
                if o is not None:
                    out.add(cstr(self.data, o))
        return out

    def _methods(self, list_va):
        o = self.off_of(list_va) if list_va else None
        if o is None:
            return []
        ent, count = struct.unpack_from('<II', self.data, o)
        out, size = [], ent & 0xffff
        if count > 100000 or size not in (12, 24):
            return []
        for i in range(count):
            at = o + 8 + i * size
            if ent & 0x80000000:                       # relative method list
                rel = struct.unpack_from('<i', self.data, at)[0]
                if ent & 0x40000000:                   # names are direct
                    out.append(cstr(self.data, at + rel))
                else:
                    so = self.off_of(self.pointer(at + rel))
                    out.append(cstr(self.data, so) if so is not None else '?')
            else:
                so = self.off_of(self.pointer(at))
                out.append(cstr(self.data, so) if so is not None else '?')
        return out

    def _class(self, va, meta=False):
        o = self.off_of(va)
        if o is None:
            return None, []
        ro = self.off_of(self.pointer(o + 32) & ~7)
        if ro is None:
            return None, []
        no = self.off_of(self.pointer(ro + 24))
        methods = self._methods(self.pointer(ro + 32))
        return (cstr(self.data, no) if no is not None else None), methods

    def objc_defined(self):
        """({class: {'-sel' | '+sel'}}, {'-sel' | '+sel' from categories}): what this image
        implements. Category methods are kept apart because the class they extend may be
        another image's."""
        classes, cats = {}, set()
        s = self._section('__objc_classlist')
        if s:
            for off in range(s[2], s[2] + s[1], 8):
                va = self.pointer(off)
                name, inst = self._class(va)
                if name is None:
                    continue
                co = self.off_of(va)
                _mn, cls = self._class(self.pointer(co)) if co is not None else (None, [])
                classes.setdefault(name, set()).update(['-' + m for m in inst] + ['+' + m for m in cls])
        s = self._section('__objc_catlist')
        if s:
            for off in range(s[2], s[2] + s[1], 8):
                o = self.off_of(self.pointer(off))
                if o is None:
                    continue
                cats.update('-' + m for m in self._methods(self.pointer(o + 16)))
                cats.update('+' + m for m in self._methods(self.pointer(o + 24)))
        return classes, cats

    def exported(self):
        """({symbol}, {symbol: (ordinal, imported name)}) from the export trie: symbols this
        image defines, and symbols it re-exports individually from another library."""
        own, re = set(), {}
        if not self.exports:
            return own, re
        start, size = self.exports
        trie = self.data[start:start + size]
        stack = [(0, '')]
        seen = set()
        while stack:
            off, prefix = stack.pop()
            if off in seen or off >= len(trie):
                continue
            seen.add(off)
            tsize, p = uleb(trie, off)
            if tsize:
                flags, q = uleb(trie, p)
                if flags & 0x08:                         # EXPORT_SYMBOL_FLAGS_REEXPORT
                    ordinal, q = uleb(trie, q)
                    name = cstr(trie, q)
                    re[prefix] = (ordinal, name or prefix)
                else:
                    own.add(prefix)
            p += tsize
            n = trie[p]
            p += 1
            for _ in range(n):
                end = trie.index(b'\0', p)
                edge = trie[p:end].decode('utf-8', 'replace')
                child, p = uleb(trie, end + 1)
                stack.append((child, prefix + edge))
        return own, re

    def signature(self):
        """None, or dict(flags, identifier, adhoc, linker_signed, hash types, code limit)."""
        if not self.codesig:
            return None
        off, size = self.codesig
        blob = self.data[off:off + size]
        if len(blob) < 12 or struct.unpack_from('>I', blob)[0] != 0xfade0cc0:
            return dict(error='no embedded signature superblob')
        count = struct.unpack_from('>I', blob, 8)[0]
        out = dict(directories=[], cms=False)
        for i in range(count):
            typ, boff = struct.unpack_from('>II', blob, 12 + i * 8)
            magic, length = struct.unpack_from('>II', blob, boff)
            if magic == 0xfade0c02:
                ver, flags, hash_off, ident_off, nspecial, ncode, limit, hsize, htype, _plat, pshift = struct.unpack_from('>IIIIIIIBBBB', blob, boff + 8)
                out['directories'].append(dict(at=off + boff, version=ver, flags=flags, hash_offset=hash_off, slots=ncode,
                                               code_limit=limit, hash_size=hsize, hash_type=htype, page_shift=pshift))
                out.setdefault('flags', flags)
                out.setdefault('identifier', cstr(blob, boff + ident_off))
            elif magic == 0xfade0b01 and length > 8:
                out['cms'] = True
        out['adhoc'] = bool(out.get('flags', 0) & 0x2)
        out['linker_signed'] = bool(out.get('flags', 0) & 0x20000)
        return out


def load(path, prefer=None):
    with open(path, 'rb') as f:
        return MachO(f.read(), str(path), prefer)


# ---- editing -------------------------------------------------------------------------
def rewrite_names(data, replace):
    """Return (new bytes, changed) with replace(str)->str applied to every install name and
    rpath. Load commands are re-laid-out; the result must still fit before the first
    section (bottles are linked with -headerpad_max_install_names)."""
    m = MachO(data)
    if m.fat:
        raise ValueError('universal file: not edited')
    out, changed = bytearray(), False
    for cmd, off, size in m.commands:
        raw = data[off:off + size]
        if cmd in DYLIB_KINDS or cmd in (LC_ID_DYLIB, LC_RPATH):
            noff = struct.unpack_from('<I', raw, 8)[0]
            old = cstr(raw, noff)
            new = replace(old)
            if new != old:
                changed = True
                body = raw[:noff] + new.encode() + b'\0'
                body += b'\0' * (-len(body) % 8)
                raw = body[:4] + struct.pack('<I', len(body)) + body[8:]
        out += raw
    if not changed:
        return data, False
    limit = m.first_section if m.first_section is not None else 32 + m.sizeofcmds
    if 32 + len(out) > limit:
        raise ValueError('load commands do not fit: need %d, have %d' % (32 + len(out), limit))
    new = bytearray(data)
    new[32:32 + max(len(out), m.sizeofcmds)] = bytes(out) + b'\0' * max(0, m.sizeofcmds - len(out))
    struct.pack_into('<I', new, 20, len(out))
    return bytes(new), True


HASHES = {1: lambda b: hashlib.sha1(b).digest(), 2: lambda b: hashlib.sha256(b).digest(),
          3: lambda b: hashlib.sha256(b).digest()[:20], 4: lambda b: hashlib.sha384(b).digest()}


def resign(data):
    """Recompute the code-page hashes of every CodeDirectory in place. Returns (bytes,
    slots changed, note). Only for ad-hoc signatures: a signature with a CMS blob (a real
    identity) cannot be repaired this way and is reported instead."""
    m = MachO(data)
    sig = m.signature()
    if sig is None:
        return data, 0, 'unsigned'
    if 'error' in sig:
        return data, 0, sig['error']
    if sig['cms'] or not sig['adhoc']:
        return data, 0, 'not ad-hoc (identity-signed): left alone'
    new = bytearray(data)
    changed = 0
    for d in sig['directories']:
        h = HASHES.get(d['hash_type'])
        if h is None:
            return data, 0, 'unknown hash type %d' % d['hash_type']
        page = 1 << d['page_shift'] if d['page_shift'] else d['code_limit']
        for i in range(d['slots']):
            start = i * page
            digest = h(bytes(new[start:min(start + page, d['code_limit'])]))[:d['hash_size']]
            at = d['at'] + d['hash_offset'] + i * d['hash_size']
            if new[at:at + d['hash_size']] != digest:
                new[at:at + d['hash_size']] = digest
                changed += 1
    return bytes(new), changed, 'ad-hoc'


def verify(data):
    """Number of code pages whose hash does not match (0 = the signature covers the bytes)."""
    return resign(data)[1]
