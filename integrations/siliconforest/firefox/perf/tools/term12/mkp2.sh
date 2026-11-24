#!/bin/bash
# mkp2.sh <qc|ak|ws> <from a|m> <to m|b> <patch-file-name> <file...>: diff of runs/sf-term12/<t>/<from> against <t>/<to>
cd /home/chrisk/src/darwin-lab/runs/sf-term12; t=$1; from=$2; to=$3; out=$4; shift 4
case $t in qc) D=src/Frameworks/QuartzCore;; ak) D=src/Frameworks/AppKit;; ws) D=src/Frameworks/WindowServer;; esac
P=/home/chrisk/src/darwin-lab/integrations/siliconforest/cocoa/patches/$out; : > $P
for f in "$@"; do diff -uN --label a/$D/$f --label b/$D/$f $t/$from/$f $t/$to/$f >> $P; done; echo "$out $(wc -l < $P)"
