#!/usr/bin/env python3
"""Save a research agent's final report (the last long assistant text in its transcript) to a file."""
import json, sys
src, dst = sys.argv[1], sys.argv[2]
last = None
for line in open(src, errors='replace'):
    try:
        d = json.loads(line)
    except Exception:
        continue
    m = d.get('message') or {}
    if m.get('role') == 'assistant':
        for c in m.get('content') or []:
            if isinstance(c, dict) and c.get('type') == 'text' and len(c.get('text', '')) > 1500:
                last = c['text']
open(dst, 'w').write("<!-- Research input for the roadmap tickets; produced by a read-only research agent on 2026-11-09. Unverified on hardware. -->\n\n" + (last or 'MISSING'))
print(dst, len(last or ''))
