# SPDX-License-Identifier: MIT
# Build the public desktop GL dispatch API against EXACTLY the installed Asahi
# Mesa source. It uses Mesa's generated C entries and its live dispatch table;
# it neither mixes another Mesa version's slot numbers nor brings in GLX/X11.
{ prefix ? "/private/var/db/sffx/gl-v1" }:
let
  c = import ../m1-vulkan/common.nix;
  inherit (c) pkgs p triple sdkRoot;
  tc = p.arm64-cross-toolchain;
  system = p.libsystem-arm64;
  mesa = import ../m1-vulkan/mesa-darwin.nix;
in pkgs.stdenv.mkDerivation {
  pname = "siliconforest-asahi-desktop-gl-api-arm64";
  version = c.mesaVersion;
  src = c.mesaSrc;
  nativeBuildInputs = [ c.pythonEnv ];
  dontConfigure = true;
  buildPhase = ''
    # Mesa's Linux libGL ABI list omits entries that native Mac clients resolve
    # directly from OpenGL.framework, including core timer queries. Generate
    # their real signatures/slots from the SAME Mesa XML and live dispatch.
    cp src/glx/libgl-symbols.txt sffx-gl-symbols.txt
    cat >> sffx-gl-symbols.txt <<'EOF'
    glEGLImageTargetTexture2DOES
    glEGLImageTargetRenderbufferStorageOES
    glBindBufferOffsetEXT
    glQueryCounter
    glGetQueryObjecti64v
    glGetQueryObjectui64v
    glGetInternalformativ
    glPolygonOffsetClamp
    glClipControl
    EOF
    python3 src/mesa/glapi/mapi_abi.py --printer glapi \
      --gl_symbols sffx-gl-symbols.txt \
      src/mesa/glapi/glapi/gen/gl_and_es_API.xml > entries.h
    cat > entries.c <<'EOF'
    #include <GL/gl.h>
    #include <GL/glext.h>
    typedef void (*_glapi_proc)(void);
    struct _glapi_table;
    extern struct _glapi_table *_mesa_glapi_get_dispatch(void);
    #define GET_DISPATCH() _mesa_glapi_get_dispatch()
    #define MAPI_TMP_PUBLIC_ENTRIES_NO_HIDDEN
    #include "entries.h"
    EOF
    ${tc}/bin/${triple}-clang -O2 -fPIC -isysroot ${sdkRoot} \
      -Iinclude -Isrc -I${system}/usr/include -c entries.c -o entries.o
    ${tc}/bin/${triple}-clang -dynamiclib -isysroot ${sdkRoot} \
      -fuse-ld=${p.native-ld}/bin/ld -nostdlib entries.o \
      -L${system}/usr/lib -L${mesa}/usr/lib -lgallium-26.3.0-devel -lSystem \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${system}/usr/lib/system/libdyld.dylib \
      -Wl,-platform_version,macos,26.5,26.5 \
      -Wl,-install_name,${prefix}/lib/libGL.1.dylib \
      -Wl,-current_version,4.0.0 -Wl,-compatibility_version,4.0.0 -o libGL.1.dylib
    ${tc}/bin/${triple}-install_name_tool -change \
      /usr/lib/libgallium-26.3.0-devel.dylib \
      /usr/local/lib/mesa-asahi/libgallium-26.3.0-devel.dylib libGL.1.dylib
    python3 ${../m1-mesa-asahi/tools/cs-rehash.py} libGL.1.dylib
  '';
  installPhase = ''
    mkdir -p $out/lib $out/share/licenses
    cp libGL.1.dylib $out/lib/
    ln -s libGL.1.dylib $out/lib/libGL.dylib
    cp -r docs/license* $out/share/licenses/
  '';
  dontFixup = true;
  dontStrip = true;
}
