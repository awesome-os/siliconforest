#!/bin/sh
# SPDX-License-Identifier: MIT
# Guest side of tools/qemu_ffmpeg_check.sh: decode every clip in /private/var/avd/clips with the
# staged ffmpeg (software decode) and print the frame MD5 list between markers.
PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin; export PATH
printf 'AVDFF_%s\n' BEGIN
/usr/local/bin/ffmpeg -hide_banner -version 2>&1 | head -1 | sed 's/^/AVDFF_VERSION /'
/usr/local/bin/ffmpeg -hide_banner -hwaccels 2>&1 | tr '\n' ' ' | sed 's/^/AVDFF_HWACCELS /'; echo
for c in /private/var/avd/clips/*; do
    n=$(basename "$c")
    start=$(date +%s)
    /usr/local/bin/ffmpeg -v error -nostdin -threads 2 -i "$c" -map 0:v:0 -an -sn -fps_mode passthrough -pix_fmt nv12 \
        -f framemd5 "/private/tmp/$n.framemd5" 2> "/private/tmp/$n.err"
    rc=$?
    printf 'AVDFF_CLIP %s rc=%s seconds=%s\n' "$n" "$rc" "$(( $(date +%s) - start ))"
    sed "s|^|AVDFF_ERR $n |" "/private/tmp/$n.err" | head -20
    grep -v '^#' "/private/tmp/$n.framemd5" | sed "s|^|AVDFF_MD5 $n |"
done
printf 'AVDFF_%s\n' END
