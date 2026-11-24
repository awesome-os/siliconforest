# Shared-shell return qualification helper

Host-only reader for one copied closed capture. It pins criteria `9768f316`, export parser `4e845eef`, and packet review `e67b200b` before it reads a capture. It does not edit those files, boot a machine, or turn a text grid into a window result.

`python3 qualify.py` prints the pins and stays at `PREPARED_AWAITING_CAPTURE`.

`python3 qualify.py --capture DIRECTORY --attestation FILE` reviews a copied tree. `REQUIRED.json` names the files. A refused export is not decoded into a pass. Application wait `Some(0)` is separate from a PTY `raw_wait_status` of 9 or 15. The report keeps `qualification` at `NOT_QUALIFIED` because framebuffer glyphs and two live windows are outside this reader.

Run `PYTHONDONTWRITEBYTECODE=1 python3 test_qualify.py` from this directory.
