# Fixed RAM parent preparer001

A separate original host builder creates exactly `/private/var/run`, mode0755,
uid0/gid0, in a new sparse copy of an existing ext4 image. The original image is
opened read-only through a retained FD and must match the caller's exact SHA256.
No mount, resize, filesystem format, device, target, SSH or frozen-stager edit.

From repository root, use a fresh owned private output directory:

```
mkdir -m 0700 runs/m1-desktop-010/desktop-network-prepared-base-001
python integrations/puredarwin/m1-network-desktop-service/revisions/002/prepare_ram_parent.py \
  --input artifacts/m1-bringup-009/ramdisk.img \
  --expected-sha256 b1154b2854c9771445a9b1b631d3d48837b35f655a4747665b94c72da10e4d05 \
  --output runs/m1-desktop-010/desktop-network-prepared-base-001/base.img
```

The caller chooses the input and new output only; there is no guest-path option.
Input must be a real regular 16MiB..1GiB extent-based ext4 image. Output parent
must already be owned0700 and cannot be a symlink. Output and every sidecar name
must initially be absent, including unknown/broken symlinks. The original source
FD/name, metadata and streamed SHA are rechecked; the private output directory
identity is rechecked before publication. Large image bytes are streamed in1MiB
chunks; zero chunks become holes. The image is never loaded wholesale in memory.

Before any mutation: require clean read-only e2fsck; require `/private` and
`/private/var` to be real root-owned/group0 non-writable-by-group/other directories;
require exactly absent `/private/var/run`. Existing directory/file/symlink or
unknown/error output refuses. Only then create an unpublished private copy and
run exactly these four fixed debugfs commands:

```
mkdir /private/var/run
set_inode_field /private/var/run mode 040755
set_inode_field /private/var/run uid 0
set_inode_field /private/var/run gid 0
```

The S2-style batch validator requires exact command echoes once/in order, one
strict version banner and no other output. A debugfs error remains a refusal
even if debugfs returns0. Readback requires a new directory inode, mode0755,
uid0/gid0/nlink2; final e2fsck must return0 with all five exact clean passes.
Successful outputs are exclusive hardlink publications: `base.img`0444,
`base.img.record.json`0600 and `base.img.filesystem.log`0600. Sidecars publish
before the image. The record binds input/output bytes+SHA, commands, real inode,
metadata, clean fsck and log SHA. Failures never publish the prepared image;
where available, diagnostics are preserved exclusively in `base.img.failed.log`.
Existing names are never overwritten or removed. No prepared real base was
produced by the source tests; Root owns actual composition and packet admission.

Root's separately scoped producer must bind the prepared base hash before calling
the existing stager. The frozen stager's old BASE_SHA is not silently bypassed or
edited by this tool. No general image editor or arbitrary debugfs commands are
exposed. New output metadata on disk does not establish native runtime behavior.

Replay tests:

```
cd integrations/puredarwin/m1-network-desktop-service/revisions/002
python -m unittest -v test_prepare_ram_parent
```

Ten groups use only fresh32MiB local ext4 fixtures: positive sparse copy/readback,
run existing directory/regular/symlink, parent wrong type/mode/uid/gid/symlink,
wrong SHA, existing output or broken symlink, unsafe output parent, input symlink,
strict malformed/extra command output and actual duplicate-mkdir command failure
with debugfs return0. Every failure checks unchanged original bytes. No Darwin,
networking, hardware or boot success is inferred from these host tests.
