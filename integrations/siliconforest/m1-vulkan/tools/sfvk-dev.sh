#!/bin/bash
# SPDX-License-Identifier: MIT
# Fast loop: put a libvulkan_asahi.dylib from a development build tree (ninja in a nix-shell of
# ../mesa-darwin.nix) into the M1's stage without a full nix build. The library gets the stage's
# install names (as ../stage.nix gives them) and re-hashed page signatures; the stage's own copy
# is kept as lib/libvulkan_asahi.dylib.nix. `sfvk.sh push` puts the nix-built stage back.
#   sfvk-dev.sh <build tree>/src/asahi/vulkan/libvulkan_asahi.dylib
set -euo pipefail
HERE=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
export PATH=$HOME/.nix-profile/bin:$PATH
F="--extra-experimental-features nix-command --extra-experimental-features flakes"
TC=$(nix-build $F -E "(import $HERE/../common.nix).p.arm64-cross-toolchain" --no-out-link)
INT=$TC/bin/arm64-apple-darwin20.4-install_name_tool; OTOOL=$TC/bin/arm64-apple-darwin20.4-otool
D=/private/var/db/sfvk
t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
cp "$1" "$t/libvulkan_asahi.dylib"; chmod u+w "$t/libvulkan_asahi.dylib"; f=$t/libvulkan_asahi.dylib
"$INT" -id "$D/lib/libvulkan_asahi.dylib" "$f"
for old in $("$OTOOL" -L "$f" | awk 'NR>1 {print $1}'); do
  case "$(basename "$old")" in libdrm.2.dylib|libwayland-client.0.dylib) "$INT" -change "$old" "$D/lib/$(basename "$old")" "$f";; esac
done
for r in $("$OTOOL" -l "$f" | awk '/LC_RPATH/ {getline; getline; print $2}'); do "$INT" -delete_rpath "$r" "$f"; done
python3 "$HERE/../../m1-mesa-asahi/tools/cs-rehash.py" "$f" >/dev/null
python3 "$HERE/../../m1-mesa-asahi/tools/cs-rehash.py" --check "$f"
sum=$(sha256sum "$f" | cut -d' ' -f1)
SSH=(ssh -o UserKnownHostsFile="$HOME/.ssh/known_hosts_m1_siliconforest" -o HostKeyAlias=m1-siliconforest
     -o StrictHostKeyChecking=yes -o ConnectTimeout=15 "${SFVK_HOST:-root@10.1.1.211}")
"${SSH[@]}" "cd $D/lib && { [ -e libvulkan_asahi.dylib.nix ] || cp -p libvulkan_asahi.dylib libvulkan_asahi.dylib.nix; } && cat > libvulkan_asahi.dylib.new" < "$f"
"${SSH[@]}" "cd $D/lib && s=\$(shasum -a 256 libvulkan_asahi.dylib.new | cut -d' ' -f1) && [ \"\$s\" = $sum ] && mv libvulkan_asahi.dylib.new libvulkan_asahi.dylib && echo 'dev libvulkan_asahi.dylib $sum'"
