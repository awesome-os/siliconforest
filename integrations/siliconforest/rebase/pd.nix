# SPDX-License-Identifier: MIT
# The PureDarwin source every lab recipe builds from, and the one switch between the pinned
# revision and the rebase target. Recipes say
#   pd = (import ../rebase/pd.nix).pd;
# instead of naming a revision themselves.
#
#   SF_PD unset, empty or "tip"   the default since 12 November 2026: upstream's tip below
#   SF_PD=pin                     the revision the lab was pinned to until then (e7aa00a6), with
#                                 the same flake reference the recipes carried, so every pin
#                                 derivation is the one it always was
#
# The switch is an environment variable because the recipes import each other by path and
# nix-build has no argument that reaches every import. The two caches that remember store
# paths by recipe text key on it: m1-boot-speed/tools/bs_nix_path.py and qemu-dev/qd_image.py.
#
# `tip` was upstream-xnu-12377 on 11 November 2026 (clone: sources/puredarwin-rebase, which
# is this stream's own; sources/puredarwin-upstream-next belongs to the Cocoa stream).
let
  lab = "/home/chrisk/src/darwin-lab";
  sources = {
    pin = { checkout = "sources/puredarwin-baseline"; rev = "e7aa00a60f0b56ef35aa24252e81467787bf71ea"; };
    # The SiliconForest tree: upstream PureDarwin c713d8fb9b048a38e2242d67e29b9957ced8c495 plus one commit,
    # the mechanical rename (branch siliconforest/c713d8fb of the clone), made by
    #   tools/sf_rename.py fork sources/puredarwin-rebase c713d8fb9b048a38e2242d67e29b9957ced8c495
    # The same upstream revision and the same rules give the same commit. A later upstream commit is
    # taken the same way: fork it, put the new id here (docs/design/siliconforest-naming.md).
    # SF_PD=pin still names upstream's own tree; the recipes of this branch carry the SiliconForest
    # names and do not build it (build the pin from the commit before the rename).
    tip = { checkout = "sources/puredarwin-rebase"; rev = "cd001928e0223fd77a7850f09e43580b51bde239"; };
  };
  asked = builtins.getEnv "SF_PD";
  which = if asked == "" then "tip" else asked;
  source = sources.${which} or (throw "SF_PD=${asked}: expected pin or tip");
in rec {
  inherit which;
  inherit (source) rev;
  tip = which == "tip";
  # one of two values by source, for the few places where a recipe must differ
  pick = { pin, tip }: if which == "tip" then tip else pin;
  pd = builtins.getFlake "git+file://${lab}/${source.checkout}?submodules=1&shallow=1&rev=${source.rev}";
  pkgs = import pd.inputs.nixpkgs { system = "x86_64-linux"; };
}
