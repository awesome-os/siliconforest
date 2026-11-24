## Goal
Run at full CPU speed and on all eight cores.

## Why
The desktop is slow partly because the kernel runs on one efficiency core at whatever frequency iBoot left. Measured facts: the boot CPU is an Icestorm core (MIDR part 0x022), boot-args carry `cpus=1`, and nothing raises the performance state.

## Measured on the M1 (2026-11-09, boots `c01` and `c02`)
The frequency step (item C1) is done and on by default in the launcher, and **it made no difference on its own**:

| | Efficiency cluster P-state | Performance cluster P-state | sha256 of zeros in 20 s (two runs) |
|---|---|---|---|
| Without the step | 5 (already its top state) | 1 | 552 / 568 MB |
| With `cpufreq_init` | 5 | 7 | 560 / 568 MB |

The efficiency cluster is already at its highest state when we get control, and the one CPU we run on is an efficiency core. The step only raised the performance cluster, which is idle until SMP works. So the earlier assumption that the kernel runs at a low boot frequency was wrong for the core in use. **The CPU speedup comes from C2-C4 (performance cores) and from a non-DEBUG kernel (SF-D), not from C1.** Guest benchmark: `integrations/puredarwin/m1-apcie-host/supervisor/darwin-cpu-bench.in`.

## Decision
- **Frequency:** first call m1n1's existing `cpufreq_init` from the launcher (configuration only). Later, a native kext ported from Linux `drivers/cpufreq/apple-soc-cpufreq.c` (461 lines, GPL-2.0).
- **SMP:** native code in the platform expert, ported from m1n1 `src/smp.c` (MIT). XNU already has the Apple SMP machinery (`USE_APPLEARMSMP`); it needs a real `enableCPUCore`.
- **The boot CPU stays an efficiency core.** XNU and m1n1 both take the core iBoot started; the fix is SMP plus scheduler placement, not changing the boot core.

## What exists (read from source; see `docs/roadmap/research/cpu-boot.md`)
- One CPU is enforced in four places: launcher (`hv.smp = False`, secondary start stops the guest), hypervisor device-tree pruning of non-running CPUs, `cpus=1` in the boot-args, and `PDAppleBootCPUPMGR` (130 lines) which panics in `enableCPUCore`.
- The m1n1 hypervisor already supports multi-vCPU guests (start hook on the PMGR CPU-start registers, 1:1 vCPU to core).
- `PDAppleAIC` routes every interrupt to CPU 0; IPIs are system-register fast IPIs and need no AIC code.
- `PDPerfControl` (85 lines) has an efficiency/performance cluster policy that the Apple platform-expert path does not call.

## Work items
| ID | Deliverable | Acceptance on the M1 | Size |
|---|---|---|---|
| C1 | **Done.** Launcher calls `p.cpufreq_init()` before starting the guest (`PCIE_CPUFREQ=off` disables it) | Register readback logged each boot; no speedup on one efficiency core (see above) | done |
| C2 | `enableCPUCore` for T8103 from m1n1 `smp.c:319-372`: PMGR CPU-start registers (`+0x54000`), per-cluster start bits, reset vector check | Compile evidence plus a register-sequence comparison against m1n1 | ~200 lines |
| C3 | Launcher SMP mode (needs SF-B B4), drop `cpus=1` | `sysctl hw.ncpu` reports 8; both clusters visible | ~100 lines |
| C4 | Scheduler placement: wire `PDPerfControl` on the Apple path; interrupt affinity review | An 8-thread load runs 10 minutes without panic; interactive threads land on performance cores | ~50 lines + soak |
| C5 | Native DVFS kext so frequency follows load | Performance cluster reaches its top states under load; 1,000 transitions without a hang | ~500 lines |
| C6 | Rerun the Wi-Fi lifecycle and throughput tests on 8 CPUs (the driver has only ever run on one) | Tests pass; any locking bug filed against SF-M | test only |

## Dependencies
C1 is done. C3 needs SF-B B4. C2 can be written in parallel with everything.

## Risks
- Whether our modified kernel start code still places the secondary reset vector where the hypervisor expects.
- XNU's idle path writes a register the hypervisor refuses in some modes.
- Hypervisor cost with 8 CPUs (IPIs trap; a slow path stops all CPUs).
- No thermal management exists once frequencies are raised.

## Out of scope
Running without the hypervisor.
