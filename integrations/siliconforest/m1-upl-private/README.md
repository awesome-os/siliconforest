# Private device-pager pages must use per-page UPL completion

Firefox42 panicked on the M1 during AVD context teardown at vm_upl.c:1360,
`VM_OBJECT_WIRED_PAGE_UPDATE_END`'s overflow assertion. The backtrace includes
`IOGeneralMemoryDescriptor::complete → sf_avd_os_dma_free → session_destroy`.
The saved registers include426 and−426. Exact UART, symbols and failed capsule
remain private under runs/sftb/firefox-install-0042. No decoded-output or
zero-copy success was observed. Qualified devfs-only was restored afterwards.

The frozen XNU source provides a matching counterexample. Device-pager objects
are marked `private` and hold private, noncanonical page descriptors.
`vm_page_wire` and `vm_page_unwire` use canonical-only object/global accounting.
The UPL bulk completion path instead subtracts every final unwired page from
both counters and appends them to pageable queues. A426-page private object
therefore underflows its zero wired-page count. The existing per-page path
uses the correct canonical-only accounting and active/inactive guards.

kernel.patch adds one eligibility condition: private objects cannot use that
bulk path. The existing delayed-work path performs every real page unwire.
No assertion, IOMemoryDescriptor complete, pin release, DMA invalidation or
lifetime requirement is bypassed. Canonical objects retain their fast path.
Existing Apple/XNU source licences remain in the patched files; these build
and host-fixture tools are MIT. manifest.json pins the exact before/after file.

`python3 test-source.py` compiles literal source fragments under ASan/UBSan:
UPL eligibility and its per-page branch, canonical-only count macros, the
unwire accounting prefix, overflow check and queue eligibility guards. Frozen
private426 reproduces the assertion as an expected FAIL. Patched private426,
canonical objects, wire counts1/2/3, INACTIVATE/FREE_ABSENT, vector/purgable and
empty cases pass. The mixed nonprivate control keeps its noncanonical pages
wired (2→1) while unwiring canonical siblings; it is not a claim to fix arbitrary
noncanonical pages in normal objects. Initial host compilation failed on fixture
declarations; corrected actual-source run is retained separately.

These are host accounting/branch tests, not the full VM implementation: locking,
ledgers, object/pager retention, physical pinning and hardware DMA are outside
the fixture. Native descriptor kind and UPL flags at the historical panic were
not captured. A native GPU-BO pin/import/destroy test and the strict frame/PTS/
held-buffer qualifier remain mandatory before shared decoding is qualified.

build-kernel.nix stacks this single-file change on the qualified devfs core and
keeps its original source-hash checks. It only builds locally. Do not install the
failed AVD5238 image alone again. The next controlled kernel must link the new
core with any reintroduced import driver, preserve the reviewed43 wait/HID/sound
inputs, and retain the qualified rollback. No AVE or Thunderbolt trial is added.


pin-probe.c / build-pin-probe.nix prepare a native diagnostic for the FIXED core
plus import driver, never the current cap0 image. It refuses a mismatched
kernelcache ID or missing import capability before opening a GPU node. From an
existing private recording's CREATE it allocates real writeback GEM buffers,
maps each twice at distinct addresses, unmaps one while checking the original,
imports only DATA after a16KiB header, preserves header canaries, then DESTROYs
and verifies the still-owned mapping before munmap/GEM-close. No GPU or decoder
job is submitted. A30s alarm and caller deadline bound the test; synchronous
pre-import/pre-destroy markers help retain evidence if a kernel fault recurs.
The Asahi UAPI header is the exact hash-pinned MIT header from Omacom6a08a08e.
Native helper compilation succeeds; native execution is pending. This provides
a smaller first hardware gate than a full VideoToolbox decode and also answers
the duplicate-map/unmap lifetime question without using an owner profile.


build-kc.nix retains image43's exact immutable AGX/DCP/HID/SPIHID/sound
inputs (speaker extra cap default0), replaces its core with build-kernel.nix,
and reintroduces the reviewed import SDK only alongside that fixed core.
The final verifier requires57 modules, exactly8 changed/49 unchanged against
CIO29, no AVE, and the real ARM64 BL/stub/chained-pointer Force-CMO link check.
The complete core and collection builds succeed. Native instruction inspection
finds the added object-private bit13 test selecting per-page completion, absent
from image43. These recipes do not install or boot anything. Image43 is now
natively qualified with original AVD; the private-UPL/import image is uninstalled.


Native44 qualification, 2026-11-16: three14-buffer no-job rounds pass real
GPU-BO duplicate mapping/alias unmap, DATA import and context destruction;
426-page buffers complete without the old overflow. Strict H264600/HEVC300
full visible NV12 hashes and PTS match independent Linux references; held64,
copied bytes0 and exact900 hardware-job completions pass, with zero errors,
DART faults, remaining contexts/allocations or quarantine. GL/Vulkan10 pass.
The fixed image is retained normal with speaker extra cap0 and qualified43
rollback. This is capture correctness, not a GPU-consumer fence, task-death,
real browser presentation or60fps qualification; those gates remain pending.
Private machine receipts are under runs/sftb/firefox-install-0044, never in git.


task-probe.c / build-task-probe.nix add a separate native lifetime gate,
SDK-built but not yet run. Parent creates real writeback BOs and keeps the
consumer mappings/handles. A new child opens its OWN AVD descriptor, creates
and imports captures using its inherited shared BO mappings, then unmaps all
its GPU VAs and closes its inherited DRM FD while keeping AVD alive. Parent
checks the surviving consumer, kills ONLY that known child, requires reap in2s,
checks context/import availability and the consumer again, then releases its
own mappings/handles. Three rounds, no GPU/decoder jobs and a30s outer alarm.
Missing import capability, wrong kernel ID or a pre-existing decoder context
refuses before allocation. This tests the producer-unmap/death edge separately
from normal destroy; it still does not establish a GPU-consumer fence.


The first native task-probe (v1) failed at its immediate post-exit INFO predicate
after its child was reaped. It did not reach the post-death consumer check.
Fresh later INFO/stats were healthy, contexts/DART allocations0, import still
available, no panic/errors. The exact failed subcondition was not recorded;
its printed errno can be stale because the predicate also checked fields.
The failure remains private under runs/sftb/firefox-task-death-0044.

Frozen SFAppleAVD.cpp documents that a process cannot close the shared node
while another client holds it: reap_contexts runs on CREATE, and last close
retires all contexts. INFO does not reap. Revised task-probe records the exact
post-exit fields, checks its consumer, writes a marker only after the producer
is dead (no jobs), then invokes the REAL CREATE reaper and checks the surviving
consumer again under new allocation pressure. It destroys that new owned
context and requires contexts/DART allocations0. This is explicit lazy retirement,
not an immediate-on-death claim. Revised native execution passed three rounds on qualified44 at2026-11-16
03:18Z. Immediate INFO returned0/contexts1/caps0x10007; the real next CREATE
reaped, while the marked consumer survived retirement/allocation pressure.
Owned child reap was2.53–2.54ms, final contexts/DART bytes/quarantine0 and no
GPU/decoder jobs or fault-counter changes. This establishes the tested lazy
retirement contract; GPU-consumer fences and real-browser presentation remain
separate gates. The first native failure remains a failure.
