# SPDX-License-Identifier: MIT
# The installed disk root lacks ioreg; stage this open-source tool privately.
(import ../rebase/pd.nix).pd.packages.x86_64-linux.ioreg-arm64
