#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Unpack JVT H.264 conformance archives and write reference hashes for their bitstreams.

    python3 -I jvt_refs.py --ffmpeg /path/to/bin DIR

DIR holds one directory per archive (<name>/<name>.zip, as fetched from
https://www.itu.int/wftp3/av-arch/jvt-site/draft_conformance/). Each archive is unpacked into
<name>/x/ (member names are checked; nothing in it is executed). For every bitstream found:
  <bitstream>.framemd5     ffmpeg -fps_mode passthrough -pix_fmt nv12 -f framemd5
  <bitstream>.planes.md5   planes_md5.py
and one line in DIR/INDEX.tsv: archive, bitstream, profile, size, frames, frame_mbs_only_flag
(from the sequence parameter set, via the trace_headers bitstream filter), entropy coding,
pix_fmt, and whether FFmpeg's decode equals the archive's own decoded YUV (md5 of all frames,
when the archive has one of matching size).
The archives are data: this script only reads them and passes file names to ffmpeg.
"""
import argparse, hashlib, json, os, re, subprocess, sys, zipfile

STREAM_EXT = (".264", ".h264", ".jsv", ".jvt", ".avc", ".26l", ".bit", ".bits", ".bin")
MAX_MEMBER = 400 << 20

def unpack(zpath, dest):
    os.makedirs(dest, exist_ok=True)
    with zipfile.ZipFile(zpath) as z:
        for info in z.infolist():
            name = info.filename.replace("\\", "/")
            if info.is_dir() or name.startswith("/") or ".." in name.split("/") or info.file_size > MAX_MEMBER:
                continue
            if (info.external_attr >> 16) & 0o170000 == 0o120000:      # a symbolic link
                continue
            out = os.path.join(dest, *[p for p in name.split("/") if p])
            if not os.path.realpath(out).startswith(os.path.realpath(dest) + os.sep):
                continue
            os.makedirs(os.path.dirname(out), exist_ok=True)
            if os.path.exists(out) and os.path.getsize(out) == info.file_size:
                continue
            with z.open(info) as src, open(out, "wb") as dst:
                while True:
                    buf = src.read(1 << 20)
                    if not buf:
                        break
                    dst.write(buf)
            os.chmod(out, 0o644)

def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, **kw)

def probe(ff, path):
    r = run([ff + "/ffprobe", "-v", "error", "-f", "h264", "-select_streams", "v:0", "-show_entries",
             "stream=profile,width,height,pix_fmt,level", "-of", "json", "--", path])
    try:
        return json.loads(r.stdout)["streams"][0]
    except (ValueError, KeyError, IndexError):
        return None

def sps_flags(ff, path):
    r = run([ff + "/ffmpeg", "-v", "trace", "-nostdin", "-f", "h264", "-i", path, "-map", "0:v:0", "-c", "copy",
             "-bsf:v", "trace_headers", "-f", "null", "-"])
    text = r.stderr.decode(errors="replace")
    def all_values(field):
        return sorted(set(re.findall(r"\b%s\s+\S+ = (\d+)" % field, text)))
    return all_values("frame_mbs_only_flag"), all_values("entropy_coding_mode_flag"), all_values("field_pic_flag")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ffmpeg", required=True)
    ap.add_argument("dir")
    a = ap.parse_args()
    here = os.path.dirname(os.path.abspath(__file__))
    rows = []
    for name in sorted(os.listdir(a.dir)):
        d = os.path.join(a.dir, name)
        z = os.path.join(d, name + ".zip")
        if not os.path.isfile(z):
            continue
        try:
            unpack(z, os.path.join(d, "x"))
        except (zipfile.BadZipFile, OSError) as e:
            rows.append([name, "-", "UNPACK FAILED: %s" % e])
            continue
        files = []
        for root, _, names in os.walk(os.path.join(d, "x")):
            files += [os.path.join(root, n) for n in names]
        streams = [f for f in files if f.lower().endswith(STREAM_EXT)]
        yuvs = [f for f in files if f.lower().endswith((".yuv", ".qcif", ".cif"))]
        for s in sorted(streams):
            info = probe(a.ffmpeg, s)
            if not info or not info.get("width"):
                rows.append([name, os.path.relpath(s, a.dir), "NOT DECODABLE AS H.264"])
                continue
            fmo, entropy, field = sps_flags(a.ffmpeg, s)
            with open(s + ".framemd5", "w") as out:
                rc = subprocess.run([a.ffmpeg + "/ffmpeg", "-v", "error", "-nostdin", "-threads", "2", "-f", "h264", "-i", s,
                                     "-map", "0:v:0", "-fps_mode", "passthrough", "-pix_fmt", "nv12", "-f", "framemd5", "-"],
                                    stdout=out).returncode
            frames = sum(1 for l in open(s + ".framemd5") if l.strip() and not l.startswith("#"))
            subprocess.run([sys.executable, "-I", os.path.join(here, "planes_md5.py"), "--ffmpeg", a.ffmpeg, s, s + ".planes.md5"])
            # the archive's own decoded pictures, when present: the same bytes as FFmpeg's output?
            conf = "no yuv in archive"
            if info["pix_fmt"] in ("yuv420p", "yuvj420p") and yuvs:
                p = subprocess.Popen([a.ffmpeg + "/ffmpeg", "-v", "error", "-nostdin", "-threads", "2", "-f", "h264", "-i", s,
                                      "-map", "0:v:0", "-fps_mode", "passthrough", "-pix_fmt", "yuv420p", "-f", "rawvideo", "-"],
                                     stdout=subprocess.PIPE)
                h, size = hashlib.md5(), 0
                while True:
                    buf = p.stdout.read(1 << 20)
                    if not buf:
                        break
                    h.update(buf)
                    size += len(buf)
                p.wait()
                conf = "yuv differs or has another size"
                for y in yuvs:
                    if os.path.getsize(y) == size:
                        hy = hashlib.md5()
                        with open(y, "rb") as f:
                            while True:
                                buf = f.read(1 << 20)
                                if not buf:
                                    break
                                hy.update(buf)
                        if hy.digest() == h.digest():
                            conf = "equals " + os.path.basename(y)
                            break
            rows.append([name, os.path.relpath(s, a.dir), info.get("profile", "?"), "%dx%d" % (info["width"], info["height"]),
                         str(frames), "frame_mbs_only=" + ("/".join(fmo) or "?"),
                         "entropy=" + "/".join({"0": "CAVLC", "1": "CABAC"}.get(e, e) for e in entropy),
                         "field_pic=" + ("/".join(field) or "?"), info["pix_fmt"], "ffmpeg rc=%d" % rc, conf])
    with open(os.path.join(a.dir, "INDEX.tsv"), "w") as out:
        out.write("# archive\tbitstream\tprofile\tsize\tframes\tSPS frame_mbs_only_flag\tentropy\tfield_pic_flag\tpix_fmt\tdecode\tarchive yuv\n")
        for r in rows:
            out.write("\t".join(r) + "\n")
    print("%d bitstreams, index in %s/INDEX.tsv" % (len(rows), a.dir))

if __name__ == "__main__":
    sys.exit(main())
