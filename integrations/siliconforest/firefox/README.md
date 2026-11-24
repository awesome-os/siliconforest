# Firefox integration and verification

## Network notification backend and quit-crash qualification

`sc-network.nix` builds a private, complete SystemConfiguration framework from
upstream source. It preserves the existing address-based route lookup and adds
reachability callbacks on the caller's CF run loop. `sc-route-source.c` blocks
in kqueue on real PF_ROUTE messages and resolver-file/directory vnode events;
it does not poll or modify network configuration. Atomic resolver replacement
reopens the file watcher. Source destruction joins its worker and releases its
socket/context/descriptor resources. Scheduled reachability targets must be
unscheduled before their final caller release.

When configd is unavailable, callback-bearing dynamic-store sessions can use
this read-only network-notification backend. The session retains the canonical
SCDynamicStore CF type; supported network key patterns receive invalidations
from actual interface/route and resolver events. General store reads/writes
remain unsupported without configd. No EAPOL state is invented. This addresses
Firefox's network initializer returning early and subsequently removing and
releasing its uninitialized run loop during shutdown.

`sc-route-probe.nix` checks file rewrite/replacement notification and 65 complete
source lifetimes. `sc-network-probe.nix` checks 65 API lifetimes, actual callback
delivery, balanced context ownership, descriptor stability, idle CPU and
unsupported-operation failures. These native controls alone do not prove
Firefox can quit: require three actual browser natural quits, no family left
alive or cleanup signals, and UUID-matched private framework mappings before
`install-network-runtime.py` can select it. The installer plans by default,
requires cold original build bytes, preserves the launcher/manifest for checked
rollback, and leaves owner processes and profiles alone. Reporting remains
disabled until the crashreporter UI itself is qualified.

After a verifier failure has triggered rollback, `--retry` can reselect the same
qualified image. It checks both prior text hashes, the original cold payload,
and the executed image's load-command UUID. It preserves the original backup
and never treats lowered executed Mach-O bytes as a new pristine image. A
changed launcher or runtime manifest prevents both retry and rollback.

## Temporary QA data

Native QA directories can contain large renderer captures and fresh browser
caches. `retire-owned-run.py --run runs/firefox/<run>` verifies the collected
archive by reading every file and plans retirement of that exact remote run.
With `--apply`, use the shared GUI or core lock: it checks coordination again,
refuses live process and installed-runtime references, and records before/after
free space. It never removes a tool or provider prefix. The workstation archive
and extracted evidence remain available; executed image bytes in an archive
remain diagnostic evidence, not a pristine build source.

## Installed page and window gates

Run browser diagnostics through `launch.py --installed-bundle --bundle
/Applications/Firefox.app` with a fresh owned profile, the coordination gate and
shared GUI lock. All synthetic input uses the virtual seat and an explicitly
admitted owned window. Agent launches disable crash reporting.

`--functional-qa --presentation-seat --site-legibility` collects actual renderer
visible, text-hidden and restored captures. The checker includes supported
iframe documents, suppresses CSS transitions throughout the paint control,
and requires ink in every eligible visible text run plus faithful restoration.
`--paired-site-legibility` repeats with web fonts on/off in the same owned
profile, preserving cookies and restoring the original preference even on a
measurement failure. Compare its `legibility-on` and `legibility-off` outputs
with `compare-legibility.py --on-prefix legibility-on --off-prefix legibility-off`.
Inspect the actual renderer pictures, final URLs and requested-site coverage:
a CAPTCHA, interstitial, empty page or duplicate tab never qualifies ordinary
site legibility. Pixel differences alone cannot establish correct glyphs/fonts.

`lifecycle-run.py` pairs the owned audio fixture with per-process callbacks,
content-process identities and compositor window observations before cleanup.
`analyze-lifecycle.py` requires media teardown after close, no invisible live
window after close, and an actual compositor reopen/Quit route. Hidden and
minimised windows must retain their media and reopen the same tab. The
`--quit-menu-control` diagnostic can isolate an application termination-routing
failure, but cannot qualify desktop Quit. Cleanup signals never prove natural
shutdown, and callback silence does not establish acoustic output.

## Shared decoder video selection

`install-video-runtime.py` selects the complete qualified CoreVideo/VideoToolbox
family, preserving system graphics, the network fix and corrected audio clock.
Its dry plan checks the retained matched playback evidence, original cold-file
ledger, reviewed renderer pictures and loaded-image UUIDs. Apply only under the
shared GUI/core/bench locks after notice; follow immediately with the media
owner's `run-installed-video.py` acceptance packet and inspect both renderer
pictures. Failed or incomplete acceptance requires checked rollback. The
installed gate requires complete 600/300-frame playback, hardware jobs, pixels,
provider paths, natural status0 and no surviving browser family before cleanup.
Decoder pool retirement remains disabled. These gates do not establish panel
pacing, acoustic synchronisation or smooth playback while interacting.

The optional CGL build flag `nv12GPUCopy=true` requires `nv12Backing=true`
and adds the media owner's strict GPU plane-copy API to this recipe. Its
default is off; it does not enable the image cache, damage path or frame
observer. `CGLPDCopyNV12ToTextures2D(surface, textures)` fills two independently
allocated R8/RG8 textures while holding one global IOSurface use across both
completed GPU blits. The caller must also keep the input frame and global use
alive across its complete attempt and fallback. A false return requires CPU
fallback to overwrite both planes. Preserve the existing color matrix, range,
orientation and texture sampling. Count actual CPU fallback bytes; helper
statistics count planes, not video frames. Its conservative completion waits
remain. This is GPU copying rather than a direct decoder-surface alias, and
offscreen native controls do not qualify browser integration or performance.

`install-video-clock.py` advances that family to the display-phase clock from
TERM's 0172 recipe while requiring the same qualified VideoToolbox UUIDs.
The cold payload comes from Finch's `firefox-media/clocked-providers.nix`, which
imports TERM's CoreVideo recipe. The selector changes only `video_override`
and retains the previous family and manifest for checked rollback. Repeat the
installed media acceptance immediately; a successful build or display-link
tick count alone does not qualify Firefox presentation.

`presentation-series.py --root /private/var/db/sffx/<owned-prefix> --round N`
measures ten active seconds each of long scrolling, CSS animation and H264
1080p60 through the installed outer launcher. Stage its sibling Python modules,
`presentation-series.html` and a fifteen-second `frame-id-1080p60.mp4` fixture
first. Run rounds 1–12 with GUI/core-shared/bench locks, releasing between rounds.
One own profile is reused only after natural status 0 and full family exit.
The initial targeted virtual modifier tap wakes the display before timing;
there is no pointer/key injection during the measured phases. Background
services retain their ordinary preferences. The runner checks hardware
WebRender, actual system graphics and the selected clock in parent and GPU.

Use `presentation-report.py <collected-directory>` on the workstation. It sums
per-window shown counts over their reported intervals, lists replaced commits,
retains separate histogram percentiles and derives matched-process CPU deltas.
It does not average percentiles or treat browser rAF/decoded-frame counts as
presentation. Stats commands and synchronous log output may perturb the
measurement; preserve that limitation when reporting long intervals or rates.
Inspect all three first-round renderer captures. Keep failed runs, natural-exit
failures and below-threshold rates; they cannot be replaced by cleanup success
or a selected subset of the twelve rounds.

For a steady-state control, `--settle-seconds 60 --automatic-intervals` keeps
normal services enabled while the process settles, then observes 22 seconds
per workload without stats/dump/input requests. It discards the first automatic
compositor interval, which can overlap warmup, and takes the last complete
interval by a rule fixed before collection. All intervals remain in the raw
receipt. Scrolling continues for 60 seconds and the video loops. CPU deltas
cover the full observation; presentation covers the selected interval. Report
these separately from startup results. A successful warm control does not
erase startup stalls or qualify twelve repetitions.

The optional `av1_enabled` runtime policy has a strict `SFFX_AV1=0|1` switch. On this M1, disabling AV1 lets sites negotiate hardware VP9/H264/HEVC instead of software AV1; it does not request a lower resolution or frame rate. Explicit `media.av1.enabled` entries outside the launcher's marked `user.js` block win. `SFFX_AV1=1` enables software AV1 when needed for an AV1-only source. Removing the manifest policy removes only that managed block on the next launch. `install-codec-policy.py` admits the matched native comparison and inspected renderer pictures, keeps a checked rollback, and requires an immediate installed-launcher acceptance without a test preference overriding the default. The measured YouTube pair is one clip, not a universal codec quality or panel cadence claim.
