# SPDX-License-Identifier: MIT
# build-sfvideo.nix with the GL output (usr/bin/sfvideo-gl and the Mesa bundle it loads).
import ./build-sfvideo.nix { gl = true; }
