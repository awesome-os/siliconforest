#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Own non-GUI asset/reader/writer declarations from Finch implementations."""
from pathlib import Path
import subprocess,sys,runpy,re
here=Path(__file__).resolve().parent
classes={'AVAssetTrackSegment':'NSObject','AVAssetTrackGroup':'NSObject','AVAssetTrack':'NSObject','AVAsset':'NSObject','AVURLAsset':'AVAsset','AVMetadataItem':'NSObject','AVMutableMetadataItem':'AVMetadataItem','AVMetadataItemFilter':'NSObject','AVMetadataItemValueRequest':'NSObject','AVMetadataGroup':'NSObject','AVTimedMetadataGroup':'AVMetadataGroup','AVMutableTimedMetadataGroup':'AVTimedMetadataGroup','AVDateRangeMetadataGroup':'AVMetadataGroup','AVMutableDateRangeMetadataGroup':'AVDateRangeMetadataGroup','AVMediaSelectionOption':'NSObject','AVMediaSelectionGroup':'NSObject','AVMediaSelection':'NSObject','AVMutableMediaSelection':'AVMediaSelection','AVAssetReaderOutput':'NSObject','AVAssetReaderTrackOutput':'AVAssetReaderOutput','AVAssetReaderSampleReferenceOutput':'AVAssetReaderOutput','AVAssetReader':'NSObject','AVAssetWriterInput':'NSObject','AVAssetWriterInputPixelBufferAdaptor':'NSObject','AVAssetWriter':'NSObject','AVSampleCursor':'NSObject','AVAssetWriterInputGroup':'AVMediaSelectionGroup'}
out=here/'include/AVFoundation/AVFoundation.h'
subprocess.run([sys.executable,str(here.parent/'tools/declarations.py'),sys.argv[1],str(here/'types.h'),str(out),'--class-map',','.join(k+':'+v for k,v in classes.items()),'--categories'],check=True)
s=out.read_text()
s=re.sub(r'^[-+] \([^)]*\)\s*_\w+[^;]*;\n', '', s, flags=re.M)
s=s.replace('@interface AVAssetTrackSegment : NSObject\n','@interface AVAssetTrackSegment : NSObject { CMTimeMapping _timeMapping; }\n')
for name in classes:
 start=s.index('@interface '+name+' :');end=s.index('@end',start);body=s[start:end]
 for method in re.findall(r'- \(BOOL\)(is[A-Z]\w*)\s*;',body):
  prop=method[2].lower()+method[3:];body+='@property(readonly,getter='+method+') BOOL '+prop+';\n'
 s=s[:start]+body+s[end:]
s+='\n@interface AVCompositionTrack : AVAssetTrack\n@end\n'
# Public Foundation categories implemented by Finch's AVTime.m.
fn=runpy.run_path(str(here.parent/'tools/declarations.py'))['declarations']
cats={'NSValue':[],'NSCoder':[]};fn((Path(sys.argv[1])/'AVTime.m').read_text(),cats,True)
for n,methods in cats.items():s+='\n@interface '+n+' (SiliconForestAVTime)\n'+'\n'.join(methods)+'\n@end\n'
out.write_text(s)
for n in list(classes)+['AVTime']:(here/'include/AVFoundation'/(n+'.h')).write_text('/* SPDX-License-Identifier: MIT */\n#pragma once\n#import <AVFoundation/AVFoundation.h>\n')
