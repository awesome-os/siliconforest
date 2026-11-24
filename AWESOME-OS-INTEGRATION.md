# Awesome-OS Integration — `awesome-os/siliconforest`

> **Upstream Origin:** [`chriskearney/siliconforest`](https://github.com/chriskearney/siliconforest) (MIT License, Copyright © 2026 Chris Kearney and the SiliconForest contributors; ported/adopted components retain their original licenses in [`LICENSE`](LICENSE), [`LICENSING.md`](LICENSING.md), [`LICENSES/`](LICENSES/), and [`NOTICE`](NOTICE)).

`awesome-os/siliconforest` preserves Chris Kearney's native Apple Silicon Darwin/XNU bring-up tree (`MacBookPro17,1` / T8103 M1, Asahi Linux-compat kexts, Rust GPU driver, DCP display coprocessor, Mesa `asahi`/Honeykrisp, Finch frameworks, and Chonkstep Wayland desktop) intact while adding a **C11 `B8G_ABI_MAGIC_V3` (`0x33564742`) + 64-byte aligned `B8G_ARENA_V1`** bridge header ([`include/b8g_siliconforest_abi.h`](include/b8g_siliconforest_abi.h)) for the Awesome-OS 4-layer stack.

---

## Pairing Matrix

| Awesome-OS Repository | Role | How `awesome-os/siliconforest` Pairs With It |
| :--- | :--- | :--- |
| **`awesome-os/t2xnu`** (`T2macOS`) | Open Darwin / XNU kernel & Apple T2 / Apple Silicon platform layer | Shares Darwin XNU kernel bring-up patterns, IOKit families, LinuxKPI compatibility shims, and `B8G_ABI_MAGIC_V3` kext descriptors (`include/b8g_siliconforest_abi.h`) so Apple Silicon M-series and T2 Darwin subsystems speak a single zero-copy 64-byte aligned `B8G_ARENA_V1` ABI. |
| **`awesome-os/t2linux`** (`ARM64`) | Linux kernel & driver distribution for Apple T2 and Apple Silicon (ARM64) | Aligns Asahi/Aurora Linux-derived drivers (AGX GPU, DCP, DART, NVMe, BCM4378 Wi-Fi, speakersafety, video decode) with `t2linux` ARM64 device trees and firmware extraction contracts, allowing dual-boot or co-resident Darwin + Linux partitions orchestrated via `awesome-os/siliconforest-m1n1`. |
| **`awesome-os/b8g-ghost` `@ cas-v1`** | Content-addressed `.b8p` CAS image & warm-RAM checkpointing store | Indexes SiliconForest kernel collections, Mesa/Chonkstep rootfs layers, and 64-byte aligned `B8G_ARENA_V1` warm-RAM checkpoints by content hash (`cas-v1`), enabling $O(1)$ rollback and reproducible artifact distribution without vendoring proprietary Apple binaries. |
| **`awesome-os/siliconforest-m1n1`** | Stage-1 / Stage-2 Apple Silicon bootloader (`m1n1`) | Hands off ADT/FDT, framebuffer, and `B8G_ARENA_V1` memory regions to SiliconForest XNU or `t2linux` ARM64 via `B8G_ABI_MAGIC_V3` (`0x33564742`). |

---

## C11 ABI Bridge (`include/b8g_siliconforest_abi.h`)

The header [`include/b8g_siliconforest_abi.h`](include/b8g_siliconforest_abi.h) provides:

- `B8G_ABI_MAGIC_V3` (`0x33564742u`) and `B8G_ARENA_V1_ALIGN` (`64u`) compile-time invariants verified with C11 `_Static_assert`.
- `B8gArenaV1Header` (64-byte aligned) for zero-copy ring/framebuffer/CAS-v1 memory exchange between XNU kexts, `t2linux`, and `b8g-ghost`.
- `B8gSiliconForestBridgeDesc` (64-byte aligned) exposing XNU/LinuxKPI/GPU/DCP/NVMe/Wi-Fi/Audio subsystem handles to `b8g-abi`.

### Local Verification

```sh
gcc -std=c11 -Wall -Wextra -Werror -fsyntax-only include/b8g_siliconforest_abi.h
```
