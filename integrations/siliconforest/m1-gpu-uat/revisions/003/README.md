# UAT codec revision 003

Provenance-only refreeze of revision 002. `uat_codec.py` and `test_uat_codec.py` are byte-identical to `../002/`. The executable profile is unchanged: T8103 only, and a block walk is refused.

The three Linux reference entries now name repository `omacom/linux-aurora` and acquired commit `6a08a08e57558083f552733a5276d71b1a4d8ff6` from `reports/sprint-010/gpu-source-acquisition.json`. Their sha256 and git blob values are the ones already matched to those files. `bf62c0673ce64c48503739fadf0140db39796e8d` is the enclosing darwin-lab commit, not the source commit.

The numeric suite and its directory-bound freeze check stay in revision 002. Run `python3 -m unittest test_uat_codec.py` there.
