# Fixed native platform-facts reader

This original source-only reader uses the genuine C++17 IOKit shared-pointer
API and the frozen platform-facts decoder. Its production entry point is
`npr_read_native(expected,workspace,out)`. It reads only `/chosen` in gIODTPlane
and fixed `dram-base`, `dram-size`, `darwin-lab-platform-facts` properties. It
contains no registry publisher, mapping, physical access, hardware reader,
producer, owner configuration or conversion to bpl_policy. No driver, kext or
packet is built. Nothing here admits hardware execution.

The native entry retains the chosen entry and all three OSObject properties in
local OSSharedPtr instances, checks OSData class, obtains bounded borrowed bytes,
and resets every partially acquired reference on every core exit path. The pure
coordinator copies exact8-byte DRAM properties and1280..9472 facts bytes into a
caller-owned off-stack workspace. It invokes the unchanged facts and MCC decoders,
then commits caller output once after validation and cleanup. It never publishes
borrowed pointers or retained objects. The fake-adapter tests count synthetic
references; the genuine shared-pointer implementation is compiled, not executed.

PE_state is an explicit Unsupported.arm64 dependency. Only the source-proven
40-byte BootArgs common prefix is copied. No Video field, tail or sizeof(boot_args)
copy is used. Common Revision/Version must be2/2, physical base and memory size
must exactly match the decoded envelope, and chosen DRAM must match decoded DRAM.
The declared retained boot range must start at actual physBase and cover the
nonempty `[physBase,topOfKernelData)` prefix. Its boundaries are already validated
and aligned by the facts decoder. No extra topOfKernelData alignment assumption
is imposed. Coverage equality cannot establish that memory is actually retained.

Revision2/Version2 and these40bytes are shared by legacy736 and native1152
layouts. A positive legacy-common-prefix test deliberately demonstrates that this
reader does not authenticate the applied wire layout. Expected identities and
final native-wire digest are trusted caller declarations, never recomputed from
BootArgs here. Neither labels nor matching hashes authenticate an untrusted
producer. Four-plane/four-TZ MCC coverage remains limited; global exclusions,
policy authority, caller source provenance and actual native handoff are unproved.

A trusted boot context MUST provide serialization/quiescence and known accessible
immutable PE_state/prefix and registry OSData storage for the whole operation.
Retaining OSData preserves object lifetime, not immutable backing storage. Repeated
pointer/length/byte comparisons detect bounded changes in tests; they cannot make
concurrent mutation/reallocation safe. Callback checks are not a security or
quiescence barrier. The native source is gated to ARM64 Darwin KERNEL compilation,
with no default or TEST-SDK success stub. The internal named-source callback seam
is trusted implementation/test code, not a public alternate registry or raw-address
configuration mechanism.

All expected/ops/workspace/output object spans and borrowed source spans are
checked for forbidden overlap before copying. Caller output remains unchanged on
failure, including cleanup refusal. Workspace contents are unspecified after any
call and must stay off the kernel stack. Accessible immutable pointers and caller
serialization remain obligations; a pointer check proves neither accessibility
nor ownership. There is no retry.

Run `python3 validate.py --output-dir /private/scratch/path` for host-only tests.
Sanitizers exercise missing sources, partial acquisition, callback failures,
class-error seam responses, short/legacy80/oversize data, changed source views,
identity changes, envelope/boot mismatches, cleanup errors, aliases and atomic
failure. Eighteen compiled/executed semantic mutants must fail assertions. An early
envelope-base mutant survived a redundant first test; a case keeping actual phys
and declared boot base consistent while only envelope base differed fixed that
coverage gap before freeze. The original source guard was already present.

The genuine ARM64 audit compiles four TUs at O0/O2 and partial-links the native
reader, pure coordinator, frozen facts and frozen MCC code. Import/export and
actual standalone-kernel symbol presence are audited separately; virtual method
presence and successful partial linking do not establish runtime ABI behavior.
No object or native kernel API is executed. Tool wrapper hashes are not a claim
of a freshly built complete transitive toolchain. Hardware STOP remains unchanged.
