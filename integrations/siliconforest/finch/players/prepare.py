#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Copy a verified third-party player and our original movie into a fresh stage."""
import argparse,json,hashlib,shutil,runpy
from pathlib import Path


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--bundle',type=Path,required=True)
    ap.add_argument('--download',type=Path,required=True)
    ap.add_argument('--movie',type=Path,required=True)
    ap.add_argument('--stage',type=Path,required=True)
    ap.add_argument('--system-compat',type=Path,help='Reviewed own legacy errno-table facade output')
    ap.add_argument('--bundle-id',help='Private stage identity, to avoid reusing another staged instance')
    ap.add_argument('--entry-program',type=Path,help='VLC original main rebuilt on Linux with upstream root option')
    ap.add_argument('--hardware',action='store_true',help='Include actual VideoToolbox/CVPX modules for the hardware path')
    ap.add_argument('--minimal-headless',action='store_true',help='Explicit CLI-only module selection; no GUI/playback claim')
    a=ap.parse_args()
    known={r['name']:r for r in json.loads(Path(__file__).with_name('downloads.json').read_text())}
    row=known[a.download.name]
    if hashlib.file_digest(a.download.open('rb'),'sha256').hexdigest()!=row['sha256']:
        raise ValueError('Download does not match publisher checksum')
    if a.stage.exists():raise ValueError('Use a new stage')
    (a.stage/'Applications').mkdir(parents=True)
    shutil.copytree(a.bundle,a.stage/'Applications'/a.bundle.name,symlinks=True)
    if a.minimal_headless:
        selected={'dummy','avcodec','mp4','filesystem','packetizer_h264','packetizer_mpeg4audio',
                  'auhal','audio_format','float_mixer','samplerate','swscale','logger','console_logger',
                  'oldrc','vdummy','hotkeys','equalizer','scaletempo','subtitle','stream_out_transcode'}
        if a.hardware:selected.update({'videotoolbox','cvpx'})
        plugins=a.stage/'Applications/VLC.app/Contents/MacOS/plugins'
        disabled=a.stage/'disabled-plugins';disabled.mkdir()
        wanted={'lib'+name+'_plugin.dylib' for name in selected}
        if not wanted.issubset({p.name for p in plugins.iterdir()}):raise ValueError('Incomplete CLI plugin set')
        for path in plugins.iterdir():
            if path.name not in wanted:shutil.move(str(path),disabled/path.name)
        (a.stage/'plugin-selection.json').write_text(json.dumps({'mode':'headless only','enabled':sorted(wanted),
            'note':'Full verified original is untouched. Excluded copies retained outside the app plugin search.'},indent=2)+'\n')
    if a.system_compat:
        lib=a.stage/'Applications/VLC.app/Contents/MacOS/lib'
        shutil.copy2(a.system_compat/'lib/libSFVLCSystem.dylib',lib)
        macho=runpy.run_path(str(Path(__file__).resolve().parents[2]/'homebrew/hb_macho.py'))
        core=lib/'libvlccore.dylib'
        changed,count=macho['rewrite_names'](core.read_bytes(),lambda name:'@loader_path/libSFVLCSystem.dylib' if name=='/usr/lib/libSystem.B.dylib' else name)
        if count!=1:raise ValueError('Expected one libSystem load command in libvlccore')
        changed=macho['resign'](changed)[0]
        if macho['verify'](changed):raise ValueError('Invalid ad-hoc signature')
        core.write_bytes(changed)
    if a.entry_program:
        import plistlib
        app=a.stage/'Applications/VLC.app'
        # VLC's own Darwin config_GetLibDir recognizes an image ending in VLC.
        # Keep that suffix so its unchanged plugin/data discovery finds the app.
        executable=app/'Contents/MacOS/SiliconForest-VLC'
        shutil.copy2(a.entry_program/'bin/VLC-SiliconForest',executable)
        info=app/'Contents/Info.plist';shutil.copy2(info,info.with_name('Info.plist.vendor'))
        metadata=plistlib.loads(info.read_bytes());metadata['CFBundleExecutable']='SiliconForest-VLC'
        if a.bundle_id:metadata['CFBundleIdentifier']=a.bundle_id
        info.write_bytes(plistlib.dumps(metadata))
    (a.stage/'fixtures').mkdir();shutil.copy2(a.movie,a.stage/'fixtures/movie.mov')
    shutil.copy2(Path(__file__).with_name('launch.py'),a.stage)
    records=[]
    for p in sorted((a.stage/'Applications').rglob('*')):
        if p.is_file() and not p.is_symlink():
            records.append({'path':str(p.relative_to(a.stage)),'sha256':hashlib.file_digest(p.open('rb'),'sha256').hexdigest()})
    (a.stage/'bundle-hashes.json').write_text(json.dumps(records,indent=2)+'\n')
    (a.stage/'publisher.json').write_text(json.dumps(row,indent=2)+'\n')


if __name__=='__main__':main()
