# Regenerates lkpi/patches/ (the series against Linux's rust/): pristine rust/, the mechanical
# gates (lkpi/tools/gate.py, gate_errnos.py), then one patch per p<N>.py, each of which edits
# the tree under ./work with sfpatch.py. To change a patch, edit its p<N>.py and run this.
#   mkpatches.sh <workdir>
set -e
here=$(cd "$(dirname "$0")" && pwd); top=$(cd "$here/../../.." && pwd)
export PATH=$HOME/.nix-profile/bin:$PATH
mkdir -p "${1:?workdir}"; cd "$1"; cp "$here"/sfpatch.py "$here"/p[1-6].py .
D=$top/lkpi
LX=$(cd "$top/../../.." && pwd)/sources/gpu-plan/linux-aurora
BG=$(nix-build --extra-experimental-features nix-command --extra-experimental-features flakes "$D" -A bindingsGen --no-out-link)
rm -rf stage && mkdir stage && cp -r $LX/rust stage/a && chmod -R u+w stage/a
python3 $D/tools/gate.py stage/a/kernel > /dev/null
python3 $D/tools/gate_errnos.py stage/a/kernel/error.rs $BG/objtree/rust/bindings/bindings_generated.rs > /dev/null
mkdir -p $D/patches; rm -f $D/patches/*.patch
i=0
while read -r script name; do
  i=$((i+1)); rm -rf stage/b; cp -r stage/a stage/b
  rm -rf work; mv stage/b work; python3 $script; mv work stage/b
  f=$D/patches/$(printf %04d $i)-$name.patch
  { sed -n '/^"""/,/^"""/p' $script | sed '1d;$d'; echo; (cd stage && diff -ruN a b | grep -v '^diff -ruN' | sed -E 's/^(---|\+\+\+) ([ab]\/[^\t]*)\t.*/\1 \2/') ; } > $f || true
  rm -rf stage/a; mv stage/b stage/a
done <<LIST
p1.py core-tier-without-mm-pidns-cred-fs
p2.py time-without-hrtimer
p3.py workqueue-on-linuxkpi
p4.py c-string-literals-as-char-ptr
p5.py io-irq-uaccess-without-device
p6.py dma-without-debugfs
LIST
rm -rf work; cp -r stage/a work
for f in $D/patches/*.patch; do echo "$(basename $f): +$(grep -c '^+[^+]' $f) -$(grep -c '^-[^-]' $f)"; done
