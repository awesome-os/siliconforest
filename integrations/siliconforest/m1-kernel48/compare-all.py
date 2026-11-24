#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The member table: every member of kernel 48 (old names) against the member this recipe builds.

  compare-all.py OLD_COLLECTION NEW_MEMBERS OUT_DIR [OLD_KERNELS]
      OLD_COLLECTION  kernel 48's store path: its nix-support/kc-strip-proof.json names the 56
                      bundles that were linked, and its build command the kernel
      NEW_MEMBERS     nix-build build-kc.nix -A members
      OLD_KERNELS     (optional fourth argument) System/Library/Kernels of kernel 48's kernel
Writes OUT_DIR/<member>.json (compare-members.py) and OUT_DIR/table.tsv; exit status 0 only when
every member's object code is identical apart from names and its property list maps exactly, or
its differences are exactly the ones listed with their reason in ./explained-differences.json.
"""
import json, os, plistlib, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', '..', 'tools'))
import sf_rename  # noqa: E402


def mapped(x):
    if isinstance(x, str):
        return sf_rename.transform_text(x, {}, 'Info.plist')
    if isinstance(x, dict):
        return {mapped(k): mapped(v) for k, v in x.items()}
    if isinstance(x, list):
        return [mapped(v) for v in x]
    return x


EXPLAINED = json.load(open(os.path.join(HERE, 'explained-differences.json')))


def main():
    old, new, out = sys.argv[1:4]
    old_kernel = sys.argv[4] if len(sys.argv) > 4 else None
    os.makedirs(out, exist_ok=True)
    rows, ok = [], True
    pairs = []
    if old_kernel:    # the directory System/Library/Kernels of kernel 48's kernel
        for f in sorted(os.listdir(old_kernel)):
            if os.path.isfile(os.path.join(new, 'kernel', f)) and not f.endswith('.link'):
                pairs.append((f, os.path.join(old_kernel, f), os.path.join(new, 'kernel', f), None, None))
    for e in json.load(open(old + '/nix-support/kc-strip-proof.json')):
        name = e['bundle']
        nname = mapped(name)
        pairs.append((name, e['source'] + '/Contents/MacOS/' + name, '%s/kexts/%s.kext/Contents/MacOS/%s' % (new, nname, nname),
                      e['source'] + '/Contents/Info.plist', '%s/kexts/%s.kext/Contents/Info.plist' % (new, nname)))
    for name, ob, nb, op, np_ in pairs:
        js = os.path.join(out, name + '.json')
        rc = subprocess.run([sys.executable, os.path.join(HERE, 'compare-members.py'), ob, nb, '--json', js], capture_output=True, text=True)
        r = json.load(open(js)) if os.path.exists(js) and rc.returncode in (0, 1) else dict(identical_apart_from_names=False, differences=[rc.stderr[-400:]], notes=[])
        oid = nid = ver = ''
        plist_ok = True
        if op:
            a, b = plistlib.load(open(op, 'rb')), plistlib.load(open(np_, 'rb'))
            oid, nid, ver = a.get('CFBundleIdentifier', ''), b.get('CFBundleIdentifier', ''), b.get('CFBundleVersion', '')
            if mapped(a) != b:
                plist_ok = False
                keys = sorted(k for k in set(mapped(a)) | set(b) if mapped(a).get(k) != b.get(k))
                r.setdefault('differences', []).append('Info.plist differs beyond the rename in: %s' % keys)
            if a.get('CFBundleVersion') != b.get('CFBundleVersion'):
                plist_ok = False
        explained = EXPLAINED.get(name, [])
        only_explained = bool(explained) and plist_ok and sorted(r.get('differences', [])) == sorted(e['difference'] for e in explained)
        good = bool(r['identical_apart_from_names']) and plist_ok
        ok = ok and (good or only_explained)
        rows.append((name, oid, nid, ver, 'identical apart from names' if good else
                     'identical apart from names and %d explained difference(s)' % len(explained) if only_explained else 'DIFFERS',
                     '%d/%d' % (r.get('code_words_equal', 0) + r.get('code_relative', 0) + r.get('linker_page_reuse', 0), r.get('code_words', 0)), str(r.get('code_relative', 0)),
                     str(r.get('strings_renamed', 0)), '; '.join(r.get('notes', []) + r.get('differences', [])[:3])))
        print('\t'.join(rows[-1][:8]), flush=True)
    with open(os.path.join(out, 'table.tsv'), 'w') as f:
        f.write('member\told bundle id\tnew bundle id\tversion\tresult\tcode words proven/total\tby target\tstrings renamed\tnotes\n')
        for row in rows:
            f.write('\t'.join(row) + '\n')
    print('members: %d, identical apart from names: %d, with explained differences: %d, DIFFERS: %d' % (
        len(rows), sum(1 for r in rows if r[4] == 'identical apart from names'), sum(1 for r in rows if 'explained' in r[4]), sum(1 for r in rows if r[4] == 'DIFFERS')))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
