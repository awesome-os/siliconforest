# shared-desktop export blocks

Host parser for one console span. It returns byte payloads and hashes. Root owns desktop criteria, log and pixel interpretation, and the supervisor freeze.

```python
decode_exports(raw, host_nonce, instances, phase)
```

`host_nonce` is 48 lowercase hex digits. `instances` is two distinct ids of the same form, in caller order. `phase` is `live` or `closed`. Titles and the supervisor's counter are not inputs; pass the ids in.

The result is `session` plus `applications`. `session` and each `logs` value are the frozen DL22 maps of `data`, `sha256`, and `file_bytes_after`. `applications` is a tuple of `instance`, `terminal`, and `logs` in the caller's order. `terminal` is the frozen DL11 map.

The span is one `DL23_SESSION_EXPORT_BEGIN` / `DL23_SESSION_EXPORT_END` pair, then for each instance `DL23_APP_EXPORT_BEGIN`, `DL23_APP_TERMINAL_EXPORT_END`, and `DL23_APP_EXPORT_END`. Every `rc`, `terminal`, and `logs` field is `0`. The phase token on each begin line is the `phase` argument. A foreign nonce, unknown instance, duplicate, reorder, or malformed `DL23_*EXPORT*` line raises `ValueError`.

Carriage returns are removed. Each interior is then the exact normalized bytes between its wrappers. Session interiors go to the frozen `DL22` session decoder with the host nonce. Terminal interiors go to the frozen `DL11` decoder with that instance id. Application interiors go to the frozen `DL22` application decoder with that instance id. A line in an interior that is not a record for that block raises. Live terminal results contain the four required files. Closed terminal results contain all six, each with `file_bytes_after` equal to the captured size.

Other console lines may sit before the first begin marker and after the last app end marker. Record lines there raise. The desktop `failed=` footer is not returned.

A missing or malformed span raises. The caller marks dependent checks not evaluated.

Host check, from this directory: `PYTHONDONTWRITEBYTECODE=1 python3 test_exports.py`
