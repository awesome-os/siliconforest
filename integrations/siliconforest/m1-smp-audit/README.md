# m1-smp-audit: stress for what the multi-core audit found

The audit and the list of fixes are in `docs/roadmap/research/smp-lock-audit.md`. This
directory holds the programs and scripts that exercise those findings on the M1 with 8 CPUs.

| File | What it is |
|---|---|
| `test/drm-storm.c`, `drm-storm.nix` | N threads against the asahi render node: open/close, ioctls on a shared fd, GEM create/mmap/verify (fresh objects must be zero, writes must stick), syncobj create/wait/destroy with neighbours waiting on handles being destroyed, PRIME export and import into a second open file in every order of release, sync-file and dma-buf poll with the sync-file ioctls, close-while-ioctl, mmap-while-close. One `DRMSTORM case=... result=ok|FAIL` line per case, `DRMSTORM_DONE status=ok|failed`. |
| `guest/darwin-la-stress.in` | Guest script. Stages `storm`, `gl`, `wifi`, `session` (see its header for which findings each one aims at), 8 CPU workers under every stage. `LA_<STAGE>_RC=` per stage, `LA_RESULT stages=N failed=M status=PASS|FAIL`. |
| `tools/la_boot.sh` | One boot: builds or takes the desktop collection, stages the session's files plus `agx-offscreen-test` and `drm-storm`, queues for the bench itself (`tools/bench.sh run`). `la_boot.sh <name> [stages] [scale seconds]`. |
| `tools/la_soak.sh` | The 2-hour soak for the overnight batch: `tools/la_soak.sh <name> [minutes]`. Not to be started without the coordinator scheduling it. Pass and fail lines are in its header. |

Short boots (each under 10 minutes):

```
LA_KC=<collection> integrations/siliconforest/m1-smp-audit/tools/la_boot.sh la01 storm,gl 5
LA_KC=<collection> integrations/siliconforest/m1-smp-audit/tools/la_boot.sh la02 wifi,session 10
```

The collection must carry the audit's fixes (`m1-smp/build-kc-desktop.nix` as it is in the
tree: layer patches 0028-0040, drm72 0010, platform patch 0004). Without layer patch 0028
`drm-storm`'s `open` case runs out of `/dev/dri` slots after 1023 opens.

What the stress cannot show: the boot-time races (P1-P3 in the audit) need two nubs starting at
the same instant, and the AIC race (H1) a teardown within nanoseconds of an interrupt; a clean
run says little about those. Open findings that the stages do reach and that may still fail
are W2 (delayed work), X2 (RCU across the two DRM kexts), X4 (three-way reservation deadlock)
and W3 (scheduler teardown at client exit).
