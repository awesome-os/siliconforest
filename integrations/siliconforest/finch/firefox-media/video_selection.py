# SPDX-License-Identifier: MIT
"""Exact installed video-family admission; provider bytes are verified cold separately."""
from pathlib import Path

RUNTIME = Path('/usr/local/lib/siliconforest/firefox/160-system1')
FAMILY = RUNTIME / 'overrides/video-shared-v1'
CLOCK_FAMILY = RUNTIME / 'overrides/video-clock-0172-v1'
VIDEO_FAMILIES = {
    str(FAMILY / 'Frameworks'): 'adf505101dc83759acafa2aa1f7d6310',
    str(CLOCK_FAMILY / 'Frameworks'): '9fb0e7eb4cfa35abb18f2c9f21b5afde',
}


def installed_family(manifest):
    expected = {
        'target': str(RUNTIME), 'system_layout': True, 'system_ui': True,
        'system_gpu_providers': True,
        'video_zero_copy': True, 'audio_output_clock': True,
        'audio_override': str(RUNTIME / 'overrides/audio-clock-v1/Frameworks'),
        'network_override': str(RUNTIME / 'overrides/network-v1/Frameworks'),
    }
    if any(type(manifest.get(k)) is not type(v) or manifest.get(k) != v
           for k, v in expected.items()):
        raise ValueError('Expected selected video family with qualified system graphics, network and audio clock')
    path=manifest.get('video_override')
    if not isinstance(path,str) or path not in VIDEO_FAMILIES:
        raise ValueError('Video family is outside the exact qualified candidates')
    if manifest.get('graphics_override') or manifest.get('diagnostics'):
        raise ValueError('Installed video gate cannot introduce a private graphics cohort or installation diagnostics')
    return Path(path)
