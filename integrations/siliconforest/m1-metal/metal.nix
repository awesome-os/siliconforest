# SPDX-License-Identifier: MIT
# darling-metal's Metal.framework (MPL-2.0 per file; NOTICE) cross-built for arm64 Darwin over Indium
# (./indium.nix: Vulkan, Honeykrisp on the M1) against SiliconForest's libobjc, Foundation and CoreFoundation,
# staged at <prefix>/Frameworks/Metal.framework, with mslc (./mslc.nix) for newLibraryWithSource:, and our
# own test program (test/sf-metal-tri.m, MIT): a triangle drawn off-screen, read back and compared.
# Our changes to MPL-2.0 files are the `substituteInPlace` lines below (published here, MPL-2.0 like the files):
#   MTLDevice.mm  mslc is loaded from <prefix>/lib/libmslc.dylib instead of Darling's /usr/lib/darling
#   MTLRenderPipeline.mm, MTLRenderCommandEncoder.mm  a cast where SiliconForest's NSDictionary subscript
#                 returns id (no lightweight generics there) and the compiler picked another class's
#                 -asIndiumDescriptor
# Headers of ours for what the system lacks: shim/IOSurface (no dma-buf export, no GPU callbacks).
{ prefix ? "/private/var/db/sfmetal" }:
let
  c = import ./common.nix;
  inherit (c) pkgs p triple sdkRoot;
  tc = p.arm64-cross-toolchain;
  libSystem = p.libsystem-arm64;
  cxx = p.libcxxDylib-arm64;
  cxxabi = p.libcxxabiDylib-arm64;
  objc = p.libobjc-arm64;
  foundation = p.foundation-arm64;
  cf = p.coreFoundation-arm64;
  indium = import ./indium.nix { inherit prefix; };
  mslc = import ./mslc.nix { inherit prefix; };
  fw = "${prefix}/Frameworks/Metal.framework";
  iosurfaceSyms = "IOSurfaceLock IOSurfaceUnlock IOSurfaceGetSeed IOSurfaceGetBaseAddress IOSurfaceGetBytesPerRow IOSurfaceGetPlaneCount IOSurfaceGetWidthOfPlane IOSurfaceGetHeightOfPlane IOSurfaceGetBytesPerRowOfPlane IOSurfaceGetBytesPerElementOfPlane IOSurfaceGetBaseAddressOfPlane IOSurfaceGetWidth IOSurfaceGetHeight";
in pkgs.stdenv.mkDerivation {
  pname = "darling-metal-arm64-darwin";
  version = builtins.substring 0 7 c.darlingMetalRev;
  src = c.darlingMetalSrc;
  postPatch = ''
    substituteInPlace src/Metal/MTLDevice.mm --replace-fail '"/usr/lib/darling/libmslc.dylib"' '"${prefix}/lib/libmslc.dylib"'
    substituteInPlace src/Metal/MTLRenderPipeline.mm --replace-fail '[_dict[index] asIndiumDescriptor]' \
      '[(MTLRenderPipelineColorAttachmentDescriptor*)_dict[index] asIndiumDescriptor]'
    substituteInPlace src/Metal/MTLRenderCommandEncoder.mm --replace-fail '[_dict[index] asIndiumDescriptor]' \
      '[(MTLRenderPassColorAttachmentDescriptor*)_dict[index] asIndiumDescriptor]'
  '';
  dontConfigure = true;
  buildPhase = ''
    CXX=${tc}/bin/${triple}-clang++
    CC=${tc}/bin/${triple}-clang
    BASE="-isysroot ${sdkRoot} -mmacosx-version-min=26.5 -Qunused-arguments -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-stack-protector \
      -I${libSystem}/usr/include -I${cf}/include -I${objc}/usr/include -I${foundation}/usr/include -I${./shim}"
    OBJC="$BASE -fblocks -fobjc-exceptions -O1 -g0 -DDARLING_METAL_ENABLED=1 -w -include ${./shim/sf-metal-shim.h} \
      -Iinclude -Iprivate-include -I${c.indiumSrc}/include -I${mslc}/include -I${pkgs.vulkan-headers}/include"
    OBJCXX="$OBJC -nostdinc++ -I${cxx}/usr/include/c++/v1 -std=gnu++17"
    U=""; for s in ${iosurfaceSyms}; do U="$U -Wl,-U,_$s"; done
    LINK="-fuse-ld=${p.native-ld}/bin/ld -nostdlib -isysroot ${sdkRoot} -mmacosx-version-min=26.5 \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,26.5,26.5 \
      -L${libSystem}/usr/lib -L${cxx}/usr/lib -L${cxxabi}/usr/lib -L${cf}/usr/lib -L${objc}/usr/lib -L${foundation}/usr/lib -L${indium}/lib"
    objs=""
    for f in src/Metal/*.mm; do o=dm_$(basename ''${f%.mm}).o; $CXX -x objective-c++ $OBJCXX -c $f -o $o & objs="$objs $o"; done
    wait
    for o in $objs; do [ -f $o ] || { echo "compile failed: $o"; exit 1; }; done
    mkdir -p Metal.framework/Versions/A
    $CXX $LINK -dynamiclib -install_name ${fw}/Versions/A/Metal $objs $U \
      -lindium -lFoundation -lCoreFoundation -lobjc -lc++ -lc++abi -lSystem -o Metal.framework/Versions/A/Metal
    ln -s A Metal.framework/Versions/Current; ln -s Versions/Current/Metal Metal.framework/Metal
    # ours: the triangle, against darling-metal's own headers and SiliconForest's Foundation
    $CC -x objective-c $OBJC -c ${./test/sf-metal-tri.m} -o sf-metal-tri.o
    $CXX $LINK sf-metal-tri.o Metal.framework/Versions/A/Metal -lFoundation -lCoreFoundation -lobjc -lSystem -o sf-metal-tri
  '';
  installPhase = ''
    mkdir -p $out/Frameworks $out/bin $out/lib
    cp -a Metal.framework $out/Frameworks/
    cp sf-metal-tri $out/bin/
    cp ${mslc}/lib/libmslc.dylib $out/lib/
    cp -r LICENSES $out/LICENSES.darling-metal
    ${tc}/bin/${triple}-otool -L $out/Frameworks/Metal.framework/Versions/A/Metal $out/bin/sf-metal-tri > $out/LINKAGE.txt
  '';
  dontFixup = true;
}
