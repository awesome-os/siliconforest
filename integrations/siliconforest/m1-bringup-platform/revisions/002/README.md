# Platform patch revision 2: Apple reboot path

Status: **source patch, not compiled, not booted.** It is not staged anywhere.
The frozen rev1 `../../platform.patch` (sha256 `c4a4e83a…`) and the isolated
build tree are unchanged. Root owns staging and rebuilding.

## Problem (attempt-006)

After the primary panic, `halt_all_cpus` ("MACH Reboot") calls
`PEHaltRestart(kPERestartCPU)`. That reaches the handler the platform expert
installed for every board, `PE_halt_restart = pdArmHaltRestart`.

On Apple hardware the call chain, symbolicated against the booted KC
`z981hgxk…` and the platform kext, is:

    pdArmHaltRestart+0x74
     -> PDArmCPU::systemReset+0x14
     -> pd_psci_available+0x78
     -> PDSun50i_isPlatform+0x5c
     -> IORegistryEntry::getProperty
     -> IORecursiveLockLock
     -> lck_mtx_lock on the panic path
     -> nested panic (entries 2 and 3)

Even without the lock, the next step would be an HVC PSCI probe. Apple SoCs
have no PSCI firmware, and m1n1 does not implement it.

## Change

`rev1-to-rev2.patch` touches only `PDArmPlatformExpert.cpp` and removes
exactly one line:

- **New hook:** `virtual bool usesPSCIHaltRestart(void)`. The generic default
  returns `true`, so QEMU, Pi, Sun50i, SG2002 and A733 behaviour is unchanged.
- **`start()`:** installs `pdArmHaltRestart` only when the hook returns true.
- **Apple override:** `PDAppleARMPlatformExpert::usesPSCIHaltRestart()`
  returns `false`. `PE_halt_restart` stays `NULL` (its initial value in
  `IOPlatformExpert.cpp:336`).

## Resulting behaviour on Apple (from source)

When anything asks to halt or restart:

1. `IOPlatformExpert::haltRestart()` finds no handler and returns `-1`,
   without touching the registry or PSCI.
2. `halt_all_cpus()` then calls `pd_psci_halt()`. Its lock-free SecureDT
   lookup finds no `/psci` node on the ADT, so it returns.
3. `halt_all_cpus()` spins in `while (1)`.

There is no reset and no claimed reset: recovery stays external (the M5
controller). `reboot(2)` on Apple therefore parks instead of restarting. A real
M1 reset (for example the watchdog) is separate future work; nothing here
pretends to provide it.

## Files

| File | sha256 |
|---|---|
| `platform-rev2.patch` (cumulative, pinned e7aa00a -> rev2) | `94aa3211a858e8b22a4d1b92e5cb3a247d367ee2a17a3285f366435209cf7c1c` |
| `rev1-to-rev2.patch` (incremental, on top of rev1) | `c36e2b41c83bb607aa19f17fda62bb2e5605d724621c5dcc9e753ecf002bd0b6` |
| `test_platform_rev2.py` | `b7dfb6cf7a9c8b0307c3efac0aae31454f0b26121e3629b54fffd2835f9ef41f` |

The isolated tree's PDArmPlatformExpert files are byte-identical to rev1, and
`rev1-to-rev2.patch` applies to a copy of them (checked by the tests).

Resulting kext sources (sha256):

| File | sha256 |
|---|---|
| `PDArmPlatformExpert.cpp` | `a2d03ba91310de6c5d70f862d5dfe057faffd053905e5d53cd320262d7d70015` |
| `PDAppleAIC.cpp` (= rev1) | `83f205f75bd5659639b982a412c9bdae58401c27d8226e1db0d9458215826296` |
| `PDAppleBootCPUPMGR.cpp` (= rev1) | `2ee996cf8acf27edf7551842e3bf9f7f5bee2a6d2c66d3dd48937741caf96eb8` |
| `PDAppleBootCPUPMGR.h` (= rev1) | `46594c5be4e08f7ed4eeb000490c2c76d0a994ca578d198a335c65119b99442e` |
| `CMakeLists.txt` (= rev1) | `0d75ed8f2c84caf7b865f102a5fcac4135e564afdffda4e1bb15ba6f76866df9` |

## Check

```sh
python3 integrations/puredarwin/m1-bringup-platform/revisions/002/test_platform_rev2.py -v
```

10 tests pass. They check that:

- the frozen rev1 hash is intact;
- the cumulative patch equals rev1 plus the incremental patch;
- the incremental patch is scoped to one file and removes only the
  unconditional assignment;
- the Apple subclass installs no handler, while the generic default and gate
  are intact;
- all rev1 checks still pass (imported read-only from the frozen rev1 test);
- the incremental patch applies to a copy of the isolated tree.

Seven mutants are caught:

- Apple keeps PSCI;
- the handler is installed unconditionally;
- the generic default is flipped;
- Apple installs a fake reset handler;
- the override is undeclared;
- an invented WDT reset;
- an extra line is removed.

## Not covered

- **Compile, link, KC inclusion, load and boot:** the first compile is root's
  rebuild.
- **Clean panic reboot:** none. After a panic the CPU now parks without the
  nested panic; the M5 recovery performs the reset.
