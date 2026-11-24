# SF-AH: layers shown in another process's window (CAContext / CALayerHost for Chromium)

Design note (TERM7, 15 November 2026). Nothing here is implemented or observed yet.

## What Chromium needs
Chromium's GPU process draws a tree of CALayers whose contents are IOSurfaces and hands it to the browser
process through `ui/base/cocoa/remote_layer_api.h` (research: `runs/media-gpu-research/REPORT.md`, section 5):
- `CGSMainConnectionID()` (CoreGraphics, private): the process's connection to the window server, non-zero when
  remote layers can be used.
- `+[CAContext contextWithCGSConnection:options:]` in the GPU process; the context's `layer` is set to the tree's root;
  its `contextId` (a 32-bit id) goes to the browser process over Chromium's own IPC.
- `CALayerHost` in the browser process: a layer whose `contextId` names the other process's context; the browser puts
  it in its view's layer tree and the GPU process's frames appear there.
- Fence ports (`-createFencePort`, `-setFencePort:`): a mach port the browser makes when it resizes; the GPU process
  holds its next commit to that port, so the resize and the new frame arrive together.

## What exists
`QuartzCore/CAContext.m` (pinned PureDarwin) already shows a layer of one process in another's window, for one kind
of layer: a context whose layer is a `CAMetalLayer` exports that layer's Wayland surface through the window server
(`_windowServerExportSurface` -> id); a `CALayerHost` with that id imports it as a subsurface of the host view's window
(`_pdAttach`, placed where the host layer sits). There is no `contextWithCGSConnection:`, no fence port, no
`CGSMainConnectionID`, and a context whose layer is an ordinary CALayer tree (Chromium's case) gets id 0.

## Design (smallest working version)
1. CoreGraphics: `CGSMainConnectionID()` answers a non-zero id per process once the window server connection exists
   (the WindowServer client's connection, `_windowServerConnectionID` or the process id when there is no number of its
   own); `CGSDefaultConnectionForThread` the same.
2. QuartzCore, the exporting side: `+contextWithCGSConnection:options:` = `+remoteContextWithOptions:`. A context whose
   layer is not a CAMetalLayer gets a surface of its own from the window server (a Wayland surface not shown by itself,
   exported as now). At each CATransaction commit that changed the tree, the tree is drawn into a buffer of that
   surface at the layer's pixel size (CPU: the same drawing AppKit uses without OpenGL, `pdDrawLayerCPU`, moved to
   QuartzCore so both use one implementation; GPU: CALayerContext rendering into a dma-buf, when OpenGL is available)
   and committed. IOSurface contents are drawn from their mapped pixels on the CPU path; on the GPU path a GPU-backed
   IOSurface (CODEX-FX's opt-in dma-buf storage) is sampled directly.
3. The hosting side: `CALayerHost` unchanged (it already shows an exported surface as a subsurface, at its layer's
   place, clipped by the window).
4. Fence ports: `-createFencePort` makes a receive right and answers a send right; the context that is given it with
   `-setFencePort:` does not commit until the port's last send right is gone (dead-name notification) or 1 s passed
   (a frame is never held longer: a fence whose holder died must not freeze the page).
5. Compositor request (DESK6; Chonkstep is not ours to change): a subsurface fed by another client's buffers needs the
   compositor to accept an exported surface id from a second client and keep the stacking/position relative to the
   host window while the two clients commit independently; synchronised commits (`wl_subsurface.set_sync`) for the
   fence case. To be written in `runs/m1-coord/requests-for-DESK6.md` with the exact protocol once step 2 runs.

## Test (two processes, before Chromium)
`probe/sf-remote-layer-probe`: process A makes a context with a CALayer tree (a solid colour that changes each
second and an IOSurface-backed sublayer) and prints its `contextId`; process B (a window) puts a `CALayerHost` with
that id in its view. Pass: B's window picture (`PD_WS_DUMP_DIR` does not include subsurfaces: a screen capture of B's
window, my own) shows A's colours changing, at the host layer's place, and B's resize with a fence shows no frame of
the old size after the new one. Fail cases kept: A exits (B shows nothing, no crash), id 0, B before A.
