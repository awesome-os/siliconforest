# SPDX-License-Identifier: MIT
# The media owner adopted the proven Firefox AAC fixes in b796bf971. Build the
# current installed family directly; do not apply the retained historical patches
# twice. Earlier target receipts identify the exact private pre-merge builds.
import ../finch/AudioToolbox/install.nix
