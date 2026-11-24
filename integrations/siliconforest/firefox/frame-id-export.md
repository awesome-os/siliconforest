# Private exported-frame diagnostic

Build `cgl-gpu.nix` with `frameIdentity=true` and the same prefix, canonical,
NV12 and cache options as the admitted CGL cohort. The default derivation does
not include this code. This is diagnostic CPU readback, not an optimization.

Use only a fresh owned Firefox instance and the media agent's committed
900-frame fixture (`bc253d91f`). `SFFX_FRAME_ID_DIMENSIONS=1` alone reports up to
eight distinct export size/flip combinations, with no pixel readback or guessed
rectangle. First collect its paused frame-zero actual GPU
image, DOM/video rectangle, DPR and export dimensions. Admit the marker geometry
and orientation against that image before the next private run. No coordinates
are inferred from a title or a different browser instance.

`SFFX_FRAME_ID_GEOMETRY=frameW,frameH,videoX,videoY,videoW,videoH,direction`
specifies the video rectangle in the exported GL framebuffer. Direction `1`
places the marker's top near its low Y edge; `-1` places it near its high Y edge.
The whole rectangle must fit and dimensions must match the current context.
Reject an invalid or nonzero frame-zero calibration; do not repair it by
discarding arbitrary timed samples.

After the existing export completion, two one-pixel RGBA rows are read. The
helper restores the read framebuffer, its read buffer, pack-buffer binding,
alignment, row length and pixel/row skips. It emits `FXFRAMEID` with PID,
sequence, export IOSurface ID, dimensions, framebuffer, monotonic timestamps,
bytes, decoded identity, GL error and 36 raw marker levels. Each complete record
uses one bounded write; `previous_emit_ms` records the previous write's cost
(the final write's cost is not included). Invalid guards,
complements or levels yield identity `-1`. Collection stops at 1,200 samples or
35 seconds after the first matching export.

Record readback duration and compare with an uninstrumented control. These
identities establish completed exported contents only. Distinct completed
panel flips require the compositor's source-buffer and flip-time correlation;
neither JS callbacks nor this diagnostic alone prove that result.
