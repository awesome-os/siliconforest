# m1-gpu-dt: Linux-style device-tree properties for the M1 GPU, from Apple's device tree

Ticket SF-F item F12. The Rust Asahi GPU driver configures itself from a Linux
device-tree node. On Linux that node comes from a static device tree source
plus values m1n1 computes at boot from Apple's device tree (ADT). Darwin only
has the ADT. This directory produces the same properties from it, for T8103
(M1, G13G) only.

**Status: host-tested; the kext compiles and links into a kernel collection.**
Nothing here has been loaded or run on hardware. It does not show that the
driver works.

| Path | What |
|---|---|
| `PROPERTIES.md` | Every property the T8103 driver path reads: shape, where Linux gets it, what this M1 shows, how Darwin provides it |
| `src/gpudt.h`, `src/gpudt.c` | The library: C11, no kernel headers, no floating point, no allocation. MIT; the T8103 computations are ported from m1n1 |
| `src/gpudt_summary.c` | The summary lines the kext logs and the host test predicts (needs `snprintf`) |
| `src/gpudt_fw_table.inc` | m1n1's iBoot-string to OS-version table, generated from `firmware.c` |
| `kernel/SFGpuDT.{h,cpp}`, `Info.plist`, `CMakeLists.txt` | The kext: powers GFX, reads the ID registers and the core mask, runs the library, logs; publishes the nodes with `sfgpudt=2` |
| `build-kext.nix`, `build-kc.nix` | Nix recipes for the kext and for the current kernel collection plus the kext |
| `kernel/sf_linux_of.patch` | Change `sf_linux_of.c` needs, and the list of missing symbols. **Not applied** |
| `kernel/sf_linux_fwnode.c` | The missing `of`/`fwnode` functions, proposed for the layer. Host-tested against stand-in headers only |
| `test/`, `Makefile` | Host test against the saved ADT and the Linux ground truth |

## Display nodes (`src/dispdt.{h,c}`, `kernel/SFDispDT.cpp`)

For the pin's `drivers/gpu/drm/apple` on J293: `iommu@23130c000`, `iommu@231304000`,
`mbox@231c08000`, `power-management@23b738000`, `clock-disp0`, `dcp@231c00000` with `piodma`
and `panel`, `display-subsystem`, the five reserved regions m1n1 adds (`dcp_data`,
`region57`, `region94`, `region95`, `framebuffer`, from `/chosen/carveout-memory-map` and
`/vram`) and `chosen/framebuffer@<base>`. Static values are `t8103.dtsi`, `t8103-jxxx.dtsi`
and `t8103-j293.dts` at the pin; the boot-time parts follow m1n1 `kboot.c`. Host-tested
against the saved device tree (`build/dispdt_test`, part of `make test`); the kext part is
compile evidence. Boot-arg `sfdispdt=1` logs a summary, `sfdispdt=2` publishes (with
`sfgpudt_stringprops=1`); default is off.

Not produced: `interrupts`/`interrupt-parent`, `power-domains`, `resets` (power and
interrupts come from native interfaces, as for the GPU), the `ports` graph to the audio
node, `iommu-addresses` unless the caller supplies the mapped addresses (the kext does not;
the DART backend in `../m1-dcp-dart` reads the mappings from the tables themselves), and
the second `iommus` entry of `display-subsystem` (dispext0).

Compared with the live Linux tree of this machine (`make compare-disp`, needs the untracked
`private/linux-dt/display-nodes.txt`): 87 properties identical once phandle numbers are
translated, 30 differences explained (`name`; `interrupts`, `interrupt-parent`,
`interrupt-names`, `power-domains`, `resets`, none of which the T8103 driver path or this
layer consumes; the dispext0 entry), none unexplained. For that comparison the regions'
mapped addresses, which give `iommu-addresses`, are passed in as boot dc12 measured them; the
kext does not publish `iommu-addresses` (only Linux's IOMMU core reads it).

`kernel/sf_of_iter.c`: `of_find_compatible_node()` with Linux's `from` semantics, which the
layer's lacks (it always returns the first match); see `../m1-dcp-dart/ANSWERS.md`.

## Running the test

```
make test
```

Needs `cc` (gcc or clang; `make test CC=clang` also passes) and `python3`, no
other packages. Inputs, both untracked:

- `ADT=` a saved Apple device tree; default is the newest
  `runs/m1-desktop-010/*/guest/original-adt.bin`. The `guest-adt.bin` next to
  it (the tree the hypervisor guest is given) has identical GPU nodes and gives
  the same result.
- `GT=` the Linux dumps; default `private/linux-dt/gpu-nodes.txt` plus
  `gpu-child-nodes.txt` (OPP and reserved-memory child nodes) when present.
- `CORE_MASK=` for the predicted kext log; default `0xff`, the value measured
  on this machine.

`test/gen_fixture.py` copies the handful of ADT nodes the library reads into
`build/adt_fixture.c`. `build/` is ignored by git and must stay so: it holds
bytes of a real machine's device tree. The test then:

1. builds the library a second time freestanding with `-mgeneral-regs-only`
   and checks it references nothing but `mem*`/`str*`;
2. runs `harness selftest`: the integer soft-float against the host FPU,
   m1n1's `adjust_leakage()` in real floats for 0..8 cores, every refusal path,
   the firmware compat mapping, and that each UAT region is exactly one iBoot
   carve-out;
3. runs `build/fwnode_test`: `kernel/sf_linux_fwnode.c` serving the calls the
   driver makes (firmware tuple, regions by name, OPP children in order) from
   the library's tree;
4. runs `test/compare.py`, which prints one line per property (identical /
   DIFFERS with both values / differs as intended / not in ground truth / not
   produced), recomputes the kext's "linux-comparable" checksum from the
   ground-truth bytes, and fails on any unexplained difference, a checksum
   mismatch, any required property missing, or any ground-truth property that
   is neither produced nor explained;
5. prints the log lines the kext is expected to produce.

## Results on this machine

```
linux-comparable checksum (fnv1a over 70 properties, phandles and boot-time blobs left out):
  library:      0xb9740392
  ground truth: 0xb9740392 over 70 properties
summary: 70 identical, 11 identical* (phandle numbers taken from the ground truth), 0 differ,
         3 differ as intended, 0 not in ground truth, 41 ground-truth properties not produced
static values with an ADT counterpart: 24, of which equal to the ADT: 24
result: PASS
```

- **70 identical**: 35 on the GPU node, the OPP table's `compatible`, all 21
  properties of the 7 operating points, 3 on `reserved-memory`, `reg` and
  `status` of the three UAT regions, and the 4 firmware strings in `/chosen`.
- **11 identical\***: phandle-valued properties (`mboxes`, `power-domains`,
  `memory-region`, `operating-points-v2`, and the `phandle` of the OPP table
  and the six region nodes). Phandle numbers are whatever the device-tree
  compiler assigned, so the test takes the base from the ground truth; the
  count and internal consistency are what is checked.
- **3 differ as intended**: `status` of `hw-cal-a`, `hw-cal-b`, `globals` is
  `disabled` here and `okay` on Linux, where m1n1 has allocated and filled
  them.
- **41 not produced**: 17 `name` entries (not FDT properties), the `reg` of
  those three blob regions, 3 `debug,*-size`, and 18 `/chosen` properties the
  GPU driver does not read (names and lengths printed only). 13 further
  reserved-memory nodes in the dump belong to other devices.
- The core count of 8 was first inferred (the only count that reproduces the
  ground-truth leakage coefficients) and has since been measured: the register
  reads 0xff (`reports/sprint-010/gpu-power-and-id-probe-001.md`). Soft-float
  equals the host FPU for every count 0..8, with or without a fused
  multiply-add.

## What the ADT alone does not give

| Item | What it is on Linux | In the ADT under the hypervisor boot? | Darwin |
|---|---|---|---|
| GPU core mask | m1n1 powers the GPU and reads `sgx + 0xd01500` | No. Hardware register; GFX is off when the guest starts | The kext powers GFX through PMGR itself, reads the register and passes it to the library, which refuses without it. Measured value on this machine: 0xff |
| `hw-cal-a`, `hw-cal-b`, `globals` regions and `debug,*-size` | Init-data blobs m1n1 builds with its own Rust code and puts in memory it allocates at boot | No | Not provided. The driver only compares its own init data with them and logs the result; without them it skips that. Nodes are left `disabled` |
| UAT handoff, page-table and TTB regions | Copied by m1n1 from ADT `gfx-handoff-*`, `gfx-shared-region-*`, `gpu-region-*`; iBoot allocated them. m1n1 chooses nothing for T8103 | Yes, same values in the original and the guest tree | Copied; identical to what Linux has. Whether the kernel can map them (they are outside XNU's managed memory) is for the LinuxKPI `ioremap` and is untested |
| Firmware version | m1n1 table lookup of ADT `/chosen` `firmware-version` | Yes | Same lookup. It names the OS whose iBoot booted; that the firmware already loaded in the coprocessor is that version is assumed, as on Linux |
| phandle numbers | Assigned by the device-tree compiler | n/a | Any unused numbers. The ADT has no `phandle` properties at all (it uses `AAPL,phandle`) |
| `mboxes` and `power-domains` targets | Static nodes for the ASC mailbox and the PMGR `gfx` power state | The facts are (`/arm-io/gfx-asc`, PMGR device 99) but not as Linux nodes | Left out unless the caller supplies phandles. Owned by SF-A (RTKit, PMGR) |
| `ttbat-phys-addr-base`, `gptbat-ready` | Present in ADT `/arm-io/sgx`; neither m1n1's T8103 path nor the driver reads them | Yes | Ignored |

## The kext

```
nix-build --extra-experimental-features nix-command --extra-experimental-features flakes build-kext.nix
nix-build --extra-experimental-features nix-command --extra-experimental-features flakes build-kc.nix
```

`build-kc.nix` imports `../m1-rust-kext/build-lkpi-kc.nix` unchanged and adds
`SFGpuDT.kext` to its link line. In the collection build log the kext's only
unresolved symbol is `dyld_stub_binder`, as for every kext there.

Boot arguments:

| Argument | Effect |
|---|---|
| `sfgpudt=0` | nothing |
| `sfgpudt=1` (default) | power GFX, read registers, build, log. No registry changes |
| `sfgpudt=2` | the same, then publish the nodes |
| `sfgpudt_power=0` | never write PMGR. Registers are read only if GFX is already on; otherwise 8 cores are assumed for the log and nothing is published |
| `sfgpudt_stringprops=1` | declares that the `of` layer honours `pd,string-props`; without it `sfgpudt=2` refuses to publish, because of the misread value below |

**The default writes one hardware register.** Order in `start()`:

1. Refuse anything but `/arm-io` compatible `arm-io,t8103`.
2. PMGR, as m1n1 `pmgr_adt_power_enable()`: for `/arm-io/sgx`, then
   `/arm-io/gfx-asc`, walk `clock-gates`; for each gate enable its parents
   first, then the device unless it is virtual. All three gates are virtual;
   their only real ancestor is GFX (device 99). The kext refuses to write any
   register other than GFX at 0x23b7003f8. It writes target ACTIVE, waits up
   to 10 ms for the actual state, logs before and after, and leaves GFX on.
3. With GFX active only: read the eight ID words at `sgx + 0xd04000`, the
   core masks at `+0xd01500` and `+0xd01514`, and `+0xd08024`.
4. Run the library, log its notes and the summary.
5. `sfgpudt=2`: create `gpu@206400000` (an `IOPlatformDevice`, the node the
   Linux platform driver binds to), `reserved-memory/...` and
   `opp-table-gpu/opp00..06` under the device-tree root, whose
   `#address-cells` and `#size-cells` are 2 and which has no `ranges`.
   Apple's `/arm-io/sgx` is not modified.

### Predicted log

`make test` prints the lines from `note` onward from the same library and
formatter. The `pmgr` and `sgx` lines are from the measurements in
`reports/sprint-010/gpu-power-and-id-probe-001.md`. If the guest is started
with GFX already on, both `pmgr GFX` lines read `0x0f0023ff -> 0x0f0023ff`
and the write count is 0.

```
SFGpuDT: start: sfgpudt=1 sfgpudt_power=1 sfgpudt_stringprops=0
SFGpuDT: pmgr GFX(99) at 0x23b7003f8: 0x0f002300 -> 0x0f0023ff
SFGpuDT: pmgr GFX(99) at 0x23b7003f8: 0x0f0023ff -> 0x0f0023ff
SFGpuDT: pmgr: 1 register write(s), GFX active
SFGpuDT: sgx 0x204d00000 id +0x4000: 04021100 00000000 00040a04 00000000 00030808 00040404 01320300 00101311
SFGpuDT: sgx core mask +0x1500 0x000000ff +0x1514 0x00000000; +0x8024 0x002fffee (ADT ttbat-phys-addr-base 0x002fffee)
SFGpuDT: note 7  (0x0, 0x0)
SFGpuDT: note 8  (0x0, 0x0)
SFGpuDT: summary v1: 82 properties fnv1a 0xe590189d; linux-comparable 70 properties fnv1a 0xb9740392
SFGpuDT: subtree gpu: 37 properties 0xc778b498; linux-comparable 35 properties 0x34e9249a
SFGpuDT: subtree opp-table-gpu: 23 properties 0xab1d4b63; linux-comparable 22 properties 0x004b5c25
SFGpuDT: subtree reserved-memory: 18 properties 0x9889d3e3; linux-comparable 9 properties 0x76fbfcfa
SFGpuDT: subtree chosen: 4 properties 0x0bd076a8; linux-comparable 4 properties 0x0bd076a8
SFGpuDT: firmware 13.5 version 13.5.0 compat 13.5.0
SFGpuDT: cores 8 mask 0xff (read) core-leak 0x447a0000 sram-leak 0x42340000
SFGpuDT: opp0 0 Hz 400000 uV 0 uW
SFGpuDT: opp1 396000000 Hz 606000 uV 3817800 uW
SFGpuDT: opp2 528000000 Hz 643000 uV 5144000 uW
SFGpuDT: opp3 720000000 Hz 693000 uV 7484400 uW
SFGpuDT: opp4 924000000 Hz 787000 uV 11805000 uW
SFGpuDT: opp5 1128000000 Hz 878000 uV 17384400 uW
SFGpuDT: opp6 1278000000 Hz 953000 uV 20013000 uW
SFGpuDT: region handoff 0xbfff70000 size 0x4000
SFGpuDT: region pagetables 0xbfff78000 size 0x40000
SFGpuDT: region ttbs 0xbfffb8000 size 0x4000
SFGpuDT: not publishing (sfgpudt=1)
```

Notes 7 and 8 say that `mboxes` and `power-domains` were left out. The
checksum is FNV-1a over, per property in table order: node path, NUL, name,
NUL, one length byte, the value in big-endian FDT form. "linux-comparable"
leaves out phandle-valued properties and the three disabled blob nodes'
`status`; `0xb9740392` is also what the host test computes from the Linux
ground-truth bytes. The "82 properties" checksum additionally depends on the
kext's phandle numbers (base `0x53460000`).

## The `of` layer

Byte order: `sf_linux_of.c` expects registry properties in the ADT's layout
(u32 cells little-endian, each 2-cell field of `reg` one native u64, text
unchanged) and converts back to big-endian FDT cells for the driver.
`gpudt_prop_to_iodt()` produces that layout; `gpudt_prop_iodt_roundtrips()`
models the conversion back.

1. **Text detection** (`kernel/sf_linux_of.patch`). The layer guesses text
   from the bytes. One value on this machine is misread: `opp01`
   `opp-microwatt` = 0x003a4148 is stored as `48 41 3a 00`, which passes for a
   string, so the driver would get 1212234240 uW instead of 3817800. The patch
   lets a node list its text properties in `pd,string-props`; the kext sets
   that list.
2. **Missing symbols.** The driver's T8103 path does not use the OPP library;
   it walks the table's children itself. Whole-word search of
   `sources/puredarwin-m1-desktop/src/Kernel/fbsdkpi/` and
   `integrations/siliconforest/m1-rust-kext/` finds no definition of the
   following, which `rust/kernel/of.rs`, `device/property.rs` and
   `platform.rs` bind:
   - implemented in `kernel/sf_linux_fwnode.c` (about 190 lines):
     `of_reserved_mem_region_to_resource_byname`,
     `of_reserved_mem_region_to_resource`, `__dev_fwnode`,
     `fwnode_property_read_u8/u16/u32/u64_array`, `fwnode_property_present`,
     `fwnode_property_read_bool`, `fwnode_property_read_string`,
     `fwnode_property_match_string`, `fwnode_property_get_reference_args`,
     `fwnode_get_name`, `fwnode_get_name_prefix`, `fwnode_count_parents`,
     `fwnode_get_nth_parent`, `fwnode_get_named_child_node`,
     `fwnode_get_next_child_node`, `fwnode_handle_get`, `fwnode_handle_put`,
     `of_root`, `of_chosen`, `of_aliases`, `of_stdout`, `of_node_is_root`.
     Needs `struct fwnode_handle fwnode;` in the layer's `struct device_node`.
   - not implemented, platform bus, a few lines each:
     `__platform_driver_register`, `platform_device_put`, `platform_bus_type`,
     `platform_get_irq_byname_optional`, `dev_is_platform` (declared, not
     defined).
3. No change, but relied on: children come back in attach order
   (`IORegistryEntry::makeLink` appends), and the driver takes the operating
   points in that order.

## Open items

- Boot the collection and compare the log with the prediction above.
- `sfgpudt=2` has never run: the published nodes, the `IOPlatformDevice` nub
  and its matching by a Linux platform driver are untested.
- `kernel/sf_linux_fwnode.c` and the text-detection patch have not been
  compiled against the layer's real headers.
- Whether the LinuxKPI `ioremap` can map the UAT regions, which are iBoot
  carve-outs outside XNU's managed memory, is untested.
- The kext leaves GFX on. Nothing turns it off again.
- The firmware table is a snapshot of m1n1's; an iBoot string newer than the
  snapshot yields `0 0 0` and the driver refuses. Regenerate with:
  `awk '/fw_versions\[NUM_FW_VERSIONS\] = \{/,/^};/' sources/m1n1-baseline/src/firmware.c | grep -E '^\s*\[V[0-9]'`
  and the `sed` expression that reshapes each row to `{iboot, string, {tuple}, length}`.
- The static values are compared with the ADT but the static ones are emitted,
  as on Linux. If a future ADT disagrees, the library reports it and still
  emits the static value; whether to prefer the ADT then is undecided.
