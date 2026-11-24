# PDDARTSurvey: source-only J293 observation prototype

This original, opt-in IOService samples an existing T8103 `dart-apcie0` register
bank. It is **not an IOMapper, initialized DART, PCIe initializer or DMA test**.
The C core passed host tests; the C++ adapter has not been SDK-compiled or run.
Root owns any future integration, build, packet and hardware qualification.
Current kernels, packets, HID revisions and host sources are unchanged.

The only opt-in is `pddartsurvey=1`. Missing or other values do nothing. A
module-global atomic probe claim is never cleared, including after failure.
There are no retries, timers, interrupts, reset/power writes, firmware calls,
mapper properties, user clients, table walks or root adoption. Unload is refused.

## Exact profile and access order

`board.c` requires J293 / MacBookPro17,1 / `arm-io,t8103`, the exact `apcie,t8103`,
`dart,t8020`, `pmgr1,t8103` and AIC nodes, observed register tuple counts/sizes,
power/clock gate IDs, bus range, DART page size/aperture/options and interrupt
parent. It resolves PMGR devices through the actual `devices`, `ps-regs`, `reg`
and `ranges` tables. It rejects duplicate selected IDs, virtual domains,
different parents/addresses, ambiguous translations and all arithmetic overflow.
Already-physical APCIE addresses are resolved through identity ranges; the
arm-io base is not added to them a second time.

| Region | Exact base | ADT aperture | Mapped prefix |
|---|---:|---:|---:|
| PMGR reg0 | `0x23b700000` | `0x8c000` | `0x4000` |
| APCIE RC, reg1 | `0x680000000` | `0x4000` | `0x4000` |
| APCIE PHY, reg2 | `0x680080000` | `0x90000` | `0x4000` |
| APCIE port0, reg6 | `0x681000000` | `0x8000` | `0x4000` |
| dart-apcie0 | `0x681008000` | `0x4000` | `0x4000` |

Native mapping is lazy, inhibit-cache and `kIOMapReadOnly`, with input-only
descriptors and a `volatile const uint32_t` dereference between `dmb osh`
barriers. The sampler's interface contains only `read32`; it exposes no writer.
There is no DART mapping or read until a full guard succeeds. Each of the 88
DART reads gets a fresh guard, and one final guard is required for completion:

1. Read PMGR IMX `+0x1b8`, APCIE `+0x348`, APCIE_GP `+0x3e8`, PCIE_REF `+0x1a0`.
   Each must already have TARGET=ACTUAL=`0xf`. RESET, AUTO, PS_RESET, BUSY /
   parent-off and DISABLE are refused. Historical WAS-gated bits are allowed.
   A domain whose firmware policy automatically clock-gates it is conservatively
   `NOT_READY`; this survey never clears AUTO or changes that policy.
2. Read PHY CTRL `+0`: CLK0/1 REQ+ACK all set, T8103 reset bit7 clear.
   Read RC PHYIF `+0x24` RUN, port APPCLK `+0x800`, RESET_DIS `+0x814` and
   STATUS_RUN `+0x804`; require bit0 in each.
3. Read one permitted DART register. Unsupported page shift, CONFIG lock or
   enabled stream bits outside 0..15 stop the observation, retaining partial
   samples. A locked device is never adopted.

| DART offset | Observation | Source evidence |
|---|---|---|
| `0x00`, `0x04` | PARAMS1/2; require page shift14 | Linux apple-dart.c:63–67,3087–3093 |
| `0x60` | CONFIG; refuse LOCK15 | m1n1 dart.c:15–16,341 |
| `0xfc` | enabled stream bitmap | m1n1 dart8020.py:75,103 |
| `0x80..0x8c`, step4 | four stream remap words | m1n1 dart8020.py:71; Linux apple-dart.c:1433 |
| `0x100..0x13c`, step4 | 16 raw TCR words | m1n1 dart8020.py:77; Linux apple-dart.c:3309 |
| `0x200..0x2fc`, step4 | 16 × 4 raw TTBR words | m1n1 dart8020.py:78; dart.c:357–362 |

There are no reads of command, selector, fault/ack, unknown or DAPF registers.
This allowlist is for the normal APCIE T8020 instance only. The pinned Linux
source warns that tunneled PCIe-C DART parameter reads can fault; those nodes
and layouts are excluded. No values from the sampled roots become pointers.
The codec in sibling `m1-dart` is not called by this adapter.

Full observation is 889 callback reads (88 × 10 + final nine), capped at 1024.
Errors and unmet prerequisites stop immediately without retry or writes. The
public C result is `DLS_SAMPLED`, `DLS_NOT_READY`, `DLS_UNSUPPORTED`, or a negative
argument/board/I/O/limit failure. `DLS_SAMPLED` means only that the read sequence
completed. It does not prove a coherent snapshot, working translation or ownership.

Revision002 preserves the same copied OSData on the persistent IOResources
provider as **`PDDARTSurveyDiagnosticV1`**, as well as the existing instance's
`PDDARTSurveyObservationV1`. This happens for success, NOT_READY, UNSUPPORTED
and other completed refusal/error paths, before any failed-start detach/free.
Plain `setProperty` does not publish a resource, send a rematch notification or
retry anything. IOResources retains its copy independently of the survey's
lifetime. Maps have already been released when the copy is made. Neither stop
nor free removes the provider property; the single probe claim prevents later
attempts in this boot from replacing it.

Allocation failure, provider property failure and instance property failure are
logged separately as `diagnostic_copy`, `provider_preserved`, and
`instance_recorded`. None modifies the raw `snapshot.result` or the logged
observation result. Service start still fails on a refusal **or** failed copy
publication. A successful provider copy survives even if the instance property
fails. Missing provider data is missing evidence, never an implicit successful
sample. There is no preservation guarantee after an MMIO abort/hang or failure
before the board-validation/sampling path completes. IOLog and service presence
are insufficient substitutes for retrieving and validating the actual data.

The wire format remains version1, **764 bytes, alignment4**, native little endian
on ARM64. `observation_layout.h` checks every member offset and the complete
size at compile time in the adapter and the C/C++ host harnesses:

| Offset | Field | Interpretation |
|---:|---|---|
| 0 | u32 version | must be1 |
| 4 | u32 sample_count | successful DART sample prefix, 0..88 |
| 8 | u32 reads_attempted | includes guards and failed callback attempts, 0..1024 |
| 12,16 | u32 failure_region, failure_offset | last attempted/refused location; region5/offset0 on complete success |
| 20 | s32 result | raw observation status, independent of property publication |
| 24 | u32 power[4] | most recently stored values; may be stale or never read |
| 40 | u32 clocks[5] | most recently stored values; may be stale or never read |
| 60 | 88 × {u32 offset,u32 value} | only entries below sample_count were successful DART reads |

**Every snapshot is noncoherent, including successful ones.** Different samples
and power/clock words come from different times. On a guard refusal/error,
earlier slots may belong to the current guard, later slots to a previous guard,
or may remain zero from initialization. There is no per-word valid bitmap;
zero does not prove that a register was read or that hardware is off. The unused
sample tail is zero-initialized and is not evidence. A board rejection has no
register observations. Consumers must check length/version/counts/status and
retain these limits rather than treating this as a simultaneous hardware state.
Register values/root addresses are not printed in IOLog. Runtime observations
and any registry dump must remain private evidence.

## Source qualification and remaining limits

The pinned MIT m1n1 sources establish the offsets and power/clock sequence.
Linux source is a factual cross-check, not copied implementation. See
`PROVENANCE.json` for exact byte identities and `NOTICE-m1n1.txt` for its license.
Original-code licensing is pending under AGENTS.md.

The saved attempt011 guest ADT passed this guard. Its PMGR graph and pinned
`HV.map_essential` show no intersection between the four sampled domains and
UART0/USB0 or UART0/USB1 shadowed power domains. This is an offline check of
that saved graph and that exact source, not a live power observation. A changed
HV hook set must be reviewed before reuse; the kext cannot authenticate the HV.

Live prerequisites are deliberately not inferred from default-on ADT flags.
If PCIe was not initialized by its real owner, this survey should refuse. It
does not call m1n1 PCIe initialization or apply firmware tunables.

**Unresolved before a hardware trial:** SDK/KPI and linked-image qualification;
live handoff power/clock state; absence of concurrent external power owners;
the actual normal-DART read behavior; and bounded recovery for a synchronous
MMIO abort or hung bus. Rechecking guards cannot make the power-check/read race
atomic. A callback-count bound cannot interrupt an MMIO instruction. Source
evidence supports the restricted register selection but is not an architectural
guarantee that every selected read completes on this handoff. No automatic boot
or readiness claim follows from host tests.

Maps are owned only by synchronous `start`, released before any service
publication and released idempotently on failure/free (map before descriptor,
reverse order). There are no DMA pages, exported mappings, persistent producers
or changes to existing quarantine. Caller-controlled tests must serialize
the board/property callbacks and keep their bytes stable during validation.

A later mapper still needs requester/RID/SID ownership, real IOMapper and MSI
binding, retained physical table/data backing, cache/coherency and invalidation,
fault containment and safe retirement/quarantine. None is supplied here.

## Host checks

```sh
python3 integrations/puredarwin/m1-dart-survey/test_survey.py --mutations
# Optional, local private saved bytes only:
python3 integrations/puredarwin/m1-dart-survey/test_survey.py --mutations \
  --adt runs/m1-desktop-010/attempt-011/guest/guest-adt.bin
```

The C harness uses ASan/UBSan, refuses NDEBUG explicitly and tests the exact
negative diagnostic. It tests board/range/parent rejection, every permitted
offset, all 889 callback-error positions, 88 post-sample power losses, immutable
fake register banks and prerequisite failures with zero DART reads. Eight
compiling source mutants must fail assertions. An additional C++ host harness
compiles the publication body extracted verbatim from `PDDARTSurvey::start`
against explicit ownership fakes: 40 combinations of observation status and
allocation/provider/instance failure verify immutable copied data, preservation
after instance destruction, independent status/logging, and failure returns.
This is not an IOKit ABI test. Full native C++ checks remain source-shape checks
and do not substitute for root's future SDK compile.

The optional saved-ADT decoder is original, bounded and imports no m1n1 modules.
It validates the same production C board function in a separate ordinary host
shared library; that optional library is not the sanitized C harness. No raw
ADT or device identity strings are copied into the public fixture or receipt.

Revision001's complete files and receipt are preserved byte-for-byte under
`revisions/001`. Revision002 changes diagnostic publication, layout assertions,
tests and documentation only. `board.c`, `survey.c`, and `survey.h` remain
byte-identical to revision001; no sampling, guard, allowlist or MMIO behavior
changes.
