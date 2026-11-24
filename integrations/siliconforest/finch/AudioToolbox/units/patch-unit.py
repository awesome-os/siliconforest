#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Retain PD's HAL/converter implementation; bind AUHAL to a real output ID."""
from pathlib import Path
import sys
p=Path(sys.argv[1]);s=p.read_text()
def change(old,new,count=1):
 global s
 assert s.count(old)==count,(old,s.count(old));s=s.replace(old,new)
change('    AudioComponent component;','    AudioComponent component;\n    AudioDeviceID outputDevice;')
change('static AudioStreamBasicDescription deviceFormat(void)','static AudioStreamBasicDescription deviceFormat(AudioDeviceID device)')
change('AudioObjectGetPropertyData(kSFAudioOutputDevice, &a, 0, NULL, &size, &f);','AudioObjectGetPropertyData(device, &a, 0, NULL, &size, &f);')
# Every remaining alias use is inside a unit operation, not a process-global
# output change. Factories and all operations remain in this one PD provider.
assert s.count('kSFAudioOutputDevice')==6
s=s.replace('kSFAudioOutputDevice','u->outputDevice').replace('deviceFormat()','deviceFormat(u->outputDevice)')
s=s.replace('SFAudioOutputDeviceIsPresent()','outputDeviceReady(u->outputDevice)')
helper='''static Boolean outputDeviceReady(AudioDeviceID device)
{
    if (device == kSFAudioOutputDevice) return SFAudioOutputDeviceIsPresent();
    UInt32 alive = 0, size = sizeof(alive);
    AudioObjectPropertyAddress a = { kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    if (AudioObjectGetPropertyData(device, &a, 0, NULL, &size, &alive) || size != sizeof(alive) || !alive) return false;
    AudioStreamBasicDescription format = {0}; size = sizeof(format);
    a.mSelector = kAudioDevicePropertyStreamFormat; a.mScope = kAudioObjectPropertyScopeOutput;
    return AudioObjectGetPropertyData(device, &a, 0, NULL, &size, &format) == noErr &&
        size == sizeof(format) && SFAudioPCMFormatIsSupported(&format);
}

'''
anchor='OSStatus AudioComponentInstanceNew('
assert s.count(anchor)==1;s=s.replace(anchor,helper+anchor)
change('    u->component = component;','    u->component = component;\n    u->outputDevice = kSFAudioOutputDevice;')
old='''    case kAudioOutputUnitProperty_CurrentDevice:
        if (value != u->outputDevice || !outputDeviceReady(u->outputDevice))
            err = kAudioUnitErr_InvalidPropertyValue;
        break;'''
new='''    case kAudioOutputUnitProperty_CurrentDevice:
        if (u->initialized) err = kAudioUnitErr_Initialized;
        else if (!outputDeviceReady(value)) err = kAudioUnitErr_InvalidPropertyValue;
        else {
            u->outputDevice = value;
            AudioObjectPropertyAddress a = { kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
            UInt32 frames = 0, size = sizeof(frames);
            if (AudioObjectGetPropertyData(value, &a, 0, NULL, &size, &frames) == noErr && frames && frames <= MAX_SLICE)
                u->bufferFrames = frames;
        }
        break;'''
change(old,new)
# Describe the selected device, including its rate, instead of the default's.
s=s.replace('scope == kAudioUnitScope_Input ? u->client.mSampleRate : SFAudioOutputSampleRate()',
            'scope == kAudioUnitScope_Input ? u->client.mSampleRate : deviceFormat(u->outputDevice).mSampleRate')
s=s.replace('(Float64)u->bufferFrames / SFAudioOutputSampleRate()',
            '(Float64)u->bufferFrames / deviceFormat(u->outputDevice).mSampleRate')
p.write_text(s)
