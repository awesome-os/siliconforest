# SPDX-License-Identifier: MIT
# Keep the current Finch asset implementation; add only Firefox's four missing
# classes from the pinned SiliconForest speech/display source in a private wrapper.
let
 a=import ../cocoa/arm64.nix;
 e=import ../finch/environment.nix;
 base=import ../finch/media/bottle/avfoundation.nix;
 retained=base.overrideAttrs(old:{
  name="sffx-retained-current-avfoundation-arm64";
  buildPhase=builtins.replaceStrings
   ["-install_name /System/Library/Frameworks/AVFoundation.framework/Versions/A/AVFoundation "]
   ["-install_name /System/Library/Frameworks/AVFoundation.framework/Versions/A/AVFoundation.pd "] old.buildPhase;
 });
 headers=import ../finch/media/headers.nix;
 cm=import ../finch/CoreMedia/build.nix;
 cv=import ../finch/CoreVideo/build.nix;
 av=import ../finch/AVFAudio/build.nix;
in e.pkgs.runCommand "sffx-current-avfoundation-extra-classes-arm64" {nativeBuildInputs=[e.pkgs.python3];} ''
 python3 - ${a.src}/src/Frameworks/AVFoundation/AVFoundation.m <<'PY'
 import sys,pathlib
 source=pathlib.Path(sys.argv[1]).read_text()
 speech=source[source.index('// speech:'):source.index('// assets:')]
 display=source[source.index('// shows the newest enqueued frame'):]
 marker='    CGImageRef image = pixels ? createImage(pixels) : NULL;'
 assert display.count(marker)==1
 display=display.replace(marker,"""    // NV12/P010 surfaces are rendered by the existing real QuartzCore YUV path.
     IOSurfaceRef surface = pixels ? CVPixelBufferGetIOSurface(pixels) : NULL;
     OSType type = pixels ? CVPixelBufferGetPixelFormatType(pixels) : 0;
     if (surface && (type == 0x34323076 || type == 0x34323066 || type == 0x78343230 || type == 0x78663230)) {
         [_error release]; _error = nil;
         _status = AVQueuedSampleBufferRenderingStatusRendering;
         [self setContents:(id)surface];
         return;
     }
 """+marker)
 preamble=source.splitlines()[1]+"""
 #import <Foundation/Foundation.h>
 #import <QuartzCore/CALayer.h>
 #include <CoreGraphics/CoreGraphics.h>
 #include <CoreMedia/CoreMedia.h>
 #include <CoreVideo/CoreVideo.h>
 #include <IOSurface/IOSurface.h>
 #include <dispatch/dispatch.h>
 typedef NSString *AVLayerVideoGravity;
 extern NSString *const AVLayerVideoGravityResizeAspect;
 extern NSString *const AVFoundationErrorDomain;
 enum { AVErrorDecoderNotFound = -11833 };
 static NSError *avError(NSInteger code,NSString *reason) {
     return [NSError errorWithDomain:AVFoundationErrorDomain code:code
                            userInfo:@{NSLocalizedFailureReasonErrorKey:reason}];
 }
 """
 pathlib.Path('classes.m').write_text(preamble+speech+display)
 PY
 CF="-isysroot ${e.sdk} -O2 -fblocks -Wno-nullability-completeness -Wno-macro-redefined -I${e.libsystem}/usr/include -I${a.base.libobjc}/usr/include -I${a.base.foundation}/usr/include -I${a.base.corefoundation}/include -I${a.appDeps.coregraphics}/usr/include -I${a.appDeps.onyx2d}/usr/include -I${headers} -I${a.src}/src/Frameworks -I${a.mesa}/usr/include -F${a.openglFramework}/System/Library/Frameworks"
 ${e.cc} $CF -c classes.m -o classes.o
 ${e.cc} $CF -c ${./avfoundation-authorization.m} -o authorization.o
 F=$out/Frameworks/AVFoundation.framework
 mkdir -p $F/Versions/A/Resources/Source
 cp ${retained}/System/Library/Frameworks/AVFoundation.framework/Versions/A/AVFoundation $F/Versions/A/AVFoundation.pd
 ${e.cc} -dynamiclib classes.o authorization.o --ld-path=${e.ld} -nostdlib -isysroot ${e.sdk} \
  -L${e.libsystem}/usr/lib -L${a.base.libobjc}/usr/lib -L${a.base.foundation}/usr/lib -L${a.base.corefoundation}/usr/lib \
  -L${a.appDeps.quartzcore}/usr/lib -L${a.appDeps.coregraphics}/usr/lib \
  -F${cm}/System/Library/Frameworks -F${cv}/System/Library/Frameworks -F${av}/System/Library/Frameworks \
  -Wl,-reexport_library,$F/Versions/A/AVFoundation.pd \
  -Wl,-install_name,/System/Library/Frameworks/AVFoundation.framework/Versions/A/AVFoundation.sfcompat \
  -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${e.libsystem}/usr/lib/system/libdyld.dylib \
  -Wl,-platform_version,macos,26.5,26.5 \
  -framework CoreMedia -framework CoreVideo -lQuartzCore -lCoreGraphics -lFoundation -lCoreFoundation -lobjc -lSystem \
  -o $F/Versions/A/AVFoundation
 cp classes.m $F/Versions/A/Resources/Source/
 cp ${./avfoundation-authorization.m} $F/Versions/A/Resources/Source/
 cp ${a.src}/src/Frameworks/AVFoundation/AVFoundation.m $F/Versions/A/Resources/Source/SiliconForest-AVFoundation.m
 cp -r ${retained}/System/Library/Frameworks/AVFoundation.framework/Versions/A/Resources/Licenses $F/Versions/A/Resources/
 ln -s A $F/Versions/Current;ln -s Versions/Current/AVFoundation $F/AVFoundation
 ln -s Versions/Current/Resources $F/Resources
''
