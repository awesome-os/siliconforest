#!/bin/sh
# SPDX-License-Identifier: MIT
# Runs ON THE M1: a private copy of the installed Firefox launcher for FXPERF's own instance only.
# Same runtime directory and providers. Extra (FXPERF instance only):
#   FXPERF_MANIFEST=<json under /private/var/db/sffxperf/bin>  read instead of the runtime's release.json
#   SFFXPERF_FRAMEWORKS / SFFXPERF_LIBS  colon lists put in front of the DYLD paths (own prefix or runtime overrides)
set -e
D=/private/var/db/sffxperf/bin
R=/usr/local/lib/siliconforest/firefox/160-system1
mkdir -p $D
cp /Applications/Firefox.app/Contents/MacOS/firefox-sf $D/firefox-perf
printf '{"target": "%s"}\n' $R > $D/system-runtime.json
python3 - <<'PY'
import json
p='/private/var/db/sffxperf/bin/firefox-perf'
s=open(p).read()
s=s.replace("(root / 'release.json').read_text()", "(Path(os.environ['FXPERF_MANIFEST']) if os.environ.get('FXPERF_MANIFEST','').startswith('/private/var/db/sffxperf/bin/') else root / 'release.json').read_text()")
assert s.count("FXPERF_MANIFEST")==4, s.count("FXPERF_MANIFEST")
old="    observer = env.get('SFFX_DIAGNOSTIC_OBSERVER')"
add='''    for var, key in [('SFFXPERF_FRAMEWORKS', 'DYLD_FRAMEWORK_PATH'), ('SFFXPERF_LIBS', 'DYLD_LIBRARY_PATH')]:
        extra = os.environ.get(var)
        if extra:
            if any(not (x.startswith('/private/var/db/sffxperf/') or x.startswith(str(root / 'overrides') + '/')) for x in extra.split(':')):
                raise ValueError('FXPERF overrides must live in its own prefix')
            env[key] = extra + ':' + env[key]
    if os.environ.get('SFFXPERF_INSERT', '').startswith('/private/var/db/sffxperf/lib/'):
        env['DYLD_INSERT_LIBRARIES'] += ':' + os.environ['SFFXPERF_INSERT']
'''
assert s.count(old)==1
s=s.replace(old, add+old)
open(p,'w').write(s)
R='/usr/local/lib/siliconforest/firefox/160-system1'
new=json.load(open(R+'/release.json')); old=json.load(open(R+'/maintenance-system-ui-v1/release.json'))
m=dict(new); m['system_ui']=False; m.pop('coretext_source',None)
m['coretext_override']=old['coretext_override']; m['onyx_override']=old['onyx_override']
json.dump(m, open('/private/var/db/sffxperf/bin/release-private-gpu.json','w'), indent=1)
PY
chmod 755 $D/firefox-perf
echo '--- installed plan'; $D/firefox-perf --sf-plan -no-remote --profile /private/var/db/sffxperf/p1 | grep -c System/Library/Frameworks
echo '--- private-gpu plan'; FXPERF_MANIFEST=$D/release-private-gpu.json $D/firefox-perf --sf-plan -no-remote --profile /private/var/db/sffxperf/p1 | grep DYLD_FRAMEWORK
