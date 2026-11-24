# Selected driver porting map

This is an implementation map, not proof that these components work together.
The inventory in `reports/source-inventory.json` covers two **source prefixes**:
`gem.rs` lines 1–134 and `driver.rs` lines 1–39 (actual counts are recorded in the
report). It finds 16 and 14 lexical references respectively. It does not expand
globs, macros, conditional compilation, inferred types or transitive imports.
Use the acquisition tool and scan complete files before treating this as a
complete port inventory.

## Inputs inspected again in this development session

| Input | Exact revision | Evidence |
|---|---|---|
| Omacom Linux Aurora `gem.rs` | `6a08a08e57558083f552733a5276d71b1a4d8ff6` | Connector lines 1–240 read; prefix scanned |
| Omacom Linux Aurora `driver.rs` | same | Connector lines 1–190 read; import prefix scanned |
| Omacom Linux UAPI | same | Connector lines 1–175 read; enum values observed |
| Omacom Mesa UAPI | `a19691114759e2c0507f3502cf6166ef2c6e2e33` | Connector lines 1–110 read; enum values observed |
| PureDarwin `pd_dri_cdev.h` | `e7aa00a60f0b56ef35aa24252e81467787bf71ea` | Full 2,123-byte header; independently matched blob SHA; host probe compiled and run |

The original source branch names and reported full-file blobs remain in the
archived manifest. Prefix captures are not full-file hash verification.

## Adapter obligations, in dependency order

| Existing coupling | Proposed boundary | Necessary test before integration | Status |
|---|---|---|---|
| `kernel::device::{Core,DeviceContext}`, `platform`, `of` | Target identity, device discovery, resource acquisition | Exact board/firmware admission; reject missing resources | Not implemented |
| `kernel::dma::{Device,DmaMask}` | DMA addressability and IOMMU mappings | Invalid range, guard-page and unmap-completion tests | Not implemented |
| `drm::gem::shmem`, `ARef`, `VMapOwned` | Buffer ownership plus CPU/kernel mappings | Owner death cannot free held pages; last mapping controls release | RAM-only XNU probe exercised in ARM64 virt; graphics backend absent |
| `filp->f_mapping` and Linux mapping flags | Native backing-store pin/migration/reclaim rules | Reclaim blocked by real GPU/firmware aliases and in-flight work | IOBufferMemoryDescriptor RAM probe implemented; exported backing remains quarantined |
| `kernel_mappings`, `memacct::Charge` | Alias accounting and memory-pressure policy | Accounting does not prematurely purge live backing | Probe byte/object counters exercised; GPU charges and memory-pressure policy absent |
| `Mutex`, `Arc`, `SpinLock`, `new_mutex` | Synchronization, lifetimes and execution contexts | Lock ordering and interrupt/thread restrictions under concurrency | Threaded host notification tests and serialized XNU probe path; broader CPU/IRQ stress pending |
| `kernel::bindings::*`, `prelude::*`, kernel macros | Explicit dependency inventory or narrow replacement | Expand hidden APIs and macros in selected configuration | Lexical inventory only |
| `declare_drm_ioctls!`, `uapi` | Render-node dispatch and user/kernel copying | Structure layouts, ioctl encodings, sizes, zero-padding, error behavior | Enum/probe tooling implemented; full ABI test absent |
| `PD_DRI` callbacks | Native file/device and mapping lifetime integration | Last-map release callback exactly once, process exit, mmap offsets | CPU map/fork/exit checkpoints and drains observed; final backing reuse remains BLOCKED |
| G17/M3/legacy `ProbeConfig` paths | Separate hardware/firmware backends | One selected board/firmware, never broad generation assumptions | J293/T8103 selected and observed by SSH; proxy, recovery and first-entry qualification pending |

### Critical ownership observation

The inspected `set_mapping_held` logic does not merely increment a generic
reference count. It changes Linux address-space mapping flags and memory charges.
A Darwin implementation must preserve backing lifetime while respecting its own
VM mechanisms, not copy Linux object layouts.

Our C model tests the decision policy around those lifetimes. The separate C++
probe now allocates nonpageable backing and exposes real CPU mappings through
XNU in ARM64 virt. That does not establish cache/TLB finalization, DMA safety or
firmware integration. See `reports/sprint-006/vm-device-runtime.json`.

### The bridge exercised in increment 006

PureDarwin's verified header exposes `pd_dri_ops.mmap_pages` with a release
callback and release argument. The real SDK compiled the page-number width and
architecture cache definitions, and the device exerciser observed CPU aliasing,
file close, fork and child exit through this bridge. The existing callback runs
before complete VM-object finalization; mapped backing remains quarantined. The
observed run does not prove cache/TLB teardown or make it a GPU driver.

## Source references

- https://github.com/omacom/linux-aurora/blob/6a08a08e57558083f552733a5276d71b1a4d8ff6/drivers/gpu/drm/asahi/gem.rs
- https://github.com/omacom/linux-aurora/blob/6a08a08e57558083f552733a5276d71b1a4d8ff6/drivers/gpu/drm/asahi/driver.rs
- https://github.com/omacom/linux-aurora/blob/6a08a08e57558083f552733a5276d71b1a4d8ff6/include/uapi/drm/asahi_drm.h
- https://github.com/omacom/mesa/blob/a19691114759e2c0507f3502cf6166ef2c6e2e33/include/drm-uapi/asahi_drm.h
- https://github.com/PureDarwin/PureDarwin/blob/e7aa00a60f0b56ef35aa24252e81467787bf71ea/src/Kernel/fbsdkpi/pd_dri_cdev.h
