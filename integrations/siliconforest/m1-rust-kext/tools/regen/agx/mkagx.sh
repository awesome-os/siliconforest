# regenerates agx/patches from src.orig (pristine drivers/gpu/drm/asahi) vs src (edited)
set -e
cd "$(dirname "$0")"; OUT=$1
rm -rf pa pb; mkdir -p pa pb
d() { for f in "$@"; do mkdir -p pa/a/$(dirname $f) pa/b/$(dirname $f); cp src.orig/$f pa/a/$f; cp src/$f pa/b/$f; done
      (cd pa && diff -ruN a b | sed -E 's/^(--- a\/[^\t]*)\t.*/\1/; s/^(\+\+\+ b\/[^\t]*)\t.*/\1/' | grep -v '^diff -ruN'); rm -rf pa/a pa/b; }
{ cat <<'M'
drm/asahi: a build with the common code and the G13/G14 (legacy) backend only

--cfg sf_g13 leaves out the G17 and M3 backends (61,000 of the driver's 111,000 lines) and
the hw tables that exist only for them. Without the cfg nothing changes. The common code
names a few G17 items unconditionally (Vm status, cumulative sync bundles, `is::<g17::Gpu>()`
tests, two t8140 constants); for those the build keeps the real g17/status.rs, hw/t8140.rs
minus its G17 configuration, and two types that cannot be constructed (`g17::Gpu`,
`g17::sync_output::SignalBundle`), so every G17 branch of the common code is dead but
type-checks. None of the driver's C (sysfs.c, g17/*.c) is needed by this build.
UPSTREAM: optional (a Kconfig symbol per backend would be the Linux form).

M
d asahi_core.rs hw/mod.rs hw/t8140.rs driver.rs mmu/shared.rs regs.rs; } > $OUT/0001-g13-only-build.patch
{ cat <<'M'
drm/asahi: SiliconForest's Linux layer

- gem.rs: no address_space behind a shmem GEM object here (m1-drm72 README assigns this
  branch to the driver port); the held/charged accounting is kept.
- mmu.rs: the firmware's `pagetables` region has no struct page on this kernel (memory the
  boot loader reserved is outside xnu's managed memory), so every GPU reaches it through
  ReservedTables, the mapping the driver already has for no-map regions.
- stats_raw.rs: core::ffi::c_char is i8 on Apple targets and kernel::ffi::c_char is u8;
  one pointer cast that is a no-op on Linux.
UPSTREAM: the cast could go to the driver; the rest is SiliconForest only (cfg sf_lkpi).

M
d gem.rs mmu.rs stats_raw.rs; } > $OUT/0002-siliconforest-layer.patch
for p in $OUT/*.patch; do echo "$(basename $p): +$(grep -c '^+[^+]' $p) -$(grep -c '^-[^-]' $p)"; done
# 0003: temporary diagnostics, src (after 0001+0002) vs src3
{ cat <<'M'
drm/asahi: TEMPORARY diagnostics on the kernel-mapping path ("SFAgx: diag:")

Boot ag02 failed with ENOMEM at the first HeapAllocator::add_block mapping. One line per
step that can fail between the GEM object and the page-table commit, the DMA segments of the
first four mappings, and an error instead of a silent short mapping when the sg table covers
less than the mapping; the first firmware messages, RTKit boot, and the first channel
command the firmware consumes (or a line for each one it does not). Remove when the Mesa
test passes.
UPSTREAM: no.

M
rm -rf pa; mkdir -p pa/a pa/b; for f in gem.rs mmu.rs gpu.rs channel.rs; do cp src/$f pa/a/$f; cp src3/$f pa/b/$f; done
(cd pa && diff -ruN a b | sed -E 's/^(--- a\/[^\t]*)\t.*/\1/; s/^(\+\+\+ b\/[^\t]*)\t.*/\1/' | grep -v '^diff -ruN'); rm -rf pa; } > $OUT/0003-temporary-diagnostics.patch
echo "0003-temporary-diagnostics.patch: +$(grep -c '^+[^+]' $OUT/0003-temporary-diagnostics.patch) -$(grep -c '^-[^-]' $OUT/0003-temporary-diagnostics.patch)"
