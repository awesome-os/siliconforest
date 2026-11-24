#!/usr/bin/env python3
"""Plan or explicitly fetch selected pinned source files, never execute them.

Fetches only https://raw.githubusercontent.com paths constructed from validated
repository/commit/path tuples. Every response must match the expected Git blob
SHA from the selected manifest. This verifies bytes against inherited pins, not
signatures, ownership, a full checkout, or a reproducible build.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import sys
import tempfile
from typing import Any, Callable
from urllib.request import Request, build_opener, HTTPRedirectHandler
from urllib.error import URLError
from urllib.parse import quote
try:
    from .lab import read_json, ROOT
except ImportError:
    from lab import read_json, ROOT

MAX_FILE_BYTES = 4 * 1024 * 1024
SHA1 = re.compile(r"[0-9a-f]{40}\Z")
REPO = re.compile(r"[A-Za-z0-9_-][A-Za-z0-9_.-]*/[A-Za-z0-9_-][A-Za-z0-9_.-]*\Z")

class AcquisitionError(ValueError):
    pass

def git_blob_sha(data: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()

def checked_entry(entry: dict[str, Any]) -> dict[str, str]:
    if not isinstance(entry,dict): raise AcquisitionError("Source entry must be an object")
    repo, commit, path, blob = (entry.get(k) for k in ("repository","commit","path","blob_sha"))
    if not isinstance(repo,str) or not REPO.fullmatch(repo): raise AcquisitionError("Invalid repository")
    if any(part in {".",".."} for part in repo.split("/")): raise AcquisitionError("Invalid repository")
    if not isinstance(commit,str) or not SHA1.fullmatch(commit): raise AcquisitionError("Commit must be a full immutable SHA")
    if not isinstance(blob,str) or not SHA1.fullmatch(blob): raise AcquisitionError("Missing expected Git blob SHA")
    if not isinstance(path,str) or not re.fullmatch(r"[A-Za-z0-9_./+-]+",path) or any(p in {"", ".", ".."} for p in path.split("/")):
        raise AcquisitionError("Unsafe or unsupported source path")
    return {"repository":repo,"commit":commit,"path":path,"blob_sha":blob}

def select_entries(manifest: dict[str, Any], repositories: list[str], paths: list[str]) -> list[dict[str,str]]:
    entries=manifest.get("inspected_files")
    if not isinstance(entries,list) or not entries or len(entries)>256: raise AcquisitionError("Expected 1..256 inspected_files")
    result=[]; seen=set()
    for raw in entries:
        entry=checked_entry(raw)
        if repositories and entry["repository"] not in repositories: continue
        if paths and entry["path"] not in paths: continue
        identity=(entry["repository"],entry["commit"],entry["path"])
        if identity in seen: raise AcquisitionError("Duplicate selected source identity")
        seen.add(identity); result.append(entry)
    if not result: raise AcquisitionError("No sources match the selection")
    if repositories and set(repositories)-{x["repository"] for x in result}: raise AcquisitionError("One or more selected repositories did not match")
    if paths and set(paths)-{x["path"] for x in result}: raise AcquisitionError("One or more selected paths did not match")
    return result

def source_url(entry: dict[str, str]) -> str:
    e=checked_entry(entry)
    return "https://raw.githubusercontent.com/"+e["repository"]+"/"+e["commit"]+"/"+quote(e["path"],safe="/")

class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise AcquisitionError("Redirect rejected; review the immutable source URL")

def download(url: str) -> bytes:
    # No credentials, cookies, shell, TLS overrides, or arbitrary URL inputs.
    opener=build_opener(NoRedirect())
    request=Request(url,headers={"User-Agent":"darwin-lab-source-acquisition/1"})
    with opener.open(request,timeout=30) as response:
        data=response.read(MAX_FILE_BYTES+1)
    if len(data)>MAX_FILE_BYTES: raise AcquisitionError("Source response exceeds size limit")
    return data

def safe_destination(root: Path, entry: dict[str,str]) -> Path:
    if root.is_symlink(): raise AcquisitionError("Cache root must not be a symlink")
    root.mkdir(parents=True,exist_ok=True)
    destination=root/entry["repository"]/entry["commit"]/entry["path"]
    current=root
    for piece in destination.relative_to(root).parts[:-1]:
        current=current/piece
        if current.is_symlink(): raise AcquisitionError("Symlink in cache path")
        current.mkdir(exist_ok=True)
    if destination.is_symlink(): raise AcquisitionError("Source destination is a symlink")
    return destination

def fetch_one(raw: dict[str, Any], root: Path, transport: Callable[[str],bytes]=download) -> dict[str,Any]:
    entry=checked_entry(raw); path=safe_destination(root,entry); url=source_url(entry)
    cached=path.exists()
    if cached:
        if not path.is_file() or path.stat().st_size>MAX_FILE_BYTES: raise AcquisitionError("Invalid existing cache entry")
        data=path.read_bytes()
    else:
        data=transport(url)
    if not isinstance(data,bytes) or len(data)>MAX_FILE_BYTES: raise AcquisitionError("Invalid source bytes")
    actual=git_blob_sha(data)
    if actual!=entry["blob_sha"]: raise AcquisitionError(f"Git blob mismatch for {entry['repository']}:{entry['path']}; no overwrite performed")
    if not cached:
        temporary=None
        try:
            with tempfile.NamedTemporaryFile(dir=path.parent,prefix=".fetch-",delete=False) as f:
                temporary=Path(f.name); f.write(data); f.flush(); os.fsync(f.fileno())
            # Exclusive linking prevents overwriting a concurrently-created file.
            os.link(temporary,path)
        finally:
            if temporary is not None: temporary.unlink(missing_ok=True)
    return {**entry,"source_url":url,"cache_path":path.relative_to(root).as_posix(),
            "size_bytes":len(data),"sha256":hashlib.sha256(data).hexdigest(),
            "status":"MATCHED_PINNED_BYTES","cache_hit":cached,
            "provenance_limit":"Matches inherited expected blob and commit-addressed URL; not signature or complete-tree verification.",
            "built":False,"executed":False}

def main(argv: list[str]|None=None) -> int:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("command",choices=("plan","fetch"))
    p.add_argument("--manifest",type=Path,default=ROOT/"archive/source-manifest-2026-11-07.json")
    p.add_argument("--repo",action="append",default=[])
    p.add_argument("--path",action="append",default=[])
    p.add_argument("--cache",type=Path,default=ROOT/".source-cache")
    p.add_argument("--allow-network",action="store_true")
    a=p.parse_args(argv)
    try:
        entries=select_entries(read_json(a.manifest),a.repo,a.path)
        if a.command=="plan":
            result={"status":"PLAN_ONLY","network_accessed":False,
                    "sources":[{**e,"url":source_url(e)} for e in entries]}
        else:
            if not a.allow_network: raise AcquisitionError("fetch requires explicit --allow-network")
            result={"status":"FETCH_COMPLETED","sources":[fetch_one(e,a.cache) for e in entries],
                    "build_lock_complete":False,"hardware_accessed":False}
        print(json.dumps(result,indent=2)); return 0
    except (ValueError,OSError,URLError) as exc:
        print(json.dumps({"status":"ACQUISITION_ERROR","error":str(exc)}),file=sys.stderr);return 2
if __name__=="__main__": raise SystemExit(main())
