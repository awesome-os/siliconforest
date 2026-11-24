#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Opt-in PD-device AU clock normalization; no server/CoreAudio changes.

Applied AFTER the selected-device patch. Preserve properties and default
behavior. HAL's audible-time model already includes fixed pipeline delay;
AU consumers such as cubeb add those advertised fixed properties themselves.
"""
from pathlib import Path
import sys
p=Path(sys.argv[1]);s=p.read_text()
def replace(old,new):
    global s
    if s.count(old)!=1:raise ValueError('PD AU source anchor changed: '+old[:90])
    s=s.replace(old,new)
replace('#include <string.h>','#include <string.h>\n#include <stdio.h>\n#include "SFOutputClock.h"')
replace('    Float64 sampleTime;','''    Float64 sampleTime;
    Boolean sfClockEnabled, sfOutputHostValid;
    uint64_t sfOutputHost, sfClockDeviceTicks, sfClockFixedTicks;
    double sfTicksPerDeviceFrame, sfTicksPerClientFrame, sfCycleClientBase;''')
anchor='OSStatus AudioUnitInitialize(AudioUnit u)'
helpers='''static void sfUpdateOutputClock(AudioUnit u)
{
    if (!u->sfClockEnabled) return;
    double ticks = (double)u->sfClockDeviceTicks + u->bufferFrames * u->sfTicksPerDeviceFrame;
    __atomic_store_n(&u->sfClockFixedTicks, ticks >= 0 && ticks < 9223372036854775808.0 ? (uint64_t)ticks : 0, __ATOMIC_RELEASE);
}

static void sfConfigureOutputClock(AudioUnit u)
{
    u->sfClockEnabled = false;
    const char *mode = getenv("SF_AU_NORMALIZE_HAL_CLOCK");
    if (!mode || strcmp(mode, "1")) return;
    // Only the installed PD /dev backend has this audited audible-time model.
    CFStringRef uid = NULL; UInt32 bytes = sizeof(uid);
    AudioObjectPropertyAddress a = { kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    if (AudioObjectGetPropertyData(u->outputDevice, &a, 0, NULL, &bytes, &uid) || bytes != sizeof(uid) || !uid) return;
    Boolean known = CFEqual(uid, CFSTR("SFAudio:speakers")) || CFEqual(uid, CFSTR("SFAudio:headphones"));
    CFRelease(uid);
    if (!known) return;
    UInt32 device = 0, stream = 0; bytes = sizeof(device);
    a.mSelector = kAudioDevicePropertyLatency; a.mScope = kAudioObjectPropertyScopeOutput;
    if (AudioObjectGetPropertyData(u->outputDevice, &a, 0, NULL, &bytes, &device) || bytes != sizeof(device)) return;
    AudioStreamID streams[32]; bytes = sizeof(streams); a.mSelector = kAudioDevicePropertyStreams;
    if (AudioObjectGetPropertyData(u->outputDevice, &a, 0, NULL, &bytes, streams) || bytes < sizeof(AudioStreamID) || bytes > sizeof(streams)) return;
    bytes = sizeof(stream); a.mSelector = kAudioStreamPropertyLatency; a.mScope = kAudioObjectPropertyScopeGlobal;
    if (AudioObjectGetPropertyData(streams[0], &a, 0, NULL, &bytes, &stream) || bytes != sizeof(stream)) return;
    mach_timebase_info_data_t tb = {0};
    if (mach_timebase_info(&tb) || !tb.numer || !tb.denom || !(u->device.mSampleRate > 0) || !(u->client.mSampleRate > 0)) return;
    double ticksPerSecond = 1e9 * tb.denom / tb.numer;
    u->sfTicksPerDeviceFrame = ticksPerSecond / u->device.mSampleRate;
    u->sfTicksPerClientFrame = ticksPerSecond / u->client.mSampleRate;
    double deviceTicks = ((uint64_t)device + stream) * u->sfTicksPerDeviceFrame;
    if (!(deviceTicks >= 0 && deviceTicks < 9223372036854775808.0)) return;
    u->sfClockDeviceTicks = (uint64_t)deviceTicks; u->sfClockEnabled = true;
    sfUpdateOutputClock(u);
    fprintf(stderr, "SF_AU_CLOCK normalized=1 device-fixed-frames=%llu unit-buffer-frames=%u properties-unchanged=1\\n", (unsigned long long)((uint64_t)device + stream), (unsigned)u->bufferFrames);
}

'''
replace(anchor,helpers+anchor)
replace('''    u->initialized = true;
    return noErr;
}

OSStatus AudioOutputUnitStop''','''    sfConfigureOutputClock(u);
    u->initialized = true;
    return noErr;
}

OSStatus AudioOutputUnitStop''')
replace('''    u->initialized = false;
    return noErr;''','''    u->sfClockEnabled = false;
    u->initialized = false;
    return noErr;''')
replace('''        else
            u->bufferFrames = value;
        break;''','''        else {
            u->bufferFrames = value;
            sfUpdateOutputClock(u);
        }
        break;''')
replace('''    ts.mHostTime = mach_absolute_time();''','''    uint64_t hostNow = mach_absolute_time();
    ts.mHostTime = u->sfClockEnabled ? sf_audio_callback_host(u->sfOutputHost, u->sfOutputHostValid,
        __atomic_load_n(&u->sfClockFixedTicks, __ATOMIC_ACQUIRE), u->sampleTime - u->sfCycleClientBase,
        u->sfTicksPerClientFrame, hostNow) : hostNow;''')
replace('''    (void)input;
    (void)inputTime;
    (void)outputTime;''','''    (void)input;
    (void)inputTime;
    u->sfOutputHost = outputTime ? outputTime->mHostTime : 0;
    u->sfOutputHostValid = outputTime && (outputTime->mFlags & kAudioTimeStampHostTimeValid);
    u->sfCycleClientBase = u->sampleTime;''')
p.write_text(s)
