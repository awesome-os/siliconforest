# SPDX-License-Identifier: MIT
# The SiliconForest tree every recipe of the Cocoa stream builds from: upstream PureDarwin e7aa00a6 (the
# revision this stream is pinned to) plus one generated commit, the mechanical rename (branch
# siliconforest/e7aa00a6 of sources/puredarwin-baseline, made by tools/sf_rename.py fork).
(builtins.getFlake "git+file:///home/chrisk/src/darwin-lab/sources/puredarwin-baseline?submodules=1&shallow=1&rev=04b6ecb44f8d6d8c650b8e3e1912bc57b881265d").packages.x86_64-linux
