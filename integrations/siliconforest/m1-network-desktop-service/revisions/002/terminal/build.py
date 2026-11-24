#!/usr/bin/env python3
"""Offline pinned SDK build in a new local directory; never executes target code."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[5]
RUN=ROOT/'runs/m1-desktop-010/desktop-network-terminal-sdk-003'
RUN.mkdir(parents=True,exist_ok=False)
revision=RUN/'revision';source=revision/'terminal'
shutil.copytree(HERE,source,ignore=shutil.ignore_patterns('target*','__pycache__','source-validation*.json'))
shutil.copy2(HERE.parent/'startup.env',revision/'startup.env')
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
inputs={str(p.relative_to(source)):sha(p) for p in source.rglob('*') if p.is_file()}
env={'CARGO_TARGET_DIR':str(RUN/'target'),'CARGO_TARGET_AARCH64_APPLE_DARWIN_LINKER':str(source/'darwin-linker.py'),
'CARGO_TARGET_AARCH64_APPLE_DARWIN_RUSTFLAGS':'-L native=/nix/store/5shniy0j07mkrl7kys7vslm9vh02lw6d-puredarwin-libxkbcommon-nox-1.13.1/lib','CARGO_BUILD_JOBS':'2'}
command=['cargo','build','--offline','--locked','--release','--manifest-path',str(source/'Cargo.toml'),'--target','aarch64-apple-darwin']
(RUN/'command.json').write_text(json.dumps({'argv':command,'environment':env,'source_sha256':inputs,'startup_env_sha256':sha(revision/'startup.env')},indent=2)+'\n')
with (RUN/'build.log').open('xb') as log:result=subprocess.run(command,env=dict(os.environ,**env),stdout=log,stderr=subprocess.STDOUT)
print(json.dumps({'returncode':result.returncode,'run':str(RUN),'binary':str(RUN/'target/aarch64-apple-darwin/release/chonk-desktop-network-terminal')}))
raise SystemExit(result.returncode)
