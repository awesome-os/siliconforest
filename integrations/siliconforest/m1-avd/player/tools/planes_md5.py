#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Per-plane reference hashes of a clip, decoded by software FFmpeg.

    planes_md5.py --ffmpeg /path/to/bin CLIP [OUT]

Writes one line per decoded frame, in display (output) order:

    <frame index> <md5 of the Y plane> <md5 of the interleaved UV plane>

Both planes are the visible area (after the stream's cropping), tightly packed: Y is
width*height bytes, UV is width*ceil(height/2) bytes with the samples Cb,Cr interleaved, which is
what an NV12 frame holds without its row padding. 8-bit 4:2:0 streams only (anything else is
converted to NV12 by swscale, which is then not a bit-exact reference: the header says so).
No frame is duplicated or dropped (-fps_mode passthrough). The clip is data: it is only handed
to ffmpeg/ffprobe as an argument.
"""
import argparse, hashlib, json, subprocess, sys

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ffmpeg", required=True, help="directory with ffmpeg and ffprobe")
    ap.add_argument("clip")
    ap.add_argument("out", nargs="?")
    a = ap.parse_args()
    probe = json.loads(subprocess.run(
        [a.ffmpeg + "/ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
         "stream=width,height,pix_fmt,codec_name,profile", "-of", "json", "--", a.clip],
        check=True, capture_output=True).stdout)["streams"][0]
    w, h, pix = probe["width"], probe["height"], probe["pix_fmt"]
    cw, ch = (w + 1) // 2, (h + 1) // 2
    ysize, uvsize = w * h, 2 * cw * ch
    exact = pix in ("yuv420p", "yuvj420p", "nv12")
    out = open(a.out, "w") if a.out else sys.stdout
    out.write("# planes md5 v1: <frame> <md5 Y %dx%d> <md5 UV interleaved %dx%d (bytes per row %d)>\n" % (w, h, cw, ch, 2 * cw))
    out.write("# source %s %s pix_fmt %s%s\n" % (probe.get("codec_name"), probe.get("profile"), pix,
              "" if exact else " (NOT 8-bit 4:2:0: converted, not a bit-exact reference)"))
    p = subprocess.Popen(
        [a.ffmpeg + "/ffmpeg", "-v", "error", "-nostdin", "-threads", "2", "-i", a.clip, "-map", "0:v:0", "-an", "-sn",
         "-fps_mode", "passthrough", "-pix_fmt", "nv12", "-f", "rawvideo", "-"],
        stdout=subprocess.PIPE)
    n = 0
    while True:
        buf = p.stdout.read(ysize + uvsize)
        if len(buf) < ysize + uvsize:
            if buf:
                sys.stderr.write("planes_md5: %d trailing bytes ignored\n" % len(buf))
            break
        out.write("%d %s %s\n" % (n, hashlib.md5(buf[:ysize]).hexdigest(), hashlib.md5(buf[ysize:]).hexdigest()))
        n += 1
    rc = p.wait()
    if rc != 0:
        sys.stderr.write("planes_md5: ffmpeg exited %d for %s\n" % (rc, a.clip))
    return 0 if rc == 0 and n else 1

if __name__ == "__main__":
    sys.exit(main())
