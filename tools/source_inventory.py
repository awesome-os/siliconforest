#!/usr/bin/env python3
"""Offline lexical Rust dependency inventory; not a compiler or transitive graph."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
from typing import Any

MAX_BYTES = 4 * 1024 * 1024
RAW_START = re.compile(r'(?:br|r)(#{0,255})"')
CHAR_LITERAL = re.compile(r"'(?:\\(?:[nrt0\\'\"]|x[0-9a-fA-F]{2}|u\{[0-9a-fA-F_]+\})|[^'\\\n])'")

class SourceError(ValueError):
    pass

def mask_noncode(text: str) -> str:
    """Blank comments/literals, preserving byte positions in Python text and lines.

    Handles nested Rust block comments, raw strings, ordinary strings and char
    literals. Lifetimes remain code. Rejects unterminated constructs rather than
    reporting a misleading empty result. Not a complete Rust/C lexer.
    """
    out = list(text)
    i = 0
    def blank(start: int, end: int) -> None:
        for pos in range(start, end):
            if text[pos] != "\n": out[pos] = " "
    while i < len(text):
        start = i
        if text.startswith("//", i):
            end = text.find("\n", i)
            i = len(text) if end < 0 else end
            blank(start, i)
        elif text.startswith("/*", i):
            level = 1
            i += 2
            while i < len(text) and level:
                if text.startswith("/*", i): level += 1; i += 2
                elif text.startswith("*/", i): level -= 1; i += 2
                else: i += 1
            if level: raise SourceError("Unterminated block comment")
            blank(start, i)
        else:
            raw = RAW_START.match(text, i) if (i == 0 or not (text[i-1].isalnum() or text[i-1] == "_")) else None
            if raw:
                terminator = '"' + raw.group(1)
                end = text.find(terminator, raw.end())
                if end < 0: raise SourceError("Unterminated raw string")
                i = end + len(terminator); blank(start, i)
            elif text[i] == '"':
                i += 1
                while i < len(text):
                    if text[i] == "\\": i += 2
                    elif text[i] == '"': i += 1; break
                    else: i += 1
                else: raise SourceError("Unterminated string")
                if i > len(text): raise SourceError("Unterminated string escape")
                blank(start, i)
            elif text[i] == "'":
                char = CHAR_LITERAL.match(text, i)
                if char: i += len(char.group()); blank(start, i)
                else: i += 1
            else: i += 1
    return "".join(out)

TOKEN = re.compile(r"[A-Za-z_][A-Za-z_0-9]*|::|[{},*]")

def expand_use(body: str) -> list[tuple[str, int]]:
    """Expand nested use trees into qualified paths and leaf offsets.

    Aliases are consumed but not resolved at call sites. Unsupported syntax is
    an error, never a partial successful inventory.
    """
    tokens = [(m.group(), m.start()) for m in TOKEN.finditer(body)]
    remainder = TOKEN.sub("", body)
    if remainder.strip(): raise SourceError(f"Unsupported use syntax: {remainder.strip()!r}")
    cursor = 0
    found: list[tuple[str, int]] = []
    def peek() -> str | None:
        return tokens[cursor][0] if cursor < len(tokens) else None
    def take() -> tuple[str, int]:
        nonlocal cursor
        if cursor >= len(tokens): raise SourceError("Truncated use tree")
        token = tokens[cursor]; cursor += 1; return token
    def node(prefix: list[str]) -> None:
        nonlocal cursor
        if peek() == "::": take()
        parts = list(prefix)
        leaf = tokens[cursor][1] if cursor < len(tokens) else 0
        while True:
            if peek() == "{":
                take()
                while peek() != "}":
                    if peek() is None: raise SourceError("Unterminated use group")
                    node(parts)
                    if peek() == ",": take()
                    elif peek() != "}": raise SourceError("Expected comma in use group")
                take(); return
            name, leaf = take()
            if name != "*" and not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", name):
                raise SourceError("Expected an import name")
            if name != "self" or not parts: parts.append(name)
            if peek() == "::": take(); continue
            if peek() == "as":
                take(); alias, _ = take()
                if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", alias): raise SourceError("Invalid import alias")
            found.append(("::".join(parts), leaf)); return
    node([])
    if cursor != len(tokens): raise SourceError("Unexpected trailing use tokens")
    return found

CATEGORY = {
    "drm": "graphics-and-buffer-management", "dma": "dma-and-memory-ownership",
    "mm": "virtual-memory", "page": "virtual-memory", "io_pgtable": "iommu-and-page-tables",
    "sync": "synchronization-and-reference-lifetimes", "device": "device-model",
    "platform": "platform-discovery", "of": "device-tree", "apple_rtkit": "firmware-transport",
    "soc": "platform-services", "workqueue": "execution-context", "time": "timing",
    "bindings": "generated-linux-bindings", "prelude": "implicit-prelude-needs-expansion",
    "uapi": "userspace-contract", "macros": "kernel-macro-expansion",
}

def inventory(text: str, name: str, line_offset: int = 0, scope: str = "provided_file") -> dict[str, Any]:
    if line_offset < 0: raise SourceError("Negative line offset")
    if scope not in {"provided_file", "excerpt", "synthetic_fixture"}: raise SourceError("Invalid scope")
    masked = mask_noncode(text)
    references: list[dict[str, Any]] = []
    spans = []
    def add(path: str, pos: int, kind: str) -> None:
        if not path.startswith("kernel::"): return
        parts = path.split("::")
        references.append({"symbol": path, "line": line_offset + text.count("\n", 0, pos) + 1,
                           "kind": kind, "category": CATEGORY.get(parts[1], "requires-manual-review")})
    for match in re.finditer(r"\buse\s+([^;]+);", masked):
        spans.append(match.span())
        for path, pos in expand_use(match.group(1)):
            add(path, match.start(1) + pos, "import")
    for match in re.finditer(r"\bkernel::[A-Za-z_][A-Za-z_0-9]*(?:::[A-Za-z_][A-Za-z_0-9]*)*!?", masked):
        if any(start <= match.start() < end for start, end in spans): continue
        add(match.group().rstrip("!"), match.start(), "qualified_reference")
    references.sort(key=lambda item: (item["line"], item["symbol"], item["kind"]))
    spdx = re.search(r"SPDX-License-Identifier:\s*([^\r\n*]+)", text)
    return {"schema_version": 1, "file": name, "scope": scope, "line_offset": line_offset,
            "content_sha256": hashlib.sha256(text.encode()).hexdigest(),
            "license_declaration": spdx.group(1).strip() if spdx else None,
            "references": references, "reference_count": len(references),
            "unique_symbols": sorted({r["symbol"] for r in references}),
            "limits": ["Lexical inventory only; cfg branches are not evaluated.",
                       "Imported aliases and glob contents are not resolved at call sites.",
                       "No macro expansion, type resolution, or transitive dependency analysis.",
                       "Source presence does not establish portability or working hardware."]}

def read_source(path: Path) -> str:
    if path.is_symlink() or not path.is_file(): raise SourceError(f"Expected regular non-symlink file: {path}")
    if path.stat().st_size > MAX_BYTES: raise SourceError("Source exceeds size limit")
    return path.read_text(encoding="utf-8")

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--scope", choices=("provided_file", "excerpt", "synthetic_fixture"), default="provided_file")
    parser.add_argument("--line-offset", type=int, default=0)
    args = parser.parse_args(argv)
    try:
        results = [inventory(read_source(p), p.name, args.line_offset, args.scope) for p in args.paths]
        print(json.dumps({"status":"LEXICAL_INVENTORY", "files": results}, indent=2)); return 0
    except (SourceError, OSError, UnicodeError) as exc:
        print(json.dumps({"status":"INPUT_ERROR", "error":str(exc)}), file=sys.stderr); return 2
if __name__ == "__main__": raise SystemExit(main())
