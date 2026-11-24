#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Own-profile media comparison using the installed runtime's graphics plan.

No installed selector, owner profile, graphics framework or broker is changed.
Only the explicitly packaged media frameworks precede the selected providers.
"""
import importlib.util
import json
import os
from pathlib import Path
import sys


def main():
    root=Path(__file__).resolve().parent
    if os.uname().sysname!='Darwin' or not root.is_relative_to(Path('/private/var/db/sffx')):
        raise ValueError('Only a cold private SiliconForest packet may launch')
    spec=importlib.util.spec_from_file_location('media_daily',root/'tools/daily-launch.py')
    daily=importlib.util.module_from_spec(spec);spec.loader.exec_module(daily)
    profile=daily.selected_profile(sys.argv[1:])
    if profile is None or not profile.is_relative_to(root) or '--no-remote' not in sys.argv:
        raise ValueError('Explicit owned no-remote profile required')
    graphics=os.environ.get('SF_MEDIA_GRAPHICS_OVERRIDE')
    frame_cgl=os.environ.get('SF_MEDIA_FRAME_CGL')
    qc_control=os.environ.get('SF_MEDIA_QC_NV12')
    graphics_only=frame_cgl is not None or qc_control is not None
    frameworks=root/'Frameworks';names=set()
    if qc_control is not None:
        if graphics or frame_cgl or frameworks.exists() or Path(qc_control)!=root/'qc-nv12':
            raise ValueError('Private QC comparison must retain installed media/graphics plan')
        for file in ['lib/QuartzCore.pd','Frameworks/OpenGL.framework/Versions/A/OpenGL','identity.json']:
            if not (Path(qc_control)/file).is_file():raise ValueError('Cold private QC cohort incomplete')
    elif graphics_only:
        if graphics or frameworks.exists() or Path(frame_cgl)!=root/'frame-id-cgl/Frameworks':
            raise ValueError('Frame CGL comparison must use its packaged CGL and retain installed media')
        if not (Path(frame_cgl)/'OpenGL.framework/Versions/A/OpenGL').is_file():
            raise ValueError('Cold-packaged private marker CGL is absent')
    else:
        names={p.name for p in frameworks.iterdir()}
        if not names or names-{'CoreVideo.framework','VideoToolbox.framework','AudioToolbox.framework'}:
            raise ValueError('Only reviewed media frameworks may be selected')
    runtime=Path('/usr/local/lib/siliconforest/firefox/160-system1')
    manifest=json.loads((runtime/'release.json').read_text())
    if graphics:
        # The FX plan validates and selects framework, GLAPI and broker as one
        # cohort. This is a copy of the manifest, never an installed mutation.
        manifest['graphics_override']=graphics
        manifest['system_gpu_providers']=False
    command,env=daily.plan(runtime,sys.argv[1:],manifest)
    # plan() deliberately has no profile writes. Match the installed main()'s
    # platform/layout defaults in this already admitted OWN profile as well.
    # Its helpers preserve explicit test choices and unrelated bytes.
    settings=profile/'user.js'
    platform_preferences=daily.update_platform_preferences(settings)
    compositor_preferences=daily.update_compositor_preference(
        settings,daily.native_compositor_preference(manifest,os.environ))
    codec_preferences=daily.update_codec_preference(settings,daily.av1_preference(manifest,os.environ))
    if qc_control is not None:
        env['DYLD_FRAMEWORK_PATH']=str(Path(qc_control)/'Frameworks')+':'+env['DYLD_FRAMEWORK_PATH']
        env['DYLD_LIBRARY_PATH']=str(Path(qc_control)/'lib')+':'+env['DYLD_LIBRARY_PATH']
    elif not graphics_only:env['DYLD_FRAMEWORK_PATH']=str(frameworks)+':'+env['DYLD_FRAMEWORK_PATH']
    else:env['DYLD_FRAMEWORK_PATH']=frame_cgl+':'+env['DYLD_FRAMEWORK_PATH']
    (profile.parent/'private-media-launch.json').write_text(json.dumps(dict(
        command=command,frameworks=sorted(names),installed_outer_launcher=False,
        installed_runtime_plan=True,owner_profile=False,
        platform_preferences=platform_preferences,compositor_preferences=compositor_preferences,
        codec_preferences=codec_preferences,retained_installed_media=graphics_only,
        graphics_override=graphics,
        frame_identity_cgl=frame_cgl,
        private_quartzcore_copy=qc_control,
        environment={k:v for k,v in env.items() if k.startswith(('DYLD_','SF_VT_','SF_AU_')) or k in ['SFFX_FRAME_ID_GEOMETRY','SFFX_FRAME_ID_DIMENSIONS','SF_CA_NV12_GPU_COPY']}),indent=2)+'\n')
    # Match the installed launcher: relative bundle resources must be resolved
    # from the real Mozilla executable directory, not our diagnostic wrapper.
    os.chdir(Path(manifest.get('original_bundle',str(runtime/'original/Firefox.app')))/'Contents/MacOS')
    os.execvpe(command[0],command,env)


if __name__=='__main__':main()
