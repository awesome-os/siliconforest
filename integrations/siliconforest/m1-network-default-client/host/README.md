# DL28 host prerequisite

This isolated opt-in uses `network_default_experiment`, profile `example-default-http-ack-v1`, with the exact typed public profile plus `gateway: 10.0.2.2`. It requires the native child prerequisite and rejects old network experiment fields, mixed input/GPU/survey experiments, unknown/missing/duplicate keys and boolean substitutions. The constructor checks the complete profile before serial open. Absence leaves the existing sealed console path.

The production helper is copied from frozen DL27 and uses DL28 markers. There is one interface phase and no DNS boundary. RESULT appends `default_add_attempted=1 route_error=0 route_stage=5`, requires phases1, interface cleanup/stage7, bounded HTTP, exact credit and parsed frame counts. RESULT may precede relay EOF; final transport completion independently requires both CLOSEs, full credit, half-close and actual EOF. Report version4 distinguishes the selected default profile from unchanged backend `example-http-ack-v2`. Raw bounded DL28_OBSERVE lines are diagnostic evidence only.

The 192-byte SPACE/LF chunks, cumulative ACK checks, 3s outstanding deadline, 75s helper bound and 131072-byte/1024-chunk/4096-ACK caps are unchanged. The external backend profile remains frozen public002 with 30s idle, 15s CLOSE and 90s actual active budget. No backend destination policy is broadened.

Replay from the repository root:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -B -m unittest discover -s integrations/puredarwin/m1-network-default-client/host -p 'test_*.py' -v
python3 -B integrations/puredarwin/m1-network-default-client/host/test_public_pipeline.py --library /tmp/dlnet-codec-001-STkBll/libdlnet-host.so --output /new/private/dl28-run --allow-public-example-http
```

The pipeline requires the hash-bound real host C codec, frozen backend, QEMU and external DNS/one fixed example.com GET. It emits synthetic guest packets/ACKs and never emits a Darwin RESULT or claims a real default route, interface, resolver, M1 or GPU result. Unit fixtures with RESULT markers are synthetic parser tests only. Existing console/bootstrap/recovery/controller source files stay unchanged; the new isolated lab changes only helper binding and typed host dispatch. Root owns SDK/packet/target execution and cleanup.
