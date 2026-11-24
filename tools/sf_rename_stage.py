#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Lays out a staged install for tools/sf_rename_switch.sh from built trees.

    sf_rename_stage.py SPEC.json STAGE_DIR

SPEC.json (kept outside git: it names store paths of this workstation):
  {"pieces": [{
      "name":    "cocoa",                       what the manifest's note says
      "new":     "/nix/store/...-renamed",      the tree built from sf-rename
      "old":     "/nix/store/...-old" | null,   the same recipe built from main: what the lab's
                                                records say is installed. null: no record
      "old_note": "main recipe" | ...           how the old hash is known (goes into the note)
      "prefix":  "/",                           where the tree's root goes on the system
      "class":   "direct",                      state | direct | launchd | core | kernel
      "only":    ["usr/lib/*"],                 optional: fnmatch patterns of tree paths taken
      "exclude": ["nix-support/*"],             optional
      "core":    ["usr/lib/dyld"],              optional: tree paths that are class core
      "renamed": {"usr/sbin/sf-netd": "usr/sbin/pd-netd"},   new tree path -> its path in the old tree.
                                                The old path gets a `remove` row, the new one is `absent` before
      "remove_old_only": true,                  files only the old tree has get a `remove` row
      "changed_only": true                      files that are byte for byte the old tree's are left out
                                                (a large tree of which the rename changed a few files)
  }],
  "rows": [["migrate", "/path", "unknown", "-", "0644", "state", "from=/old/path"], ...]   rows added as they are
  }
Writes STAGE_DIR/root/<path> (copies), STAGE_DIR/manifest.tsv and STAGE_DIR/pieces.json.
A later piece that brings a path an earlier one brought replaces it (and says so).
"""
import fnmatch
import hashlib
import json
import os
import shutil
import sys


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def state(path):
    if os.path.islink(path):
        return 'link:' + os.readlink(path)
    if os.path.isfile(path):
        return sha(path)
    return 'absent'


def walk(root):
    for d, dirs, files in os.walk(root):
        dirs.sort()
        for n in sorted(files + [x for x in dirs if os.path.islink(os.path.join(d, x))]):
            yield os.path.relpath(os.path.join(d, n), root)


def main():
    spec = json.load(open(sys.argv[1]))
    stage = sys.argv[2]
    if os.path.exists(stage):
        sys.exit('the stage directory must be new: ' + stage)
    os.makedirs(os.path.join(stage, 'root'))
    rows, seen, summary = {}, {}, []
    for p in spec['pieces']:
        new, old, prefix = p['new'], p.get('old'), p.get('prefix', '/').rstrip('/')
        renamed = p.get('renamed', {})
        n = same = 0
        for rel in walk(new):
            if p.get('only') and not any(fnmatch.fnmatch(rel, g) for g in p['only']):
                continue
            if any(fnmatch.fnmatch(rel, g) for g in p.get('exclude', [])):
                continue
            src, dest = os.path.join(new, rel), prefix + '/' + rel
            if p.get('changed_only') and old is not None and rel not in renamed and state(src) == state(os.path.join(old, rel)):
                # this piece owns the path and the rename left it alone: an earlier piece's file for it is not wanted
                if dest in rows:
                    print('note: %s of %s is dropped: %s owns the path and has it unchanged' % (dest, seen.get(dest), p['name']), file=sys.stderr)
                    staged = os.path.join(stage, 'root', dest.lstrip('/'))
                    if rows[dest][0] == 'install' and os.path.exists(staged):
                        os.remove(staged)
                    del rows[dest]
                same += 1
                continue
            cls = 'core' if rel in p.get('core', []) else p['class']
            note = p['name']
            if old is None:
                was = 'unknown'
            elif rel in renamed:
                was = 'absent'
                note += '; old name %s/%s' % (prefix, renamed[rel])
            else:
                was = state(os.path.join(old, rel))
                note += '; old = ' + p.get('old_note', 'main recipe')
            if dest in seen:
                print('note: %s from %s replaces the one from %s' % (dest, p['name'], seen[dest]), file=sys.stderr)
            seen[dest] = p['name']
            if os.path.islink(src):
                rows[dest] = ('link', dest, was, os.readlink(src), '-', cls, note)
                continue
            out = os.path.join(stage, 'root', dest.lstrip('/'))
            os.makedirs(os.path.dirname(out), exist_ok=True)
            shutil.copyfile(src, out)
            mode = '0755' if os.access(src, os.X_OK) else '0644'
            os.chmod(out, int(mode, 8))
            rows[dest] = ('install', dest, was, sha(out), mode, cls, note)
            n += 1
        gone = 0
        if old is not None:
            olds = {v: k for k, v in renamed.items()}
            for rel in walk(old):
                dest = prefix + '/' + rel
                if rel in olds or (p.get('remove_old_only') and not os.path.lexists(os.path.join(new, rel))):
                    if p.get('only') and rel not in olds and not any(fnmatch.fnmatch(rel, g) for g in p['only']):
                        continue
                    if dest in rows:
                        continue
                    rows[dest] = ('remove', dest, state(os.path.join(old, rel)), '-', '-', p['class'],
                                  p['name'] + '; old name' + ('' if rel not in olds else ' of %s/%s' % (prefix, olds[rel])))
                    gone += 1
        summary.append({'name': p['name'], 'new': new, 'old': old, 'files': n, 'removed': gone, 'unchanged_left_out': same})
    for r in spec.get('rows', []):
        rows[r[0] + ' ' + r[1]] = tuple(r)
    order = {'state': 0, 'direct': 1, 'launchd': 2, 'core': 3, 'kernel': 4, 'none': 5}
    with open(os.path.join(stage, 'manifest.tsv'), 'w') as f:
        f.write('# action\tpath\told sha256 (expected on the system; absent; unknown)\tnew sha256 or link target\tmode\tclass\tnote\n')
        for r in sorted(rows.values(), key=lambda r: (order.get(r[5], 9), r[1], r[0])):
            f.write('\t'.join(r) + '\n')
    json.dump(summary, open(os.path.join(stage, 'pieces.json'), 'w'), indent=1)
    acts = {}
    for r in rows.values():
        acts[r[0]] = acts.get(r[0], 0) + 1
    print(json.dumps({'rows': len(rows), 'by_action': acts, 'pieces': summary}, indent=1))


if __name__ == '__main__':
    main()
