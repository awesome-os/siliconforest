# Development increment 007 — November 7, 2026

The selected first physical target is the 13-inch M1 MacBook Pro, J293/T8103.
Read-only SSH inventory confirms its existing Arch Linux ARM/m1n1/U-Boot setup.
The M5 is the controller. No target boot, mode switch, boot-policy change or
internal-storage write has been performed in this increment.

The native T8103 diagnostic image now builds with the pinned PureDarwin SDK.
It is deliberately restricted to XNU entry, polled console output and permanent
parking before VM, IOKit or userspace. It cannot reach a Darwin shell. The
normal Apple platform CPU path still needs its GIC dependency replaced; security
backends and four unavailable cache operations remain unsupported. The latter
bind to explicit nonreturning failure loops, not successful implementations.

## Native build and artifact

The unchanged base pin is `e7aa00a60f0b56ef35aa24252e81467787bf71ea`.
Candidate revision 6 is patch SHA-256
`573bc2eaddd10ecf4250b8751b7b7459229dba77ee0483a8259debc7bd1565c4`.
Five terminal SDK failures are preserved with their exact source snapshots and
logs. Attempt 006 succeeded. Its record includes the dirty source NAR, locked
inputs, recursive submodule, build command, log and artifact hashes:
`reports/sprint-007/kernel-build-006.json`.

The actual ARM64 `MH_EXECUTE` kernel is 26,829,912 bytes, SHA-256
`d6c7ba75025d595d12e171810b89344dfaa6b467f7a444bcce277a35b8175ff2`.
Inspection confirms the real `LC_UNIXTHREAD`/`__start` branch, diagnostic
instruction prefix, final self-loop and all four unsupported cache bindings.
The inline AST preemption check is present; the optional scheduler hygiene
wrapper is not selected. See `kernel-artifact-inspection.json`,
`kernel-otool-header.txt` and the independent source review.

## Controller and packet

The default debug/proxy m1n1 image was built from
`121fcbf35d4501814b58ca5347926a4173b6a512`, with the full pin as its BUILD_TAG.
Its raw SHA-256 is
`05b36cc8288843211e97bd4f68adcf521c81e5e68afd98a4d094a6f03defb32c`.
The receipt records host tool versions and hashes all 240 host source files;
those tools are observations, not a fully hermetic Nix build lock.

The review packet is `artifacts/m1-firstboot-007-r6`. It contains the real
kernel, raw m1n1, source patch, two build receipts, hashes and inspection record.
It was transferred to the isolated M5 user workspace
`/Users/chriskearney/darwin-lab-firstboot-007/packet-r6`. The packet inspector
passed there. The pinned Mach-O parser prepared the image offline on Linux and
M5 with identical results: 78,308,440 bytes, SHA-256
`e7a9ffbc6c80296caab1c057be2cdfe314c141e8d02a9a3bdba1bfca46cac0b4`.
This is byte preparation, not loading into target memory or executing XNU.

The M5's isolated Python environment was installed from hash-locked offline
wheels. Existing maclab/system Python installations were preserved. The
controller launcher passed independent source review and 40 host-only tests on
both Linux and M5. Frozen controller source hashes match both hosts;
`docs/M1_FIRST_BOOT.md` and `HOST_CONSOLE.md` define the physical boundary.

## Remaining physical qualification

The installed M1 reports historical dirty m1n1 stages, not the new pinned proxy.
Live proxy establishment, actual USB transport, recovery and firmware handoff
remain physical-session qualification work. A USB cable or listed controller
TTY is not proof of those conditions. The offline tools never grant execution
permission. No physical boot or hardware checkpoint is claimed here.

The earlier ARM64 virt results remain separate: upstream Darwin shell, real
probe activation, unmapped PASS, and mapped BLOCKED with permanent quarantine.
No mapped reuse, native storage, display, GPU or Chonkstep support is added.

## Completed software checkpoint and expanded goal

`reports/sprint-007/completion.json` records completed preboot software
preparation: 172 Python tests, 77 C groups, 27 detected mutations, 11 focused
source/assembly checks, and 40 controller tests on each host. Required offline
metadata checks and publication dry run passed. No publication occurred.

The user then requested demonstrated **unattended** boot, test and recovery
cycles and identified an existing Linux test setup. That is the next active
objective, tracked separately in sprint008. Existing Maclab services and
retained test records are being inspected before any physical experiment.
