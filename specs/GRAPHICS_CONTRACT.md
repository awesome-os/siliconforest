# Graphics and platform tests to specify before implementation

Status: test plan only. No ABI probe, driver extraction, or hardware test is implemented by this bundle.

## A. ABI comparison

Acquire complete headers from the exact Omacom kernel and Mesa revisions. Generate independent manifests of each required structure's size/alignment/offsets, command encodings, flags, padding requirements, extensions, handles and error behavior. Repeat in the Darwin build environment. Do not use text equality or identical operation names as the acceptance test.

The previous audit lists GET_PARAMS, GET_TIME, VM_CREATE, VM_DESTROY, VM_BIND, GEM_CREATE, GEM_MMAP_OFFSET, GEM_BIND_OBJECT, GEM_MADVISE, QUEUE_CREATE, QUEUE_DESTROY and SUBMIT as candidates. This is inherited audit information, not a fresh conformance result. Generic DRM buffer and synchronization interfaces also need an inventory.

## B. Memory invariants

A buffer's backing must not be reclaimed, reused, or unmapped while firmware or the GPU may still access it. Define ownership separately for the process, memory manager, DMA/IOMMU mapping, firmware reference, queue, fence and sharing descriptor. Thread cancellation and process exit are not automatically device-idle conditions.

Tests: allocate/map/release; address-space bind/unbind; stale handle rejection; process death with in-flight work; timeout/reset; fence-delayed release; memory pressure; shared descriptors across processes; cache visibility between CPU and device; guard pages and invalid sizes. Native MMU and cache tests require the target architecture; a Python reference model cannot prove them.

## C. Rendering and presentation

First verify deterministic off-screen work and compare readback with an expected result. Next test buffer sharing and synchronization across clients. Only then attempt presentation, output mode changes, input integration, external-display changes and power transitions.

A renderer string or successful initialization ioctl is not acceptance evidence. Rendering into a buffer is not safe scanout. A visible framebuffer is not evidence of GPU acceleration.

## D. Portable protocol tests

From the complete source, identify parsers/state machines with minimal kernel coupling. Record provenance before extraction. Preserve malformed-message, length, correlation, ordering and lifetime cases. Hardware mocks must mark register, interrupt and DMA behaviors as simulated.

Do not rewrite an unknown protocol spec from this document. It intentionally contains no register offsets, firmware structure definitions, command-buffer layout, or XNU VM implementation assumptions.

## E. Porting decision experiment

Compare a small native XNU substrate with a restricted Rust/Linux compatibility approach using the same buffer-lifetime and real submission acceptance tests. Measure the compatibility surface and maintenance implications before committing to a whole-driver architecture.

Reference: archived audit F04-F07. Those are static findings, not runtime validation.
