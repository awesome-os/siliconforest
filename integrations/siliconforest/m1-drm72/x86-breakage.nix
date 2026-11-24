# What the 7.2-level DRM core breaks in the 6.14-level x86 drivers SiliconForest builds: each
# driver kext is built from the flake's own recipe with this directory's two series applied,
# ninja keeps going after errors, and the compiler's errors are collected instead of failing.
# Nothing is fixed here; the output is the list of call sites.
#
#   nix-build x86-breakage.nix -A amdgpu     (or nouveau, i915, radeon; no -A for all four)
#
# $out/errors.txt        every "error:" line, path-normalised
# $out/by-file.txt       error count per source file
# $out/failed-objects    objects that did not compile, and how many did
let
  pins = import ../m1-rust-kext/pins.nix;
  inherit (pins) pd pkgs;
  p = pd.packages.x86_64-linux;
  series = name: base: dir: pkgs.runCommand name { nativeBuildInputs = [ pkgs.patch ]; } ''
    cp -r ${base} $out
    chmod -R u+w $out
    for q in ${dir}/*.patch; do patch -d $out -p1 -F0 --no-backup-if-mismatch < $q; done
  '';
  one = { name, kext, kmod, target }:
    let
      kmod72 = series "${kmod.name}-drm72" kmod ./patches/drm-kmod;
      oldFlag = "-DSF_DRMKMOD_DIR=${kmod}";
      ninja = "ninja -C build-nix -k 0 ${target}";
    in kext.overrideAttrs (old:
      let
        configurePhase = builtins.replaceStrings [ oldFlag ] [ "-DSF_DRMKMOD_DIR=${kmod72}" ] old.configurePhase;
        buildPhase = builtins.replaceStrings [ ninja ] [ "{ ${ninja} > ninja.log 2>&1 || true; }" ] old.buildPhase;
      in
      assert configurePhase != old.configurePhase;
      assert buildPhase != old.buildPhase;
      {
        pname = "siliconforest-drm72-breakage-${name}";
        src = series "${name}-tree-drm72" old.src ./patches/siliconforest;
        inherit configurePhase buildPhase;
        installPhase = ''
          mkdir -p $out
          cp ninja.log $out/ninja.log
          norm() { sed -e "s|${kmod72}/||g" -e "s|$NIX_BUILD_TOP/[^/]*/||g"; }
          grep -E '(error|fatal error): ' ninja.log | norm | sort -u > $out/errors.txt || true
          cut -d: -f1 $out/errors.txt | sort | uniq -c | sort -rn > $out/by-file.txt
          {
            echo "failed objects: $(grep -c '^FAILED: ' ninja.log || true)"
            echo "ninja: $(grep -E '^\[[0-9]+/[0-9]+\]' ninja.log | tail -1 | cut -d' ' -f1)"
            echo "linked: $(find build-nix -name '${target}' -type d | wc -l) bundle(s), binary present: $(find build-nix -path '*${target}/Contents/MacOS/*' -type f | wc -l)"
            grep '^FAILED: ' ninja.log | norm | sed 's/^FAILED: //' | awk '{print $NF}' | sort || true
          } > $out/failed-objects
          head -3 $out/failed-objects
        '';
      });
in
{
  amdgpu = one { name = "amdgpu"; kext = p.drm-amdgpu; kmod = p.drm-kmod-src; target = "SFDRMamdgpu.kext"; };
  nouveau = one { name = "nouveau"; kext = p.drm-nouveau; kmod = p.drm-kmod-nouveau-src; target = "SFDRMnouveau.kext"; };
  i915 = one { name = "i915"; kext = p.drm-i915; kmod = p.drm-kmod-i915-src; target = "SFDRMi915.kext"; };
  radeon = one { name = "radeon"; kext = p.drm-radeon; kmod = p.drm-kmod-radeon-src; target = "SFDRMradeon.kext"; };
}
