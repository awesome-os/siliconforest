# darwin-desktop-records

Read-only exporter for one private Shell diagnostics directory. Host source and tests only. This is not a runtime approval, a desktop qualification, or a Nix build.

The installed binary name is `darwin-desktop-records`. The command is exactly:

`--directory ABS --nonce HEX48 --kind session|application --phase live|closed`

`HEX48` is 48 lowercase hex digits. `ABS` starts with `/` and has no `.` or `..` segment.

`session` reads only `session-result.txt` (16384 bytes) and `manager-status.log` (65536). `application` reads only `application-stdout.log` and `application-stderr.log` (8192 each). Both files are required in `live` and `closed`. `live` may be a prefix (`file_bytes_after` greater than the captured size). `closed` requires the size to stay unchanged. A missing, symlinked, hard-linked, non-regular, wrongly owned, or wrongly sized file fails closed.

The directory is opened with `O_DIRECTORY|O_NOFOLLOW` and must be mode `0700` and owned by the euid. Each file is `openat` `O_RDONLY|O_NOFOLLOW|O_NONBLOCK`, mode `0600`, one link, and a regular file. The program checks that the file descriptor and the directory close successfully. Stdout is the only write. It does not scan the directory, open a device or registry, or spawn.

Each wire line uses the `DL22_RECORD_` prefix and includes the nonce and kind. `decode(raw, nonce, kind, phase)` accepts one contiguous block after root partitions the supervisor boundaries. A line that is not a record for that nonce and kind is an error. The other kind is foreign. Carriage returns on the wire are normalized; file bytes are not stripped.

Derived from the reviewed collector in `integrations/puredarwin/m1-userspace-diagnostics/terminal-records`. That collector remains the exporter for its six terminal files. Root supplies the supervisor and the Nix producer.

Host check, from this directory: `PYTHONDONTWRITEBYTECODE=1 python3 test_records.py`
