# Bounded player controller

`SFPlayerController.m` adapts our existing real AVPlayer's current item,
duration, tracks, output level, rate and transport operations. It retains the
player, reports missing/failed media without playable capability, and forwards
play, pause, toggle and finite bounded seeks. The backend's actual rate-one
audio limitation remains in force; setting a property does not add resampling,
reverse playback or streaming support. Callers serialize access on the player
run-loop thread.

This is a subset of the native private class. It does not implement its private
UI, inspection, trimming/end-time configuration, timeline, chapters, timecodes,
resource management or the computed-property KVO notification families. In
particular, property getters read through the current player; there is no claim
that native controls can bind to every property and receive native-equivalent
notifications. The QuickTime controls/controller family remains incomplete.

Our Linux-built native metadata probe established NSObject inheritance and
typed scalar/object/action methods. The native value probes create only the
controller and empty player; an optional extension reads our own generated
local movie while paused. They never start native playback, open a view or
change a service. M5 uses fresh SHA-verified temporary directories, a30-second
bound, no DYLD overrides/build/install and deletes the directory afterward.
Only our programs' stdout/stderr are returned.

Native observations distinguish controller-owned mute from the player's
volume/default rate. Setting mute without a player persists after assignment;
external changes to the player's mute do not change the controller's mute.
Volume/default rate read through the attached player, and setters without a
player have no later effect. With no item, time/duration are NaN, not a
fabricated zero-length playable item. The portable v2 idle probe SHA is
`93c7dba3babecb0e2631df0a2ad1be545f0f6a3c70412079d52e9606d413c081`.

The native paused-file extension shows why metadata snapshots do not prove
transport readiness: hasContent becomes true before duration and video track
inspection finish. A subsequent observation has duration5/video enabled but
current time unavailable and play/seek false. This asynchronous private
inspection state is not represented as parity with our bounded eager backend.
These preparation snapshots do not establish muted-track semantics. Raw snapshots
remain under `runs/sffinch/m5-avkit-controller-values-v{3,4}-reference.json`.

The separate `time-resolver-probe` observes only scalar state without a player
controller. Its native defaults are interval1 and other scalar values0;
changing interval/resolution recomputes bounds. These facts alone are not an
implementation of the resolver's controller linkage, target seeking or KVO.
No AVPlayerControllerTimeResolver class is supplied merely to fill its name.
The later distinct-setter and attached-controller observations are recorded in
[the resolver evidence](TIME-RESOLVER-2026-11-15.md); immediate cached time and
completed asynchronous seek are measured separately.

The final private provider builds with77 exports, retaining all73 previous
exports. Target qualification is recorded separately after the staged M1 test;
a Linux build/native idle observation alone is not hardware playback evidence.
No canonical framework is replaced by this work.


The v5 native reference waits for actual controller readiness. It then reports
current time0, duration5, play/pause/seek capabilities true, audio/video enabled
and controller status2, even though playback is paused and mute remains true.
This corrected the draft's assumptions that canPause meant currently playing,
that mute disabled the audio track, and that controller status equaled the
AVPlayerItem status. The old queued M1 stage-v6 was stopped before it acquired
a child or executed; its no-execution receipt is retained. The source now uses
the measured ready-state contract. The private controller's inspection phase
still remains outside the native-parity claim.


The final v6 Linux-built reference probe SHA is
`14f784bba8c5ccecd44e32c0bf3528b3c1f2d48949735b286bdb739c215c7806`.
All three M5 runs completed normally: no-item values, a ready paused local
movie, and a missing file. Ready status2 and failure status3 are distinct from
AVPlayerItem's status enum. The missing file keeps hasContent true but returns
NaN times, play/pause/seek false and a nonnil error. These facts define the
source's failure behavior without inventing successful playback. Raw results
are in `runs/sffinch/m5-avkit-controller-values-v6-reference.json`.


## Actual M1 evidence and retained frame failure

At21:28Z, `avkit-controller-values-stage-v1` matched **all raw stdout rows** of
all three v6 native cases, with identical Linux-built probe hashes and exit0.
That covers no item, fully ready paused file and failed file states; there are
no masks or ignored rows. The private image retains all73 prior exports and
adds the controller class/metaclass and two own ivars (77 total). Eleven of
QuickTime's18 reviewed imports resolve: nine implemented, two capture-view
symbols explicitly unavailable. The rest of the private UI family is absent.

The separate `avkit-real-player-stage-v7` exercises the controller over the real
backend: actual track/duration getters, play, pause, seek0.2s, toggle, end and
detach assertions pass. Audio returns230400 post-seek frames at48kHz with
225 callbacks/error0; actual CPU video/control drawing changes27840 central
pixels. No window, panel, input or acoustic result is claimed.

**The strict playback result is FAIL.** Of150 decoded frames, frame40 at
1.333333333s has MD5 `b719d24f8a7dfab91d4a25f5f837b50a` instead of independent
Linux reference `bd7102cf4b354078f7385e93637693fd`; the other149 hashes and
all timestamps match. The generated movie bytes match the independent fixture.
These hashes are captured during preparation, before controller transport
operations. Global AVD counters show two other contexts before/after,454 jobs,
455 completions and one new stale completion, with no error/timeout/DART fault.
They neither isolate our150 jobs nor attribute the stale event to our process.
A completion/copy/concurrency issue remains under investigation; this is not
an acceptable arithmetic tolerance. The original strict classifier is retained
unchanged and fails. The earlier quiet v3's150-frame pass remains separate,
not a substitute for this new failure.

Raw results, strict failure and frame differences are in the two named private
run directories. Kernel-owner/FX observations were filed without requesting a
kernel change or boot slot. No canonical provider was replaced.

The raw-byte follow-up `avd-capture-bytes-v1` ran22:07:48–56Z. A software
control and three hardware runs produced150 frames each: all600 NV12 frames
match the independent Linux bytes exactly. Each hardware run reports150
hardware pictures. The probe preserves51.84MB of raw output and per-frame
hashes, and records byte offsets, luma counts and error magnitudes for any
future mismatch. It does not overwrite the earlier failure.

Two other AVD contexts remain present, with global interleaving even during
the software control. There are no new errors/timeouts/stale/DART events, but
every independent kernel-isolation gate is false. Thus frame40 was not
reproduced and remains open. The source-only IRQ ownership fix
`41b6f2d34` is **not installed** and has no hardware-fix qualification; see
[the IRQ analysis](../../m1-avd/IRQ-OWNERSHIP-2026-11-15.md).
