# Owned default route, revision001

This original userspace adapter is the next step after the real M1 DNS/HTTP
qualification in `reports/sprint-010/network-egress-hardware-attempt-002.json`.
It supports a default route through one newly allocated utun (local10.0.2.15/32,
peer10.0.2.2), so DNS and ordinary remote IPv4 destinations need not be configured
as successive point-to-point peers. It is not yet linked into a guest program,
staged or executed. The current M1 remains at its last verified Linux state.

`native.h` exposes init/open/borrow-fd/close. Allocate its zero-initialized context
on the heap, never copy/reset/reinitialize it, serialize calls in one foreground
owner and provide bounded non-reentrant observation callbacks. Call close after
every open error. The caller must establish the same fresh, exclusive guest
network-management context as the qualified finite client. This is not a kernel
lease against another privileged administrator. Clients may not close, duplicate,
pass or reconfigure the borrowed descriptor.

Before changing anything, the adapter checks unscoped default absence and saves
the complete bounded IPv4 route configuration, refusing every scoped or unscoped
default. It creates and verifies a fresh utun and direct peer route, then emits
exactly one RTM_ADD with IFP/IFA/gateway bound to that owner. The successful ADD
ACK reflects request flags0x843; independent GET must report actual flags0x40010843,
matching interface index/name, localIFA, peer gateway and zero default mask. The
GLOBAL-bit branch was checked in the actual native50 KC bytes; no optional flag
fallback is used. No descriptor is exposed before that readback succeeds.

An unknown ADD outcome is always failure: no second ADD, no readback-as-success,
and no userspace RTM_DELETE. Cleanup closes only the owned utun. Pinned XNU
unplumbs the protocol and purges all UP routes whose actual ifp pointer equals
that interface, including static defaults and children. Address cleanup alone
is insufficient. The adapter waits for interface/peer/local-route disappearance,
then verifies default absence and exact baseline IPv4 route configuration. It
compares all address bytes/scopes, flags and indices, while deliberately ignoring
counters/metrics/PID/clocks. Unknown close, foreign replacement or any incomplete
observation remains a sticky failure; a failed close is not retried.

The request codec emits full16-byte `/0` netmasks, because a zero-length request
mask would terminate XNU's address parser before IFP/IFA. GET replies may instead
compress the mask to4zero bytes. Actual GET link addresses occupy at least20bytes,
while our reflected ADD ACK has a compact link address. Separate tests cover
those shapes, IPv4 reserved bytes, name/type/index, zero padding, truncation and
all single-bit mutations. The inherited direct-peer query encoder now accepts
GET only. No old blocked raw ADD/DELETE prototype is linked.

Limits are explicit: at most128 IPv4 routes in a64KiB dump; 1.5s routing exchange;
64unrelated messages; bounded asynchronous utun cleanup inherited from the frozen
interface helper. Syscall/allocation latency itself is not a hard deadline.
The callback sees exact request bytes and write outcomes separately: a request
record alone never proves a kernel write. Error/unknown ACK paths must be saved.

Host checks:

```
python3 integrations/puredarwin/m1-network-default-route/test_default.py
```

This executes portable ownership, loss/fault, route-inventory and actual extracted
native-exchange control flow with fake syscalls, plus compiled mutations. NDEBUG
is refused. The Nix build compiles actual Darwin SDK objects into an inert static
archive; it does not execute them. Native routing syscalls, default/clone cleanup,
ordinary libc getaddrinfo, resolver service readiness and persistent networking
remain runtime requirements. A future packet needs a new exact host profile,
source/artifact bindings, pre-capture criteria, a bounded owner and mandatory
verified Linux recovery. Preserve the prior failed prototypes and captures.

The next integration uses this owner with the qualified codec/console flow, then
the native `mdnsd -debug` and a checked RAM-only resolv.conf for real libc lookup.
No libc success stub, arbitrary alternate endpoint, NIC/Wi-Fi claim, GPU observer,
firmware operation, trackpad mode command, or m1n1 change is introduced here.
