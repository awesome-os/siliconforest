#!/bin/bash
# stage.sh <name> [ak=<appkit-store>] [ws=<windowserver-store>] [qc=<quartzcore-store>] [cv=<corevideo-store>]:
# a prefix /private/var/db/sft12/<name> on the M1 with only the given parts (AppKit.framework with Versions/A and C,
# libWindowServer.dylib, QuartzCore.pd, CoreVideo.framework), hashed there
set -e; cd /home/chrisk/src/darwin-lab/runs/sf-term12; n=$1; shift
chmod -R u+w stage/$n 2>/dev/null || true; rm -rf stage/$n; mkdir -p stage/$n
for kv in "$@"; do k=${kv%%=*}; v=${kv#*=}
 case $k in
 ak) mkdir -p stage/$n/AppKit.framework/Versions/A; A=$v/System/Library/Frameworks/AppKit.framework/Versions/A
     cp -L $A/AppKit stage/$n/AppKit.framework/Versions/A/; cp -rL $A/Resources stage/$n/AppKit.framework/Versions/A/
     ln -s A stage/$n/AppKit.framework/Versions/Current; mkdir -p stage/$n/AppKit.framework/Versions/C
     ln -s ../A/AppKit stage/$n/AppKit.framework/Versions/C/AppKit; ln -s Versions/Current/AppKit stage/$n/AppKit.framework/AppKit
     ln -s Versions/Current/Resources stage/$n/AppKit.framework/Resources;;
 ws) cp -L $v/usr/lib/libWindowServer.dylib stage/$n/;;
 qc) cp -L $(find -L $v -path '*QuartzCore.framework/Versions/A/QuartzCore' -type f | head -1) stage/$n/QuartzCore.pd;;
 cv) mkdir -p stage/$n/CoreVideo.framework/Versions/A; cp -L $(find -L $v -path '*CoreVideo.framework/Versions/A/CoreVideo' -type f | head -1) stage/$n/CoreVideo.framework/Versions/A/
     ln -s A stage/$n/CoreVideo.framework/Versions/Current; ln -s Versions/Current/CoreVideo stage/$n/CoreVideo.framework/CoreVideo;;
 gl) cp -rL $v/Frameworks/OpenGL.framework stage/$n/;;
 esac
done
chmod -R u+w stage/$n
SSH="ssh -o UserKnownHostsFile=$HOME/.ssh/known_hosts_m1_siliconforest -o HostKeyAlias=m1-siliconforest -o BatchMode=yes root@10.1.1.211"
tar -C stage -cf - $n | $SSH "mkdir -p /private/var/db/sft12 && rm -rf /private/var/db/sft12/$n && tar -C /private/var/db/sft12 -xf - && cd /private/var/db/sft12/$n && shasum -a 256 \$(ls AppKit.framework/Versions/A/AppKit libWindowServer.dylib QuartzCore.pd CoreVideo.framework/Versions/A/CoreVideo OpenGL.framework/Versions/A/OpenGL 2>/dev/null) | cut -c1-8,65-"
(cd stage/$n && sha256sum $(ls AppKit.framework/Versions/A/AppKit libWindowServer.dylib QuartzCore.pd CoreVideo.framework/Versions/A/CoreVideo OpenGL.framework/Versions/A/OpenGL 2>/dev/null) | cut -c1-8,65-)
