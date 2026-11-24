#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Analyze paired actual-renderer text paint captures for an owned run."""
import argparse
import json
from pathlib import Path
from legibility_pixels import analyze

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('directory',type=Path);ap.add_argument('--manifest',default='legibility.json');args=ap.parse_args()
    rows=analyze(args.directory,args.manifest)
    print(json.dumps([{k:row[k] for k in ['url','coverage','eligible_runs','runs_with_ink','paint_check_pass']} for row in rows]))
    raise SystemExit(0 if all(row['paint_check_pass'] for row in rows) else 1)
