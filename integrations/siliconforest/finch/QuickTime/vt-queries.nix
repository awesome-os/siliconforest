# SPDX-License-Identifier: MIT
# Private additive wrapper. Retain the exact selected decoder/encoder child.
{ provider ? import ../VideoToolbox/encoder/build.nix }:
let e=import ../environment.nix;cm=import ../CoreMedia/build.nix;
in e.pkgs.runCommand "siliconforest-vt-decoder-queries-stage-arm64" {} ''
 export DARWIN_SDK_ROOT=${e.sdk}
 F=$out/System/Library/Frameworks/VideoToolbox.framework
 mkdir -p $(dirname "$F")
 cp -R ${provider}/System/Library/Frameworks/VideoToolbox.framework "$F"
 chmod -R u+w $out
 A=$F/Versions/A
 mv $A/VideoToolbox $A/VideoToolbox.pd
 ${e.p.arm64-cross-toolchain}/bin/arm64-apple-darwin20.4-install_name_tool -id @loader_path/VideoToolbox.pd $A/VideoToolbox.pd
 ${e.pkgs.python3}/bin/python3 ${../../m1-mesa-asahi/tools/cs-rehash.py} $A/VideoToolbox.pd
 ${e.cc} -O2 -isysroot ${e.sdk} -I${e.libsystem}/usr/include -I${e.p.coreFoundation-arm64}/include \
  -dynamiclib ${./VTDecoderQueries.c} --ld-path=${e.ld} -nostdlib \
  -L${e.libsystem}/usr/lib -L${e.p.coreFoundation-arm64}/usr/lib -lCoreFoundation -lSystem \
  -F${cm}/System/Library/Frameworks -framework CoreMedia -Wl,-reexport_library,$A/VideoToolbox.pd \
  -Wl,-exported_symbol,_VTIsStereoMVHEVCDecodeSupported -Wl,-exported_symbol,_VTCopyVideoDecoderExtensionProperties \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-install_name,/System/Library/Frameworks/VideoToolbox.framework/Versions/A/VideoToolbox \
  -Wl,-compatibility_version,1 -Wl,-current_version,1 -Wl,-platform_version,macos,26.5,26.5 -o $A/VideoToolbox
 cat >> $A/Headers/VideoToolbox.h <<'DECLARATIONS'
#ifdef __cplusplus
extern "C" {
#endif
enum { kVTCouldNotFindExtensionErr = -19510 };
Boolean VTIsStereoMVHEVCDecodeSupported(void);
OSStatus VTCopyVideoDecoderExtensionProperties(CMFormatDescriptionRef,CFDictionaryRef *);
#ifdef __cplusplus
}
#endif
DECLARATIONS
 mkdir -p $A/Resources/Source $out/share
 if [ -d ${provider}/share/licenses ]; then
  cp -R ${provider}/share/licenses $out/share/
  chmod -R u+w $out/share
 fi
 mkdir -p $out/share/licenses/Finch
 cp ${./VTDecoderQueries.c} $A/Resources/Source/
 cp ${e.finch}/LICENSE* $out/share/licenses/Finch/
 mkdir -p $A/Resources/Licenses
 cp -R $out/share/licenses/. $A/Resources/Licenses/
''
