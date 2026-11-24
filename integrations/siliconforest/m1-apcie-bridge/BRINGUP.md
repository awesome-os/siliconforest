# Port-0 bring-up engine (slice 2)

`bringup.c` / `bringup.h` hold original Darwin-side C for bringing up the J293
`apcie,t8103` controller and its port 0 (the BCM4378 Wi-Fi bridge). The code
follows m1n1's T8103 sequence in `src/pcie.c`, `src/pmgr.c` and `src/tunables.c`
(MIT; see `NOTICE-bringup`). It is pure C, with no IOKit, no allocation and no
mutable globals. All register access goes through the caller's `struct apb_io`,
whose callbacks take a region number and an offset. Nothing in the bundle calls it yet.

## The 23 steps

Steps 1–12 touch shared controller state:

1. PMGR power-on of IMX → APCIE → APCIE_GP, then PCIE_REF.
2. axi2af tunables.
3. RC+0x4 = 0.
4. Common tunables.
5. PHY tunables.
6. PHY CLK0 request and ack.
7. PHY CLK1 request and ack.
8. PHY reset release.
9. PHYIF RUN.
10. The 11 fuse transfers.
11. PHY-IP PLL tunables.
12. PHY-IP auspma tunables.

Steps 13–23 touch port 0 only:

13. Config tunables.
14. APPCLK.
15. PERST release (PORT_RESET DIS).
16. Wait for STATUS RUN.
17. Wait for LINKSTS not BUSY.
18. DBI write enable.
19. rc, gen3 and gen4 tunables.
20. Link speed (LNKCAP, LNKCAP2, 16-bit LNKCTL2, SPEED_CHANGE).
21. One-lane width.
22. DBI write disable.
23. Read LINKSTS.

Every write target is either on a fixed whitelist or a tunable that the strict
parser has already checked. Fuse reads are limited to 0x84, 0x418 and 0x41c.
Polls are bounded, and the engine counts reads and writes against hard limits.
A record whose `dirty` flag is set is refused, so there is never a second
attempt on top of a partial one.

## The write gate

- The writer call for shared regions (PMGR, RC, PHY, PHY-IP, AXI) exists only
  under `#ifdef APB_FAKE_REGISTER_FILE`. Without that macro, the branch
  increments `gated_writes` and returns `APB_GATED`.
- `#if defined(KERNEL) && defined(APB_FAKE_REGISTER_FILE)` is an `#error`.
  `add_kext_bundle` defines `KERNEL` for every source, so a kext build cannot
  turn shared writes on. `build-bringup-gate.nix` also fails if the `bringup.c`
  compile line lacks `-DKERNEL` or contains the fake macro.
- All shared steps come before the port steps. In a bundle, a run stops at
  step 1 (POWER) with one PMGR read and zero writes, which the gated host test
  checks. The port-local write path is compiled in, but it is unreachable
  because step 1 is always gated.
- No adapter code calls `apb_bringup_port0`. `check_bringup_bundle.py` checks
  that no branch or data word refers to it.

## Differences from m1n1

- Each PMGR domain is enabled once, parent first. m1n1 re-enables shared
  parents on the way to each child. The end state is the same.
- The engine stops at the first PMGR timeout. m1n1 still tries the remaining
  clock-gates and then returns the error.
- The tunable parser is strict. Every record must be 4 bytes wide and aligned,
  sit inside its window, use 32-bit mask and value fields, and keep the value
  inside the mask. m1n1 also accepts 1-, 2- and 8-byte records. All J293
  apcie records are 4 bytes.
- The link-speed step needs a resolved speed of 1–4. m1n1 skips the step when
  `maximum-link-speed` is absent; here the plan is refused. The J293 has the
  property, with value 2.
- LNKCTL2 uses a 16-bit read-modify-write so that Link Status 2 is never
  written back. m1n1 does the same with `mask16`.
- Link width is fixed at one lane, as for every T8103 port in m1n1.

## Checks

```sh
python3 -I test_bringup.py --adt <saved guest ADT>
nix --extra-experimental-features 'nix-command flakes' build --impure --file build-bringup-gate.nix
python3 -I check_bringup_bundle.py <result>
```

- `test_bringup.py` runs the C tests in fake mode and in gated (bundle-equivalent)
  mode under ASan/UBSan, plus the preprocessor gate proofs and the static checks.
  With `--adt`, it confirms that all nine real J293 tunable sets parse into their
  windows and that the link speed resolves to 2. It prints counts only.
- `check_bringup_bundle.py` checks the SDK binary:
  - `apb_shared_writes_compiled` is `mov w0,#0; ret`.
  - The shared branch of `wr()` has no call.
  - Only five indirect calls exist.
  - Nothing refers to `apb_bringup_port0`.
  - The linked bundle text matches the kernel object.

## Not in this slice

- An IOKit `apb_io` backend (writable maps).
- Using PMGR through the platform instead of raw writes.
- Any Wi-Fi power-enable or PERST# GPIO beyond PORT_RESET.
- Config-space nubs, IOPCIBridge, DART and MSI.
- Any live run.

Before any shared PHY or PMGR write is allowed on hardware, these need their
own scope review. That state is controller-wide, and IMX is a parent domain
shared with other SoC blocks.
