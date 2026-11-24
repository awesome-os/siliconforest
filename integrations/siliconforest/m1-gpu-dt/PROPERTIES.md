# Device-tree properties the T8103 (M1, G13G) GPU driver reads

Scope: the Rust Asahi driver in `sources/gpu-plan/linux-aurora/drivers/gpu/drm/asahi`,
common code plus `hw/t8103.rs` (`g15/`, `g17/`, `m3/` excluded). For T8103 the
hardware config has `num_dies = 1`, `max_num_clusters = 1`, `has_csafr = false`
and the default UAT quirks, so the CS/AFR properties and the `l2-pagetables`
region are never looked up.

Columns:

- **Shape**: what the driver's accessor demands. `u32` and `F32` are one
  big-endian cell (`F32` is the IEEE-754 bit pattern); `u64` is two cells.
- **Linux source**: `DTS` = static value in `t8103.dtsi`; `m1n1` = written at
  boot by m1n1 `dt_set_gpu()`; `default` = absent from the tree, the driver
  falls back to the value shown.
- **This M1**: what `private/linux-dt/gpu-nodes.txt` and `gpu-child-nodes.txt`
  show (length in bytes and the cells in hex).
- **Darwin**: how `src/gpudt.c` provides it. "static" means the same constant
  as the DTS; "(= ADT `x`)" means the library also compares it with that ADT
  property of `/arm-io/sgx` and reports a mismatch. On this machine all 24 such
  comparisons are equal.

All ADT property names are under `/arm-io/sgx` unless a path is given.

## Counts

64 distinct property names are read on this path (the three per-node OPP
properties and the region `reg` counted once each):

| Where Linux gets it | Names |
|---|---|
| Static device tree source | 32 |
| m1n1, computed from the ADT (two of them also need one hardware register) | 8 |
| Absent on Linux, driver default | 21 |
| m1n1 boot-time allocation (debug only) | 3 |

## Node `gpu@206400000`: matching and registers

| Property | Shape | Linux source | This M1 | Darwin |
|---|---|---|---|---|
| `compatible` | string list, matched against `apple,agx-t8103` | DTS: `apple,agx-t8103`, `apple,agx-g13g` | 31 bytes | static |
| `reg` | 2 tuples of (2-cell address, 2-cell size) | DTS: `0x206400000`/`0x40000`, `0x204000000`/`0x1000000` | 32 bytes, same | static; bases compared with ADT `/arm-io/gfx-asc` `reg[0]` and `/arm-io/sgx` `reg[0]` through `/arm-io` `ranges` (equal). Sizes are Linux's choice and smaller than the ADT windows. |
| `reg-names` | string list; driver asks for `asc` and `sgx` by name | DTS | 8 bytes | static |

## Firmware version

| Property | Shape | Linux source | This M1 | Darwin |
|---|---|---|---|---|
| `apple,firmware-compat` | `[u32; 3]`, required; G13 accepts only `12 3 0` and `13 5 0` | m1n1: ADT `/chosen` `firmware-version` (an iBoot version string) looked up in m1n1's table, then 12.3.1 -> 12.3, 13.5 beta4 and 13.6.1 -> 13.5, anything else unchanged | 12 bytes: `d 5 0` | same lookup, table generated from m1n1 `firmware.c` (183 entries). Unknown string -> `0 0 0` plus a diagnostic; the driver then refuses to probe. |
| `apple,firmware-version` | `[u32; 3]`, optional, default `0 0 0` | m1n1: the looked-up version itself (3 or 4 cells) | 12 bytes: `d 5 0` | same |
| `apple,firmware-abi` | not read by this driver | m1n1: same tuple as `-compat` (DTS has `0 0 0`) | 12 bytes: `d 5 0` | produced for parity |

## Power and performance controller, present in the tree

All are single cells on `gpu@206400000`. "req" = the driver fails without it.

| Property | Shape | Linux source (value) | This M1 (hex) | Darwin |
|---|---|---|---|---|
| `apple,power-sample-period` | `u32` req | DTS 8 | `8` | static (= ADT `gpu-power-sample-period`) |
| `apple,min-sram-microvolt` | `u32` req | DTS 850000 | `cf850` | static, no ADT counterpart found |
| `apple,avg-power-filter-tc-ms` | `u32` req | DTS 1000 | `3e8` | static (= ADT `gpu-avg-power-filter-tc-ms`) |
| `apple,avg-power-ki-only` | `F32` req | DTS 7.5 | `40f00000` | static (= ADT `gpu-avg-power-ki-only`) |
| `apple,avg-power-kp` | `F32` req | DTS 4.0 | `40800000` | static (= ADT `gpu-avg-power-kp`) |
| `apple,avg-power-min-duty-cycle` | `u32` req | DTS 40 | `28` | static (= ADT `gpu-avg-power-min-duty-cycle`) |
| `apple,avg-power-target-filter-tc` | `u32` req | DTS 125 | `7d` | static (= ADT `gpu-avg-power-target-filter-tc`) |
| `apple,fast-die0-integral-gain` | `F32` req | DTS 200.0 | `43480000` | static (= ADT `gpu-fast-die0-integral-gain`) |
| `apple,fast-die0-proportional-gain` | `F32` req | DTS 5.0 | `40a00000` | static (= ADT `gpu-fast-die0-proportional-gain`) |
| `apple,perf-filter-drop-threshold` | `u32` req | DTS 0 | `0` | static (= ADT `gpu-perf-filter-drop-threshold`) |
| `apple,perf-filter-time-constant` | `u32` req | DTS 5 | `5` | static (= ADT `gpu-perf-filter-time-constant`) |
| `apple,perf-filter-time-constant2` | `u32` req | DTS 50 | `32` | static (= ADT `gpu-perf-filter-time-constant2`) |
| `apple,perf-integral-gain2` | `F32` req | DTS 0.197392 | `3e4a2121` | static (= ADT `gpu-perf-integral-gain2`) |
| `apple,perf-integral-min-clamp` | `u32` req | DTS 0 | `0` | static (= ADT `gpu-perf-integral-min-clamp`) |
| `apple,perf-proportional-gain2` | `F32` req | DTS 6.853981 | `40db53d0` | static (= ADT `gpu-perf-proportional-gain2`) |
| `apple,perf-tgt-utilization` | `u32` req | DTS 85 | `55` | static (= ADT `gpu-perf-tgt-utilization`) |
| `apple,ppm-filter-time-constant-ms` | `u32` req | DTS 100 | `64` | static (= ADT `gpu-ppm-filter-time-constant-ms`) |
| `apple,ppm-ki` | `F32` req | DTS 91.5 | `42b70000` | static (= ADT `gpu-ppm-ki`) |
| `apple,ppm-kp` | `F32` req | DTS 6.9 | `40dccccd` | static (= ADT `gpu-ppm-kp`) |
| `apple,pwr-min-duty-cycle` | `u32` req | DTS 40 | `28` | static (= ADT `gpu-pwr-min-duty-cycle`) |
| `apple,perf-base-pstate` | `u32`, default 1 | DTS 1 | `1` | static, no ADT counterpart found |
| `apple,power-zones` | `Vec<u32>`, length a multiple of 3 and at most 15: (target, target offset, filter tc) per zone; default empty | DTS `30000 100 6875` | 12 bytes: `7530 64 1adb` | static (= ADT `gpu-power-zone-target-0`, `gpu-power-zone-target-offset-0`, `gpu-power-zone-filter-tc-0`) |
| `apple,pwr-filter-time-constant` | `u32`, default 313 | DTS 313 | `139` | static (= ADT `gpu-pwr-filter-time-constant`) |
| `apple,pwr-integral-gain` | `F32`, default 0.0202129 | DTS 0.0202129 | `3ca59586` | static (= ADT `gpu-pwr-integral-gain`) |
| `apple,pwr-integral-min-clamp` | `u32`, default 0 | DTS 0 | `0` | static (= ADT `gpu-pwr-integral-min-clamp`) |
| `apple,pwr-proportional-gain` | `F32`, default 5.2831855 | DTS 5.2831855 | `40a90fdb` | static (= ADT `gpu-pwr-proportional-gain`) |
| `apple,core-leak-coef` | `Vec<F32>` of exactly 1 (`max_num_clusters`), req | m1n1 `calc_power_t8103()`: start 1000.0, then `adjust_leakage(cores, 8, 0.12)`: `uncore = v * f; v = uncore + cores/8 * (v - uncore)`. `cores` = popcount of the low 8 bits of the SGX register at `sgx + 0xd01500`, read with the GPU powered. | `447a0000` (1000.0, i.e. 8 cores; the register was measured as 0xff) | same formula in integer soft-float; **the core mask must be supplied by the caller**, it is not in the ADT |
| `apple,sram-leak-coef` | `Vec<F32>` of exactly 1, req | m1n1: start 45.0, `adjust_leakage(cores, 8, 0.2)` | `42340000` (45.0) | same |

## Absent on Linux for T8103: driver defaults

None of these is in `t8103.dtsi`, m1n1 does not add them, and the dump of this
machine does not have them. The library does not produce them either, so the
driver's defaults apply exactly as on Linux.

| Property | Shape | Default |
|---|---|---|
| `apple,fast-die0-prop-tgt-delta` | `u32` | 0 |
| `apple,fast-die0-release-temp` | `u32` | 80 |
| `apple,fender-idle-off-delay-ms` | `u32` | 40 |
| `apple,fw-early-wake-timeout-ms` | `u32` | 5 |
| `apple,idle-off-delay-ms` | `u32` | 2 |
| `apple,idleoff-standby-timer` | `u32` | 0 (`idle_off_standby_timer_default` of `hw/t8103.rs`) |
| `apple,perf-boost-ce-step` | `u32` | 25 |
| `apple,perf-boost-min-util` | `u32` | 100 |
| `apple,perf-integral-gain` | `F32` | 7.8956833 |
| `apple,perf-proportional-gain` | `F32` | 14.707963 |
| `apple,perf-reset-iters` | `u32` | 6 |
| `apple,pwr-sample-period-aic-clks` | `u32` | `base_clock_hz / 1000 * power-sample-period` = 24000 * 8 |
| `apple,se-engagement-criteria` | `i32` | -1 |
| `apple,se-filter-time-constant` | `u32` | 9 |
| `apple,se-filter-time-constant-1` | `u32` | 3 |
| `apple,se-inactive-threshold` | `u32` | 2500 |
| `apple,se-ki` | `F32` | -50.0 |
| `apple,se-ki-1` | `F32` | -100.0 |
| `apple,se-kp` | `F32` | -5.0 |
| `apple,se-kp-1` | `F32` | -10.0 |
| `apple,se-reset-criteria` | `u32` | 50 |

The ADT has a few related values the Linux path ignores
(`gpu-fast-die0-target`, `gpu-fast-die0-alarm-threshold`, `gpu-sochot-temp`,
`gpu-pwr-perf-scale0/1`, `gpu-num-perf-states`); they are not used here.

## Operating points

| Property | Shape | Linux source | This M1 | Darwin |
|---|---|---|---|---|
| `operating-points-v2` (on the GPU node) | one phandle | DTS, `&gpu_opp` | 4 bytes, equals the table's `phandle` | phandle chosen by the caller |
| `opp-table-gpu`: `compatible` | not read by the driver | DTS `operating-points-v2` | 20 bytes | static |
| `opp-table-gpu`: one child per state, in order | the driver takes **every child in tree order** as performance state 0, 1, 2...; no sorting | DTS: 7 children `opp00`..`opp06` | 7 children | 7 children, same names, attached in order |
| child `opp-hz` | `u64` | m1n1: ADT `perf-states[i].freq` (u32, Hz) | 8 bytes each, identical to the library's | same |
| child `opp-microvolt` | `Vec<u32>` of exactly 1 (`max_num_clusters`) | m1n1: ADT `perf-states[i].volt` (mV) * 1000 | 4 bytes each, identical | same |
| child `opp-microwatt` | `u32` | m1n1: `volt_mV * scale[i] * 100`, `scale = {0, 63, 80, 108, 150, 198, 210}` | 4 bytes each, identical | same |

ADT inputs and the checks m1n1 makes, all ported: `perf-state-count` must be 7,
`perf-state-table-count` must be 1, `perf-states` must be 7 pairs of
(u32 frequency, u32 millivolts). The values in `t8103.dtsi` (603000 uV at
396 MHz, and so on) are placeholders that m1n1 overwrites; this machine's ADT
gives slightly higher voltages. The driver derives `max_power_mw`,
`max_freq_mhz` and `perf_max_pstate` from this table.

## Reserved memory

The driver calls `reserved_mem_region_to_resource_byname(name)` on the GPU
node: index of `name` in `memory-region-names`, that phandle of
`memory-region`, the target must be available (`status`), then its `reg`.

| Property | Shape | Linux source | This M1 | Darwin |
|---|---|---|---|---|
| `memory-region` | 6 phandles | DTS | 24 bytes, 6 consecutive numbers | 6 phandles `base+0..5` |
| `memory-region-names` | string list | DTS: `ttbs`, `pagetables`, `handoff`, `hw-cal-a`, `hw-cal-b`, `globals` | 50 bytes | static |
| `reserved-memory`: `#address-cells`, `#size-cells`, `ranges` | 2, 2, empty | DTS | same | static |
| `uat-handoff@10`: `reg`, `status` | (2-cell address, 2-cell size); driver name `handoff`, mapped cached, must hold its `Handoff` struct | m1n1 `dt_set_region()`: ADT `gfx-handoff-base`, `gfx-handoff-size` (u64 each, both non-zero), `status = "okay"` | 16 bytes, identical; size 0x4000 | same copy |
| `uat-pagetables@20`: `reg`, `status` | driver name `pagetables`; at least 0x4000; the driver uses its start as the kernel-half table root | m1n1: ADT `gfx-shared-region-base`, `-size` | identical; size 0x40000 | same copy |
| `uat-ttbs@30`: `reg`, `status` | driver name `ttbs`; at least 64 * 16 bytes | m1n1: ADT `gpu-region-base`, `-size` | identical; size 0x4000 | same copy |
| `hw-cal-a`, `hw-cal-b`, `globals` | regions holding init data to compare against; optional | m1n1 allocates them from the top of memory at boot and fills them with its own copy of the init data | `status = "okay"`, `reg` with sizes 0x8000, 0x4000, 0x14000 | **not provided**: nodes present with `status = "disabled"` and no `reg`, as in the DTS before m1n1 runs |
| `debug,hw-cal-a-size`, `debug,hw-cal-b-size`, `debug,globals-size` | `u32`, default 0 | m1n1: byte sizes of those three blobs | `421c`, `1884`, `12394` | **not provided** |

The three UAT regions are allocated by iBoot, not by m1n1. On this machine each
is exactly one entry of ADT `/chosen/carveout-memory-map`, in the last megabytes
of DRAM, outside the memory XNU is given.

## Read by the Linux core, not by the driver

| Property | Used by | Linux source | This M1 | Darwin |
|---|---|---|---|---|
| `mboxes` | RTKit, to find the ASC mailbox | DTS: phandle of `mbox@206408000` (`apple,asc-mailbox-v4`, reg `0x206408000`/`0x4000`, AIC interrupts 575..578) | 4 bytes | produced only when the caller passes a phandle. The mailbox node itself is not created here: RTKit on Darwin is SF-A's. The ADT agrees on the facts (`/arm-io/gfx-asc` `reg[0]` = `0x206400000`, interrupts 575..578). |
| `power-domains` | genpd, to power the device around probe | DTS: phandle of PMGR `ps_gfx` (reg offset `0x3f8`, parent `ps_pmp`) | 4 bytes | produced only when the caller passes a phandle; power on Darwin is SF-A's PMGR |
| `name` | appears in `/proc/device-tree` | generated by the kernel | 4 bytes | not a property; the registry entry's name |

## `/chosen`

The T8103 path reads nothing from `/chosen`. The library still produces the
four strings m1n1's `kboot.c` derives from the same firmware lookup
(`asahi,iboot1-version`, `asahi,system-fw-version`, `asahi,iboot2-version`,
`asahi,os-fw-version`) because they check the port of that lookup against the
ground truth. They are marked informational and the kernel shim does not
publish them.
