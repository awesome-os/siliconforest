# SPDX-License-Identifier: MIT
# Experimental storage; allocation requires SFFX_IOSURFACE_GPU=1 at runtime.
import ./surface-session-build.nix {gpuBacking=true;}
