#!/bin/sh
# SPDX-License-Identifier: MIT
# Runs ON THE M1: a private copy of the installed Firefox launcher for FXPERF2's own instance only. Same runtime and
# providers; SFFXPERF2_FRAMEWORKS / SFFXPERF2_LIBS (colon lists inside /private/var/db/sffxperf2 only) go in front of
# the plan's DYLD_FRAMEWORK_PATH / DYLD_LIBRARY_PATH, so a candidate framework is measured in the real browser.
set -e
D=/private/var/db/sffxperf2/bin
R=/usr/local/lib/siliconforest/firefox/160-system1
mkdir -p $D
cp /Applications/Firefox.app/Contents/MacOS/firefox-sf $D/firefox-perf2
printf '{"target": "%s"}\n' $R > $D/system-runtime.json
python3 - <<'PY'
p='/private/var/db/sffxperf2/bin/firefox-perf2'
s=open(p).read()
old="    command, env = plan(root, arguments[1:] if dry else arguments)\n"
add='''    for var, key in [('SFFXPERF2_FRAMEWORKS', 'DYLD_FRAMEWORK_PATH'), ('SFFXPERF2_LIBS', 'DYLD_LIBRARY_PATH')]:
        extra = os.environ.get(var)
        if extra:
            if any(not x.startswith('/private/var/db/sffxperf2/') for x in extra.split(':')):
                raise ValueError('FXPERF2 overrides must live in its own prefix')
            env[key] = extra + ':' + env[key]
'''
assert s.count(old)==1
s=s.replace(old, old+add)
open(p,'w').write(s)
PY
chmod 755 $D/firefox-perf2
echo '--- plan'; SFFXPERF2_FRAMEWORKS=/private/var/db/sffxperf2/cand/Frameworks SFFXPERF2_LIBS=/private/var/db/sffxperf2/cand/lib $D/firefox-perf2 --sf-plan -no-remote --profile /private/var/db/sffxperf2/p1 | grep DYLD
