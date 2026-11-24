#!/bin/bash
# mkp.sh <qc|ak|ws> <patch-file-name> <file...>: diff of runs/sf-term12/<t>/a against <t>/b for those files, as a patch of
# the series (paths from the tree's root), written to integrations/siliconforest/cocoa/patches/<patch-file-name>
cd /home/chrisk/src/darwin-lab/runs/sf-term12; t=$1; out=$2; shift 2
case $t in qc) D=src/Frameworks/QuartzCore;; ak) D=src/Frameworks/AppKit;; ws) D=src/Frameworks/WindowServer;; esac
P=/home/chrisk/src/darwin-lab/integrations/siliconforest/cocoa/patches/$out; : > $P
for f in "$@"; do diff -uN --label a/$D/$f --label b/$D/$f $t/a/$f $t/b/$f >> $P; done; echo "$out $(wc -l < $P)"
