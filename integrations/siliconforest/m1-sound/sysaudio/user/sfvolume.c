/* SPDX-License-Identifier: MIT */
/*
 * sfvolume: the level of the default output device, through CoreAudio (and so coreaudiod).
 *
 *   sfvolume get | set <0..100> | up | down | mute | unmute | toggle-mute
 *
 * up and down move by one sixteenth of the range, as the volume keys do. Every command ends
 * by printing the state: "sfvolume: <device> volume <0..100> muted <0|1>". Exit status 1 when
 * there is no coreaudiod or no output device, 2 for a bad command line.
 */
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STEP (1.0f / 16.0f)

static const AudioObjectPropertyAddress kVolume = { kAudioDevicePropertyVolumeScalar, kAudioObjectPropertyScopeOutput,
                                                    kAudioObjectPropertyElementMain };
static const AudioObjectPropertyAddress kMute = { kAudioDevicePropertyMute, kAudioObjectPropertyScopeOutput,
                                                  kAudioObjectPropertyElementMain };

static int
usage(void)
{
    fprintf(stderr, "usage: sfvolume get | set <0..100> | up | down | mute | unmute | toggle-mute\n");
    return 2;
}

int
main(int argc, char **argv)
{
    AudioObjectPropertyAddress a = { kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain };
    AudioDeviceID device = kAudioObjectUnknown;
    UInt32 size = sizeof(device), mute = 0;
    Float32 volume = 0;
    CFStringRef cfName = NULL;
    char name[64] = "?";
    const char *cmd = argc > 1 ? argv[1] : "get";
    OSStatus err = noErr;

    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &size, &device) != noErr ||
        device == kAudioObjectUnknown) {
        fprintf(stderr, "sfvolume: no output device (is coreaudiod running?)\n");
        return 1;
    }
    size = sizeof(volume);
    if (AudioObjectGetPropertyData(device, &kVolume, 0, NULL, &size, &volume) != noErr) {
        fprintf(stderr, "sfvolume: the output device has no volume\n");
        return 1;
    }
    size = sizeof(mute);
    AudioObjectGetPropertyData(device, &kMute, 0, NULL, &size, &mute);

    if (!strcmp(cmd, "set") && argc == 3) {
        char *end;
        long v = strtol(argv[2], &end, 10);

        if (*end || end == argv[2] || v < 0 || v > 100)
            return usage();
        volume = (Float32)v / 100.0f;
        err = AudioObjectSetPropertyData(device, &kVolume, 0, NULL, sizeof(volume), &volume);
    } else if ((!strcmp(cmd, "up") || !strcmp(cmd, "down")) && argc == 2) {
        volume += cmd[0] == 'u' ? STEP : -STEP;
        volume = volume < 0 ? 0 : volume > 1 ? 1 : volume;
        err = AudioObjectSetPropertyData(device, &kVolume, 0, NULL, sizeof(volume), &volume);
        /* a volume key is also how one leaves the mute */
        if (err == noErr && mute && cmd[0] == 'u') {
            mute = 0;
            err = AudioObjectSetPropertyData(device, &kMute, 0, NULL, sizeof(mute), &mute);
        }
    } else if ((!strcmp(cmd, "mute") || !strcmp(cmd, "unmute") || !strcmp(cmd, "toggle-mute")) && argc == 2) {
        mute = cmd[0] == 't' ? !mute : cmd[0] == 'm';
        err = AudioObjectSetPropertyData(device, &kMute, 0, NULL, sizeof(mute), &mute);
    } else if (strcmp(cmd, "get") || argc > 2) {
        return usage();
    }
    if (err != noErr) {
        fprintf(stderr, "sfvolume: %s failed (%d)\n", cmd, (int)err);
        return 1;
    }

    /* read back: what the server holds now */
    size = sizeof(volume);
    AudioObjectGetPropertyData(device, &kVolume, 0, NULL, &size, &volume);
    size = sizeof(mute);
    AudioObjectGetPropertyData(device, &kMute, 0, NULL, &size, &mute);
    a.mSelector = kAudioObjectPropertyName;
    size = sizeof(cfName);
    if (AudioObjectGetPropertyData(device, &a, 0, NULL, &size, &cfName) == noErr && cfName) {
        CFStringGetCString(cfName, name, sizeof(name), kCFStringEncodingUTF8);
        CFRelease(cfName);
    }
    printf("sfvolume: %s volume %d muted %u\n", name, (int)(volume * 100.0f + 0.5f), (unsigned)mute);
    return 0;
}
