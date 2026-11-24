"""Host-only decoder tests. The two pixels are fabricated in this process.

Nothing here is a framebuffer capture, a laptop panel, or a Darwin boot.
"""
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

from tools.render_boot_framebuffer import (
    RenderError, decode_rgb, encode_png, main, pixel_rgb, render_capture,
)

# Invented host pixels. Not read from hardware.
HOST_RED, HOST_GREEN, HOST_BLUE = 0x81, 0x42, 0x24
HOST_RED_2, HOST_GREEN_2, HOST_BLUE_2 = 0x10, 0x20, 0x30


def pixel30(red, green, blue):
    return (blue << 2) | (green << 12) | (red << 22)


def pixel32(red, green, blue):
    return blue | (green << 8) | (red << 16)


def le(word):
    return (word & 0xffffffff).to_bytes(4, "little")


def png_rgb(data):
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise AssertionError("not a PNG")
    pos, ihdr, idat = 8, None, b""
    while pos + 12 <= len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        tag = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        crc = struct.unpack(">I", data[pos + 8 + length:pos + 12 + length])[0]
        if (zlib.crc32(tag + chunk) & 0xffffffff) != crc:
            raise AssertionError("bad PNG crc")
        if tag == b"IHDR":
            ihdr = chunk
        elif tag == b"IDAT":
            idat += chunk
        elif tag == b"IEND":
            break
        pos += 12 + length
    width, height, depth, color, compress, filtr, interlace = struct.unpack(">IIBBBBB", ihdr)
    if (depth, color, compress, filtr, interlace) != (8, 2, 0, 0, 0):
        raise AssertionError("unexpected PNG header")
    raw = zlib.decompress(idat)
    rows, index, row_bytes = [], 0, width * 3
    for _ in range(height):
        if raw[index] != 0:
            raise AssertionError("expected filter 0")
        index += 1
        rows.append(raw[index:index + row_bytes])
        index += row_bytes
    if index != len(raw):
        raise AssertionError("trailing scanline bytes")
    return width, height, b"".join(rows)


class PixelTests(unittest.TestCase):
    def test_two_fabricated_pixels_match_m1n1_shifts(self):
        words = (
            pixel30(HOST_RED, HOST_GREEN, HOST_BLUE),
            pixel30(HOST_RED_2, HOST_GREEN_2, HOST_BLUE_2) | (3 << 30) | (3 << 20) | (3 << 10) | 0x3,
        )
        raw = b"".join(le(word) for word in words)
        rgb = decode_rgb(raw, 2, 1, 8, 30)
        # High 2 bits of each 10-bit channel are outside rgb_t and must drop.
        self.assertEqual(rgb, bytes((
            HOST_RED, HOST_GREEN, HOST_BLUE,
            HOST_RED_2, HOST_GREEN_2, HOST_BLUE_2,
        )))
        self.assertEqual(pixel_rgb(1 << 30, 30), (0, 0, 0))
        self.assertEqual(pixel_rgb(1 << 20, 30), (0, 0, 0))
        self.assertEqual(pixel_rgb(1 << 10, 30), (0, 0, 0))
        wide = pixel32(HOST_RED, HOST_GREEN, HOST_BLUE) | (0xAB << 24)
        self.assertEqual(pixel_rgb(wide, 32), (HOST_RED, HOST_GREEN, HOST_BLUE))
        self.assertEqual(decode_rgb(le(wide) + le(0x01020304), 2, 1, 8, 32),
                         bytes((HOST_RED, HOST_GREEN, HOST_BLUE, 0x02, 0x03, 0x04)))

    def test_stride_padding_is_not_a_pixel(self):
        raw = le(pixel32(1, 2, 3)) + b"\xff\xff\xff\xff" + le(pixel32(4, 5, 6)) + b"\xee\xee\xee\xee"
        rgb = decode_rgb(raw, 1, 2, 8, 32)
        self.assertEqual(rgb, bytes((1, 2, 3, 4, 5, 6)))


class CaptureTests(unittest.TestCase):
    def write_pair(self, directory, raw, depth, stride=None, width=2, height=1, **extra):
        stride = 8 if stride is None else stride
        blob = Path(directory) / "framebuffer.bin"
        meta = Path(directory) / "framebuffer.json"
        blob.write_bytes(raw)
        geometry = dict(base=0x900000000, stride=stride, width=width, height=height, depth=depth)
        document = dict(geometry=geometry, bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest(),
                        source="fabricated host fixture", physical_panel_observed=True)
        document.update(extra)
        meta.write_text(json.dumps(document))
        return blob, meta

    def test_png_roundtrip_and_ignored_panel_claim(self):
        raw = le(pixel30(HOST_RED, HOST_GREEN, HOST_BLUE)) + le(pixel30(9, 8, 7))
        with tempfile.TemporaryDirectory() as directory:
            blob, meta = self.write_pair(directory, raw, 0x1001e)
            before = blob.stat()
            meta_bytes = meta.read_bytes()
            png, receipt = render_capture(blob, meta)
            self.assertEqual(blob.stat().st_mtime_ns, before.st_mtime_ns)
            self.assertEqual(blob.stat().st_ino, before.st_ino)
            self.assertEqual(meta.read_bytes(), meta_bytes)
            self.assertEqual(png_rgb(png), (2, 1, bytes((HOST_RED, HOST_GREEN, HOST_BLUE, 9, 8, 7))))
            self.assertIn(b"Not physical panel proof.", png)
            self.assertFalse(receipt["physical_panel_observed"])
            self.assertFalse(receipt["panel_proof"])
            self.assertFalse(receipt["hardware_boot_observed"])
            self.assertFalse(receipt["command_line_observed"])
            self.assertTrue(receipt["host_only_decode"])
            self.assertEqual(receipt["depth_low8"], 30)
            self.assertEqual(receipt["depth"], 0x1001e)

    def test_depth_32_flag_and_exclusive_output_preserves_raw(self):
        raw = le(pixel32(HOST_RED, HOST_GREEN, HOST_BLUE)) + le(pixel32(1, 2, 3))
        with tempfile.TemporaryDirectory() as directory:
            blob, meta = self.write_pair(directory, raw, 0x10020)
            os.utime(blob, ns=(1_700_000_000_000_000_000, 1_700_000_000_000_000_000))
            stamp = blob.stat().st_mtime_ns
            out = Path(directory) / "inspection.png"
            stdout, stderr = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                self.assertEqual(main([str(blob), str(meta), "-o", str(out)]), 0)
            receipt = json.loads(stdout.getvalue())
            self.assertFalse(receipt["physical_panel_observed"])
            self.assertFalse(receipt["panel_proof"])
            self.assertEqual(blob.read_bytes(), raw)
            self.assertEqual(blob.stat().st_mtime_ns, stamp)
            self.assertEqual(png_rgb(out.read_bytes())[2],
                             bytes((HOST_RED, HOST_GREEN, HOST_BLUE, 1, 2, 3)))
            stdout, stderr = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                self.assertEqual(main([str(blob), str(meta), "-o", str(out)]), 1)
            self.assertIn("not overwritten", stderr.getvalue())
            self.assertEqual(png_rgb(out.read_bytes())[2],
                             bytes((HOST_RED, HOST_GREEN, HOST_BLUE, 1, 2, 3)))

    def test_rejects_bad_geometry_without_writing(self):
        raw = le(pixel30(1, 2, 3)) + le(pixel30(4, 5, 6))
        with tempfile.TemporaryDirectory() as directory:
            blob, meta = self.write_pair(directory, raw, 24)
            out = Path(directory) / "no.png"
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(main([str(blob), str(meta), "-o", str(out)]), 1)
            self.assertFalse(out.exists())
            self.assertEqual(blob.read_bytes(), raw)
            for depth in (0, 16, 31, 0x10018):
                with self.subTest(depth=depth):
                    blob, meta = self.write_pair(directory, raw, depth)
                    with self.assertRaises(RenderError):
                        render_capture(blob, meta)
            blob, meta = self.write_pair(directory, raw, 30, stride=4, width=2)
            with self.assertRaises(RenderError):
                render_capture(blob, meta)
            huge = Path(directory) / "huge.bin"
            fd = os.open(huge, os.O_CREAT | os.O_WRONLY, 0o600)
            os.truncate(fd, (64 << 20) + 1)
            os.close(fd)
            meta.write_text(json.dumps(dict(
                geometry=dict(base=0x900000000, stride=(64 << 20) + 1, width=1, height=1, depth=30))))
            with self.assertRaises(RenderError):
                render_capture(huge, meta)
            self.assertEqual(huge.stat().st_size, (64 << 20) + 1)

    def test_rejects_hash_mismatch_bool_and_input_clobber(self):
        raw = le(1) + le(2)
        with tempfile.TemporaryDirectory() as directory:
            blob, meta = self.write_pair(directory, raw, 32, sha256="0" * 64)
            with self.assertRaises(RenderError):
                render_capture(blob, meta)
            blob, meta = self.write_pair(directory, raw, 32, bytes=3)
            with self.assertRaises(RenderError):
                render_capture(blob, meta)
            document = json.loads(meta.read_text())
            document["geometry"]["depth"] = True
            meta.write_text(json.dumps(document))
            with self.assertRaises(RenderError):
                render_capture(blob, meta)
            document = dict(base=4, stride=8, width=2, height=1, depth=32,
                            sha256=hashlib.sha256(raw).hexdigest())
            meta.write_text(json.dumps(document))
            png, receipt = render_capture(blob, meta)
            self.assertEqual(receipt["depth_low8"], 32)
            self.assertEqual(png_rgb(png)[0], 2)
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(main([str(blob), str(meta), "-o", str(blob)]), 1)
            self.assertEqual(blob.read_bytes(), raw)


class PngTests(unittest.TestCase):
    def test_encoder_is_stdlib_png(self):
        encoded = encode_png(2, 1, bytes((1, 2, 3, 4, 5, 6)))
        self.assertEqual(png_rgb(encoded), (2, 1, bytes((1, 2, 3, 4, 5, 6))))


if __name__ == "__main__":
    unittest.main()
