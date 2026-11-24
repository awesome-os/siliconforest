# Portable guest DNS/HTTP validation prerequisite 001

Original C validators for the frozen `m1-network-egress` example-http-v1 profile.
No socket, route, file/device I/O, allocation, clock, process, tty or platform
syscall exists in the library. Root's guest/controller001/002/003 and frozen
transport/backend files are unchanged. This is source and host testing only;
no SDK build, network request, SSH, M1/M5, hardware or GPU action occurs here.

`egress_validate.h` is the complete API. Callers use disjoint valid spans and
real C types. The library decodes wire bytes explicitly; result structs are
ordinary in-process C values, never serialized ABI layouts.

- `eg_dns_query(id,...)` creates the exact29-byte canonical example.com A IN
  question with RD. The caller supplies its fresh transaction ID.
- `eg_dns_reply(query,...,actual_source_ipv4,actual_source_port,...)` checks
  that exact outbound query, actual recvfrom peer10.0.2.3:53, response ID/
  question/flags/counts,<=512 bytes, bounded backward compression/name steps,
  CNAME ownership/cycles/ambiguity,<=4 final global A records and TTLs. Return
  candidates in original wire order; select only `ipv4[0]`, with
  `lease_seconds=min(relevant TTLs,60)`. Zero TTL or one invalid selected-owner
  A refuses the entire answer; unrelated glue grants nothing. Output is cleared
  on error, preventing stale authorization. The caller still owns its UDP local
  port/socket correlation, retries/duplicate policy,10s reply deadline, lease
  clock, address routing and one-endpoint/no-fallback rule.
- `eg_http_request(nonce,48,...)` builds exactly186 bytes, without a NUL: the
  frozen GET/Host/User-Agent/Accept-Encoding/X-Darwin-Lab-Nonce/Connection request.
  Require48 lowercase hex bytes; no arbitrary host/path/method/body argument.
  Builders return zero written and leave output unchanged on failure.
- `eg_http_response(wire,n,eof,...)` validates one accumulated **ordered TCP
  socket** response,<=65536 bytes. It accepts only status200, exact text/html,
  identity content, unique valid headers, unambiguous Content-Length/chunked/
  EOF framing and decoded body containing `Example Domain`. Reject overflow,
  extra bytes, truncated chunks/body, extensions/trailers and framing ambiguity.
  Matching content across chunk boundaries is supported without allocating a
  decoded body. Header delimiter start is<=8192 (total header<=8196), exactly
  matching the frozen backend's implementation and its boundary test.

`EG_OK` requires actual EOF for HTTP. Even complete CL/chunked framing returns
`EG_MORE` before EOF, with `framing_complete=1` as a separate observation.
Only `recv()==0` establishes EOF; timeout, deadline, EAGAIN or a closed local
socket must not substitute. Close-delimited bodies cannot reveal a missing
unadvertised suffix: true EOF plus the bounded content/framing checks is the
profile's convention. The caller must send all186 request bytes, preserve raw
response/counts, check actual socket outcome and perform owned cleanup.
An error is terminal for that attempt: do not retry a shorter/filtered span or
reuse DNS authorization. Stateless validation is bounded per call; avoid
revalidating on every individual byte when accumulating ordinary read chunks.

IPv4 global policy is an explicit numeric range predicate matching the frozen
backend on Python3.14.7, including shared100.64/10, multicast/reserved ranges and
192.0.0.9/.10 exceptions. It is not a live IANA registry or arbitrary future
Python-version claim. The table was derived from numeric local source facts;
no Python or GPL implementation code was copied. `source-inputs.json` records
the exact stdlib and backend hashes; tests compare10088 boundary/random values.

Host verification, using a separately owned temporary binary path:

```
clang -std=c11 -Wall -Wextra -Werror -Wpedantic -O1 -g \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  egress_validate.c test_egress_validate.c -o /ABS/OWNED/test_egress_validate
/ABS/OWNED/test_egress_validate validation/fixtures
python3 -I test_backend_equivalence.py --binary /ABS/OWNED/test_egress_validate
```

The sanitizer run passes2508 assertions, including every truncated prefix of
framed fixtures, wrong peers/IDs/questions, compression/cycle/name253/254 limits,
CNAME7/8-hop boundary, TTL/record/global-address rejection, output clearing,
maximum header/wire, integer/chunk-count limits and exact request bytes.
Differential checks pass3422 DNS/HTTP cases and10088 global-address cases against
the frozen backend. Saved Linux DNS/HTTP bytes replay as first172.66.147.243,
TTL60, two candidates,931 response bytes/342 header bytes/577 decoded body bytes,
and exact186-byte request. This is replay of previously recorded Linux bytes,
not new network or Darwin execution. Neither plaintext HTTP nor DNS authenticates
the server through TLS/DNSSEC.

All files in this new codec directory are original source/tests or explicitly
identified saved host fixtures. License selection for original project code
remains pending. Next work requires independent source review, a pinned SDK
build and root-owned real guest sockets/routes/pump/capture/recovery; no runtime
or physical-network success is claimed by these host parser results.
