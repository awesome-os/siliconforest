# SPDX-License-Identifier: MIT
"""Admission for requested native held-picture controls, not a memory benchmark."""
import re


def qualify(record, env):
    text = record.get('stdout', '')
    error = record.get('stderr', '')
    checks = {'program_exit0': record.get('returncode') == 0,
              'no_capture_failure': 'CAPTURE_FAIL' not in text + error}
    phases = re.findall(r'^CAPTURE_POOL phase=([a-z-]+) (.+)$', text, re.M)
    if env.get('SF_CAPTURE_HOLD_FINAL') == '1':
        checks['all_three_snapshots'] = [p[0] for p in phases] == [
            'before-invalidate', 'after-invalidate-held', 'after-picture-release']
        checks['held_picture_survives'] = text.count(
            'CAPTURE_INVALIDATED held-visible-pixels-stable=1 GPU-handle-alive=1 ') == 1
    if env.get('SF_VT_RETIRE_POOL_AT_INVALIDATE') == '1':
        retired = re.findall(r'^SF_VT_POOL_RETIRED detached-frames=(\d+) gpu-storage-holds-unchanged=1$', error, re.M)
        checks['retirement_executed'] = len(retired) == 1 and int(retired[0]) > 0
    if env.get('SF_CAPTURE_GPU_FINAL') == '1':
        provider = re.findall(r'^CAPTURE_GPU_PROVIDER (.+)$', text, re.M)
        checks['qualified_graphics_provider'] = provider == [env.get('SF_CAPTURE_EXPECT_GL')]
        gpu = re.findall(r'^CAPTURE_GPU_FINAL hash=([a-f0-9]{32}) cpu-upload=0 cpu-readback=0 gpu-blit-bytes=(\d+) after-invalidate-and-picture-release=1 verification-readback=1$', text, re.M)
        frames = re.findall(r'^SFAVD_FRAME \d+ pts -?\d+ \d+x\d+ ([a-f0-9]{32}) ', text, re.M)
        checks['actual_gpu_copy_survives_release'] = bool(
            len(gpu) == 1 and frames and gpu[0][0] == frames[-1] and int(gpu[0][1]) > 0)
    return dict(status='PASS' if all(checks.values()) else 'FAIL', checks=checks,
                observed_global_snapshots=dict(phases),
                limits='Picture lifetime/GPU-copy correctness only. Global snapshots may include other contexts; no per-context memory attribution, browser/panel FPS, performance or direct-alias fence proof.')
