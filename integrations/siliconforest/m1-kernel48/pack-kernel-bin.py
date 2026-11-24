#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""sf/kernel.bin for the collection of ./build-kc.nix: the configuration record of the kernel.bin
that is installed (kernel 48), with the names of its command line mapped by the rename rules and
nothing else changed, and the collection.

  pack-kernel-bin.py INSTALLED_KERNEL_BIN COLLECTION OUT_KERNEL_BIN

Why not ../m1-native-boot/install/build.sh: its argument list is not the installed one. It carries
serial_video_mirror=0, which kernel 48's record does not have, and with it the installed list (599
bytes) is longer than a record holds (607). The record format, the keys and the compression are
build.sh's (../m1-native-boot/b1/pdsf_pack.py). The installed record names the owner's file system
and partition: neither file is ever committed, and this tool prints no identifier.
Host only: nothing is installed.
"""
import hashlib, importlib.util, os, re, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', '..', 'tools'))
import sf_rename  # noqa: E402

spec = importlib.util.spec_from_file_location('pdsf_pack', os.path.join(HERE, '..', 'm1-native-boot', 'b1', 'pdsf_pack.py'))
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)


def main():
    installed, collection, out = sys.argv[1:4]
    old = open(installed, 'rb').read()
    records, _ = pack.parse(old)
    assert [r[0] for r in records] == [pack.CONFIG, pack.KERNEL] and all(r[4] for r in records), 'not configuration + kernel'
    _, _, size, _, _, at = records[0]
    lines = old[at:at + size].decode('ascii').splitlines()
    args = lines[0].split('=', 1)[1]
    keys = dict(line.split('=', 1) for line in lines[1:] if line)
    assert pack.config_text(args, **keys) == old[at:at + size], 'the installed record is not what pdsf_pack writes'
    words = args.split(' ')
    new_words = [sf_rename.transform_text(w, {}, 'bootargs') for w in words]
    for a, b in zip(words, new_words):
        assert len(a) == len(b) and a.split('=', 1)[1:] == b.split('=', 1)[1:], 'a value changed'
    left = [w.split('=')[0] for w in new_words if re.match(r'pd', w)]
    assert not left, 'old names left: %s' % left
    kernel = open(os.path.join(collection, 'kernel') if os.path.isdir(collection) else collection, 'rb').read()
    # every renamed argument is a name the collection knows
    unknown = [w.split('=')[0] for a, w in zip(words, new_words) if a != w and not re.search(rb'\x00' + w.split('=')[0].encode() + rb'[=\x00]', kernel)]
    assert not unknown, 'the collection does not know: %s' % unknown
    data = pack.records(pack.config_text(' '.join(new_words), **keys), kernel)
    check, _ = pack.parse(data)
    assert [r[0] for r in check] == [pack.CONFIG, pack.KERNEL] and all(r[4] for r in check)
    assert zlib.decompress(data[check[1][5]:check[1][5] + check[1][2]], 31) == kernel
    open(out, 'wb').write(data)
    print('arguments: %d, renamed: %d, bytes: %d (installed: %d); other keys unchanged: %s' % (
        len(words), sum(1 for a, b in zip(words, new_words) if a != b), len(' '.join(new_words)), len(args), ' '.join(sorted(keys))))
    print('%s  %d bytes  sha256 %s' % (os.path.basename(out), len(data), hashlib.sha256(data).hexdigest()))


if __name__ == '__main__':
    main()
