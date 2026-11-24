# Closed qualifier revision002

This distinct revision preserves frozen001bafc657b. It changes only the location
binding and trailing-CR line-ending normalization needed by the actual saved
console path. Repeated CR bytes immediately before LF are stripped for OBS and
DL30_RESULT syntax; interior CR bytes remain errors. Raw console bytes and their
SHA256 are unchanged. Tests cover CR1/2/3, interior CR, foreign nonce and all
previous content/failure predicates. No warning filtering or packet relaxation.

Entry: `python3 -B revisions/002/qualify.py --console PATH --nonce HEX48` from
`evidence/`. Public `qualify` and `observe_records` APIs are unchanged.
The full contract and scope remain the preserved `../../CLOSED_README.md`.
Backend correspondence remains NOT_EVALUATED; no overall runtime PASS claim.
Run this directory's `check_closed.py`. Its own receipt excludes the root-owned
DNS qualifier and the preserved001 sources/receipt. No hardware actions.
