#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The speaker protection after the rename: names only.

Usage: names_only_proof.py [BASE]      (BASE: the last commit before the rename, default 93738b915)

Compares, file by file, the speaker-protection sources at BASE (the same directory under its old name)
with the working tree (integrations/siliconforest/m1-sound): the safety daemon (safetyd/), its
kernel contract (include/), the kernel's voice-coil model and speaker policy (kernel/*speaker*,
kernel/*tas2770*) and the model's equivalence test (model-test/, without its recorded results). It does not use the rename
tool's rule table. For every file:
  1. old and new split into the same number of tokens (words, numbers, everything between);
  2. every token that differs differs only in the two letters of the old prefix -> sf/SF/Sf or in
     the product name (old -> siliconforest, any case), so no digit, operator, string or keyword changed;
  3. the numeric literals are the same list, in the same order;
  4. mapping the changed names back in the new text gives the old file byte for byte.
One phrase is not a one-to-one name change: where the old text named both products the
new says "SiliconForest" (the rename's rule for a phrase that named both). Each such line is
printed; it must be a comment or a description string, and the comparison is made with the phrase
already shortened in the old text.
Over all files: one old name has one new name and the reverse (no two identifiers were merged),
and no new name was already a different word in the old sources (the product name apart).
Then the kernel model (the C file that decides the amplifiers' volume) is compiled before and
after with the host compiler: section bytes identical, symbols identical after mapping back.
Exit 0 only when all of it holds."""
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[4]
# The old names are put together from pieces: the rename tool rewrites literal old names, and this
# file must keep them.
P, PRODUCT_OLD = 'p' + 'd', 'Pure' + 'Darwin'
OLD = 'integrations/%s/m1-sound/' % PRODUCT_OLD.lower()
NEW = 'integrations/siliconforest/m1-sound/'
UNITS = ('safetyd/', 'include/', 'model-test/', 'kernel/%s_speaker' % P, 'kernel/%s_tas2770' % P)
# the model test's recorded output (it holds the SHA-256 of the model's source, which names change)
RESULTS = 'model-test/results/'
TOKEN = re.compile(r'[A-Za-z0-9_]+|[^A-Za-z0-9_]+')
NUMBER = re.compile(r'(?<![A-Za-z_])(?:0[xX][0-9a-fA-F]+|[0-9]+(?:\.[0-9]+)?)(?:[eE][+-]?[0-9]+)?[uUlLfF]*')
BOTH = 'SiliconForest ' + PRODUCT_OLD
PRODUCT = (('SiliconForest', PRODUCT_OLD), ('siliconforest', PRODUCT_OLD.lower()), ('SILICONFOREST', PRODUCT_OLD.upper()))


def norm(token):
    """A token with every sf spelling written as its old spelling: equal for old and new exactly
    when they differ in those letters only."""
    for sf, pd in PRODUCT:
        token = token.replace(sf, pd)
    return token.replace('sf', P).replace('SF', P.upper()).replace('Sf', P.capitalize())


def git(*args):
    return subprocess.run(['git', '-C', str(ROOT)] + list(args), capture_output=True, check=True).stdout


def main():
    base = sys.argv[1] if len(sys.argv) > 1 else '93738b915'
    old_files = [f for f in git('ls-tree', '-r', '--name-only', base, OLD).decode().split('\n')
                 if f and f[len(OLD):].startswith(UNITS) and not f[len(OLD):].startswith(RESULTS)]
    new_files = {norm(str(p.relative_to(ROOT / NEW))): p for p in (ROOT / NEW).rglob('*')
                 if p.is_file() and norm(str(p.relative_to(ROOT / NEW))).startswith(UNITS)
                 and not any(part in ('target', 'build') for part in p.parts)
                 and not str(p.relative_to(ROOT / NEW)).startswith(RESULTS)}
    ok, forward, backward, old_words, changed_files, identical = True, {}, {}, set(), 0, 0
    numbers, phrases, problems_phrase = 0, [], []
    for f in old_files:
        rel = f[len(OLD):]
        a = git('show', '%s:%s' % (base, f))
        p = new_files.pop(rel, None)
        if p is None:
            print('MISSING after the rename: %s' % rel)
            ok = False
            continue
        b = p.read_bytes()
        if a == b:
            identical += 1
            try:
                old_words.update(t for t in TOKEN.findall(a.decode()) if t[0].isalnum() or t[0] == '_')
            except UnicodeDecodeError:
                pass
            continue
        changed_files += 1
        a, b = a.decode(), b.decode()
        for line in a.split('\n'):
            if BOTH in line:
                phrases.append((rel, line.strip()))
                if not line.lstrip().startswith(('//', '#', 'description =', 'speakersafetyd for')):
                    problems_phrase.append((rel, line.strip()))
        a = a.replace(BOTH, 'SiliconForest')
        ta, tb = TOKEN.findall(a), TOKEN.findall(b)
        old_words.update(t for t in ta if t[0].isalnum() or t[0] == '_')
        problems = []
        if len(ta) != len(tb):
            problems.append('token count %d -> %d' % (len(ta), len(tb)))
        else:
            for x, y in zip(ta, tb):
                if x == y:
                    continue
                if norm(x) != norm(y) or norm(x) != x:
                    problems.append('%r -> %r is not a name change' % (x, y))
                    continue
                if forward.setdefault(x, y) != y:
                    problems.append('%r has two new names: %r, %r' % (x, forward[x], y))
                if backward.setdefault(y, x) != x:
                    problems.append('%r is the new name of both %r and %r' % (y, backward[y], x))
            back = ''.join(backward.get(y, y) if x != y else y for x, y in zip(ta, tb))
            if back != a:
                problems.append('mapping the names back does not give the old file')
        na, nb = NUMBER.findall(a), NUMBER.findall(b)
        numbers += len(na)
        if na != nb:
            problems.append('numeric literals differ')
        if problems:
            ok = False
            print('FAILED %s -> %s' % (rel, p.relative_to(ROOT / NEW)))
            for line in problems[:8]:
                print('    ' + line)
        else:
            print('names only: %-44s -> %-44s %5d tokens changed of %6d, %5d numeric literals identical'
                  % (rel, p.relative_to(ROOT / NEW), sum(x != y for x, y in zip(ta, tb)), len(ta), len(na)))
    for rel in sorted(new_files):
        print('NEW file with no old counterpart: %s' % new_files[rel].relative_to(ROOT / NEW))
        ok = False
    for rel, line in phrases:
        print('phrase shortened: %s: %s' % (rel, line[:110]))
    for rel, line in problems_phrase:
        print('FAILED: the phrase is in code, not in a comment or description: %s: %s' % (rel, line))
        ok = False
    # the product name is prose, not an identifier: the old text already said SiliconForest in places
    clash = sorted(n for n in backward if n in old_words and n not in [sf for sf, _ in PRODUCT])
    if clash:
        ok = False
        print('new names that were already words of the old sources: %s' % ' '.join(clash))
    print('%d files: %d byte-identical, %d changed in names only; %d distinct names changed, each one to one; '
          '%d numeric literals compared; new names already present before: %d'
          % (len(old_files), identical, changed_files, len(forward), numbers, len(clash)))

    # the kernel model, compiled before and after
    cc = shutil.which('gcc') or shutil.which('cc')
    work = Path(tempfile.mkdtemp(prefix='sf-speaker-proof-'))
    try:
        result = []
        for side, tree_files in (('old', None), ('new', None)):
            d = work / side
            d.mkdir()
            if side == 'old':
                for f in old_files:
                    if '/kernel/' in f and f.endswith(('.c', '.h')):
                        (d / os.path.basename(f)).write_bytes(git('show', '%s:%s' % (base, f)))
                src = P + '_speaker_model.c'
            else:
                for q in (ROOT / NEW / 'kernel').iterdir():
                    if q.name.startswith(('sf_speaker', 'sf_tas2770')) and q.suffix in ('.c', '.h'):
                        shutil.copy(q, d / q.name)
                src = 'sf_speaker_model.c'
            obj = d / 'model.o'
            r = subprocess.run([cc, '-std=gnu11', '-O2', '-g0', '-ffile-prefix-map=%s=.' % d, '-I', str(d), '-c', src, '-o', str(obj)],
                               cwd=d, capture_output=True, text=True)
            if r.returncode:
                print('the kernel model does not compile on this host (%s): %s' % (side, r.stderr.strip().splitlines()[-1:]))
                ok = False
                break
            sections = {}
            for sec in ('.text', '.rodata', '.data', '.bss'):
                out = str(obj) + sec
                subprocess.run(['objcopy', '-O', 'binary', '--only-section=' + sec + '*', str(obj), out], check=True)
                sections[sec] = '%s/%d' % (hashlib.sha256(open(out, 'rb').read()).hexdigest()[:16], os.path.getsize(out))
            syms = subprocess.run(['nm', '-n', str(obj)], capture_output=True, text=True).stdout
            result.append((sections, '\n'.join(norm(l) for l in syms.split('\n'))))
        if len(result) == 2:
            same = result[0] == result[1]
            print('kernel model (%s, host %s -O2): sections %s; identical before and after: %s; symbols identical after mapping back (%d): %s'
                  % (P + '_speaker_model.c -> sf_speaker_model.c', os.path.basename(cc), result[0][0],
                     result[0][0] == result[1][0], len(result[0][1].splitlines()), result[0][1] == result[1][1]))
            ok = ok and same
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print('names-only-proof:', 'names only; constants, expressions and the model\'s object code identical' if ok else 'FAILED')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
