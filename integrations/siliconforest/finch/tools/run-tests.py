#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run isolated M1 command-line checks, keeping timeouts and failures in results."""
import argparse
import json
import os
from pathlib import Path
import resource
import signal
import subprocess
import time


def limits():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    results = []
    for test in json.loads(args.manifest.read_text()):
        start = time.monotonic()
        env = os.environ.copy()
        env.update(test.get("env", {}))
        proc = subprocess.Popen(test["argv"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                env=env, start_new_session=True, preexec_fn=limits)
        timed_out = False
        try:
            output, _ = proc.communicate(timeout=test.get("timeout", 60))
        except subprocess.TimeoutExpired:
            timed_out = True
            os.killpg(proc.pid, signal.SIGKILL)
            output, _ = proc.communicate()
        result = dict(name=test["name"], returncode=proc.returncode, timed_out=timed_out,
                      seconds=round(time.monotonic()-start, 3),
                      output=output.decode("utf-8", "replace"))
        if 'expected' in test:
            result['matches_expected'] = result['output'] == test['expected']
        results.append(result)
        args.output.write_text(json.dumps(results, indent=2)+"\n")
        print(json.dumps(result), flush=True)
    return int(any(r["returncode"] or r["timed_out"] or r.get("matches_expected") is False for r in results))


if __name__ == "__main__":
    raise SystemExit(main())
