#!/bin/bash
# The shared hardware bench queue: first come first served, re-entrant, compatible with
# plain `flock runs/bench.lock`. See tools/bench.py.
#   tools/bench.sh run <command...> | status | waiting | held
exec python3 -B "$(dirname "$(readlink -f "$0")")/bench.py" "$@"
