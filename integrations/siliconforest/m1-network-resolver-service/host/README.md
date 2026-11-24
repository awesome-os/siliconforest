# Isolated DL29 native-resolver host integration

This leaf and `runs/m1-desktop-010/net-resolver-001/lab` derive from the qualified
net-default-002 host, which remains unchanged. Only the isolated launcher and
continuing controller change; a new line helper is added. Recovery, bootstrap,
BootArgs translation, native input helpers and native50 kernel stay frozen.

The new host accepts only the exact typed `network_resolver_experiment` in
`host-binding-001.json`. Its only variable fields are a fresh lowercase48-hex
nonce and the owned loopback relay port. Version, integer and boolean types are
strict. Legacy network_link/egress/default metadata is refused; absence leaves
the preexisting native desktop modes unchanged. Resolver mode requires the
exact native50 KC, passive native child opt-in, bound RAM and helper, and no
GPU/survey/serial-HID or host input experiment. Both preflight and worker call
the same profile validator. It retains the existing fixed desktop command,
seals before writing it and permits only issued SPACE/LF data afterward.

The new helper changes DL28 markers to DL29, the exact BEGIN profiles, the
RESULT serializer and diagnostic accounting. DLNET001 bytes, direction/CRC,
128-frame cap, MTU1500, queue8, 131072 data bytes, <=192-byte credit chunks,
fixed3s ACK timeout, 1024 chunks and4096 ACKs remain unchanged. Wrapper lines
with this nonce are diagnostic only; they never grant readiness or success.
Old namespaces and unknown/wrong-nonce DL29 lines fail closed. Observation or
result errors cancel further writes while the console owner retains the tail.

RESULT matches the root net_resolver.c field order exactly. It requires positive
distinct owned child PIDs, resolver address count1..32, selected IPv4 integer,
resolver+HTTP success, daemon cleanup, interface and default-route cleanup,
complete actual frame/packet counts and credit, both CLOSEs, no partial bytes.
EOF remains independently required. Parsing that line establishes only the
reported guest result; final native resolver qualification also needs raw
probe/API order, daemon/endpoint ownership and cleanup, route baseline restore,
wire DNS/HTTP correlation, actual first SYN destination, and Linux recovery.

Root's raw child observations require exact per-kind caps: mdns_child_stderr
and probe_child_stderr65536, stdout8192; other kinds4096. Chunks remain64 raw
bytes, the aggregate is512KiB and8192 lines. Observations are retained as raw
console evidence, never treated as a success receipt by themselves.

Offline helper/integration and legacy fake-only suites live here. The explicit
public fixture uses the new DL29 helper and frozen real Linux slirp backend;
it emits source-fact synthetic packets/ACKs and never emits a fake Darwin RESULT.
Its historical `production_dl28_helper_*` report key spelling is retained from
the fixture template; the value is the new helper digest and all markers and
actual profiles are DL29/native-resolver-example-http-v1.

Run source verification with `python3 -B -I .../host/verify.py`. The normalized
binding lists every isolated file and the exact five host input source pins.
No hardware operation, SSH, boot, route/firewall change or target deployment
is performed by this leaf's validation scripts.
