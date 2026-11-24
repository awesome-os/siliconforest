# SPDX-License-Identifier: MIT
# Prefix-only full Finch family + normalized PD child. No canonical installer.
let e=import ../environment.nix;
 family=import ../AudioToolbox/install.nix;
 unit=import ./clock-provider.nix;
in e.pkgs.runCommand "siliconforest-firefox-audio-clock-family-arm64" {nativeBuildInputs=[e.pkgs.python3];} ''
 mkdir -p $out/Frameworks
 cp -R ${family}/System/Library/Frameworks/AudioToolbox.framework $out/Frameworks/
 chmod -R u+w $out
 A=$out/Frameworks/AudioToolbox.framework/Versions/A
 cp ${unit}/System/Library/Frameworks/AudioToolbox.framework/Versions/A/AudioToolbox $A/AudioToolbox.output-sf
 chmod u+w $A/AudioToolbox.output-sf
 python3 - "$A/AudioToolbox.output-sf" <<'PY'
 import runpy,sys
 from pathlib import Path
 api=runpy.run_path('${../../homebrew/hb_macho.py}')
 p=Path(sys.argv[1]);before=p.read_bytes();image=api['MachO'](before)
 old=image.id['name']
 body,changed=api['rewrite_names'](before,lambda name:'@loader_path/AudioToolbox.output-sf' if name==old else name)
 assert changed
 body=api['resign'](body)[0];assert not api['verify'](body)
 p.write_bytes(body)
 PY
 mkdir -p $A/Resources/Source/NormalizedOutputClock
 cp ${unit.src}/AudioUnit.c ${unit.src}/SFOutputClock.h $A/Resources/Source/NormalizedOutputClock/
 cat > $out/providers.json <<'JSON'
 ${builtins.toJSON {AudioToolbox=toString family;retainedPD=toString unit;CoreAudio="Installed canonical retained; never staged";optIn="SF_AU_NORMALIZE_HAL_CLOCK=1, private prefix only until Firefox/queue/codec qualification";status="Built only, no automatic activation/install";}}
 JSON
''
