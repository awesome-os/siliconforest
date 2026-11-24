#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Select Finch's actual non-GUI no-device capture implementations."""
from pathlib import Path
import re
import sys

source, output = map(Path, sys.argv[1:])
text = source.read_text()
blocks = dict(re.findall(r'(@implementation\s+(\w+).*?\n@end)', text, re.S))
# Keep the source license with every extracted substantial implementation.
parts = [text.splitlines()[0], '/* Selected from Finch2d9b1bb AVCapture.m; no preview/photo/file-output code. */',
         '#import "capture.h"',
         'static NSError *capture_error(NSInteger code) { return [NSError errorWithDomain:AVFoundationErrorDomain code:code userInfo:nil]; }']
# The complete device implementation contains unneeded hardware-mode properties;
# only its actual no-device discovery methods are needed by libavdevice.
device = next(body for body, name in blocks.items() if name == 'AVCaptureDevice')
parts.append(device[:device.index('- (BOOL)hasMediaType:')]+'@end')
for name in ['AVCaptureDeviceDiscoverySession', 'AVCaptureDeviceInput', 'AVCaptureOutput',
             'AVCaptureVideoDataOutput', 'AVCaptureAudioDataOutput', 'AVCaptureSession']:
    body = next(body for body, cls in blocks.items() if cls == name)
    if name == 'AVCaptureSession':
        # Upstream marks an empty session running. Report the actual unavailable
        # input backend instead, with the public runtime-error notification.
        start = body.index('- (void)startRunning\n')
        end = body.index('- (void)stopRunning\n', start)
        body = body[:start]+'''- (void)startRunning
{
    _running = NO;
    [[NSNotificationCenter defaultCenter] postNotificationName:AVCaptureSessionRuntimeErrorNotification
        object:self userInfo:@{AVCaptureSessionErrorKey:capture_error(AVErrorDeviceNotConnected)}];
}
'''+body[end:]
    parts.append(body)
parts += ['/* SPDX-License-Identifier: MIT */',
          '/* Own unavailable base/screen input, not a capture implementation. */',
          '@implementation AVCaptureInput\n@end',
          '@implementation AVCaptureScreenInput',
          '- (instancetype)init { return nil; }',
          '- (instancetype)initWithDisplayID:(uint32_t)display { (void)display; return nil; }',
          '@end']
output.write_text('\n\n'.join(parts)+'\n')
