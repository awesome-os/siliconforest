#!/usr/bin/env python3
"""Decode a paused boot-framebuffer capture into a PNG for host inspection.

The raw file is the byte image read from firmware framebuffer DRAM. Geometry
comes from the companion JSON: base, stride, width, height, and depth. Stride
is in bytes, matching integrations/puredarwin/m1-bringup/host_progress.py
(size = stride * height). m1n1's own fb.stride is that byte stride divided by
four; this decoder does not divide it again.

Depth uses the low 8 bits, as m1n1 src/fb.c does. 30 selects pixel2rgb_30 and
32 selects pixel2rgb_24. Channels are rgb_t u8 values:

    30: R = (pixel >> 22) & 255, G = (pixel >> 12) & 255, B = (pixel >> 2) & 255
    32: R = (pixel >> 16) & 255, G = (pixel >> 8) & 255, B = pixel & 255

Pixels are little-endian 32-bit words. The PNG is an inspection of those
bytes. It is not evidence that the laptop panel scanned them out, and it is
not a Darwin boot or a command-line result.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import zlib

MAX_BYTES = 64 << 20
_PNG_SIG = b"\x89PNG\r\n\x1a\n"
_NOTE = (
    b"Comment\x00Host inspection of captured framebuffer bytes. "
    b"Not physical panel proof."
)


class RenderError(ValueError):
    """The capture cannot be decoded, or the PNG path is not exclusive."""


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise RenderError("duplicate JSON key " + str(key))
        result[key] = value
    return result


def _integer(value, name):
    if isinstance(value, bool) or not isinstance(value, int):
        raise RenderError(name + " must be an integer")
    return value


def _geometry(document):
    if not isinstance(document, dict):
        raise RenderError("metadata must be a JSON object")
    source = document["geometry"] if isinstance(document.get("geometry"), dict) else document
    if "geometry" in document and not isinstance(document["geometry"], dict):
        raise RenderError("geometry must be an object")
    fields = {}
    for name in ("base", "stride", "width", "height", "depth"):
        if name not in source:
            raise RenderError("missing geometry field " + name)
        fields[name] = _integer(source[name], name)
        if "geometry" in document and name in document and document[name] != fields[name]:
            raise RenderError("top-level " + name + " disagrees with geometry")
    return fields


def _check_geometry(fields, file_bytes):
    base, stride, width, height, depth = (
        fields["base"], fields["stride"], fields["width"], fields["height"], fields["depth"])
    if not 0 < base < (1 << 64) or base % 4 != 0:
        raise RenderError("base is outside the firmware framebuffer contract")
    if not 0 <= depth <= 0xffffffff or (depth & 0xff) not in (30, 32):
        raise RenderError("depth low 8 bits must be 30 or 32")
    for name, value in (("width", width), ("height", height), ("stride", stride), ("file", file_bytes)):
        if not 0 < value <= MAX_BYTES:
            raise RenderError(name + " is outside 1..64MiB")
    if width > (MAX_BYTES // 4) or width * 4 > stride:
        raise RenderError("width does not fit in stride")
    if stride > MAX_BYTES // height or stride * height != file_bytes:
        raise RenderError("stride*height does not match the raw file, or exceeds 64MiB")
    return depth & 0xff


def pixel_rgb(pixel, depth_low8):
    """Match m1n1 src/fb.c pixel2rgb_30 / pixel2rgb_24, including u8 truncation."""
    pixel &= 0xffffffff
    if depth_low8 == 30:
        return ((pixel >> 22) & 0xff, (pixel >> 12) & 0xff, (pixel >> 2) & 0xff)
    if depth_low8 == 32:
        return ((pixel >> 16) & 0xff, (pixel >> 8) & 0xff, pixel & 0xff)
    raise RenderError("unsupported depth")


def decode_rgb(raw, width, height, stride, depth_low8):
    out = bytearray(width * height * 3)
    dest = 0
    for y in range(height):
        start = y * stride
        for x in range(width):
            word = raw[start + x * 4:start + x * 4 + 4]
            if len(word) != 4:
                raise RenderError("pixel extends past its row")
            red, green, blue = pixel_rgb(int.from_bytes(word, "little"), depth_low8)
            out[dest:dest + 3] = bytes((red, green, blue))
            dest += 3
    return bytes(out)


def _chunk(tag, data):
    body = tag + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xffffffff)


def encode_png(width, height, rgb):
    if len(rgb) != width * height * 3:
        raise RenderError("RGB size does not match width and height")
    scan = bytearray()
    row_bytes = width * 3
    for y in range(height):
        scan.append(0)
        scan.extend(rgb[y * row_bytes:(y + 1) * row_bytes])
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return b"".join((
        _PNG_SIG,
        _chunk(b"IHDR", ihdr),
        _chunk(b"tEXt", _NOTE),
        _chunk(b"IDAT", zlib.compress(bytes(scan), 9)),
        _chunk(b"IEND", b""),
    ))


def _sha256(data):
    return hashlib.sha256(data).hexdigest()


def load_capture(raw_path, metadata_path):
    """Read both inputs. Neither file is opened for write."""
    raw_path = Path(raw_path)
    metadata_path = Path(metadata_path)
    try:
        info = raw_path.stat()
    except OSError as exc:
        raise RenderError("cannot read raw capture") from exc
    if not raw_path.is_file():
        raise RenderError("raw capture is not a regular file")
    try:
        document = json.loads(metadata_path.read_text(), object_pairs_hook=_unique_object)
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise RenderError("cannot read framebuffer metadata") from exc
    fields = _geometry(document)
    depth_low8 = _check_geometry(fields, info.st_size)
    try:
        raw = raw_path.read_bytes()
    except OSError as exc:
        raise RenderError("cannot read raw capture") from exc
    if len(raw) != info.st_size:
        raise RenderError("raw capture changed while reading")
    claimed = document.get("sha256")
    digest = _sha256(raw)
    if claimed is not None and (not isinstance(claimed, str) or claimed.lower() != digest):
        raise RenderError("raw sha256 does not match metadata")
    declared = document.get("bytes")
    if declared is not None and declared != len(raw):
        raise RenderError("metadata bytes does not match the raw file")
    return raw, fields, depth_low8, digest


def render_capture(raw_path, metadata_path):
    raw, fields, depth_low8, digest = load_capture(raw_path, metadata_path)
    rgb = decode_rgb(raw, fields["width"], fields["height"], fields["stride"], depth_low8)
    png = encode_png(fields["width"], fields["height"], rgb)
    receipt = {
        "status": "HOST_FRAMEBUFFER_PNG",
        "host_only_decode": True,
        "physical_panel_observed": False,
        "panel_proof": False,
        "hardware_boot_observed": False,
        "command_line_observed": False,
        "metadata_panel_claim_ignored": True,
        "raw_preserved": True,
        "base": fields["base"],
        "stride": fields["stride"],
        "width": fields["width"],
        "height": fields["height"],
        "depth": fields["depth"],
        "depth_low8": depth_low8,
        "raw_bytes": len(raw),
        "raw_sha256": digest,
        "note": (
            "PNG is a host-side decode of captured framebuffer bytes for inspection. "
            "It does not prove the laptop panel scanned out those pixels."
        ),
    }
    return png, receipt


def publish_png(output, payload):
    path = Path(output)
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        fd = os.open(path, flags, 0o600)
    except FileExistsError as exc:
        raise RenderError("output exists; not overwritten") from exc
    except OSError as exc:
        raise RenderError("cannot create exclusive PNG output") from exc
    view = memoryview(payload)
    try:
        while view:
            written = os.write(fd, view)
            if written <= 0:
                raise RenderError("short write to PNG output")
            view = view[written:]
        os.fsync(fd)
    except BaseException:
        os.close(fd)
        os.unlink(path)
        raise
    os.close(fd)


def _distinct(path, other):
    left = Path(path)
    right = Path(other)
    if left.exists() and right.exists():
        try:
            if left.samefile(right):
                return False
        except OSError:
            pass
    return left.resolve() != right.resolve()


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Decode a paused boot framebuffer capture to PNG. "
                    "The PNG does not prove physical panel scanout.")
    parser.add_argument("raw", help="framebuffer.bin from the paused capture")
    parser.add_argument("metadata", help="framebuffer.json with geometry base,stride,width,height,depth")
    parser.add_argument("-o", "--output", required=True, help="new PNG path; existing files are left untouched")
    args = parser.parse_args(argv)
    if not _distinct(args.output, args.raw) or not _distinct(args.output, args.metadata):
        print("refusing to write the PNG over an input", file=sys.stderr)
        return 1
    try:
        before = Path(args.raw).stat()
        png, receipt = render_capture(args.raw, args.metadata)
        publish_png(args.output, png)
        after = Path(args.raw).stat()
    except RenderError as exc:
        print(str(exc), file=sys.stderr)
        return 1
    if (before.st_ino, before.st_size, before.st_mtime_ns) != (after.st_ino, after.st_size, after.st_mtime_ns):
        print("raw capture was not preserved", file=sys.stderr)
        return 1
    receipt["output"] = str(args.output)
    receipt["png_bytes"] = len(png)
    receipt["png_sha256"] = _sha256(png)
    print(json.dumps(receipt, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
