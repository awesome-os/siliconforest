# SPDX-License-Identifier: MIT
"""Bounded compositor pacing A/B using its owner's existing inject interface.

Caller owns gui.lock and this browser. Restore the observed automatic pacing
mode in finally. A mode switch resets learned pacing history; no persisted
configuration, owner appearance setting or compositor process is changed.
"""
from pathlib import Path
import json
import re
import time
from compositor_stats import start,stop,flush_command


def state():
    admission=start()
    return stop(admission)


def measure(client,snapshot,elapsed,directory,scroll,seconds,modes):
    admission=state()
    descriptions=[line for line in admission['lines'] if 'pacer=' in line]
    if not descriptions:raise RuntimeError('Compositor pacing state absent')
    previous=re.search(r'pacer=(late-latch|off)\b',descriptions[-1])
    if not previous or '(fixed)' in descriptions[-1]:
        raise RuntimeError('Only an observed automatic pacing mode can be restored')
    previous='on' if previous[1]=='late-latch' else 'off'
    result=dict(admission=admission,previous=previous,phases=[],restored=False,
        method='Finite runtime latch sequence; each toggle resets learned pacing history. Original observed automatic mode restored in finally. No persistent configuration, renderer replacement or restart.')
    output=Path(directory)/'compositor-pacing.json'
    try:
        for index,mode in enumerate(modes):
            if index:
                from presentation_seat import pulse
                result.setdefault('wake_pulses',[]).append(pulse(Path(directory)/'presentation-seat.json',Path(directory)/('pacing-wake-%d.json'%index)))
            flush_command('latch '+mode)
            time.sleep(.5)
            clock=start()
            phase=dict(pacing=mode,fixture_cpu_before=dict(elapsed=elapsed(),ps=snapshot()))
            result['phases'].append(phase)
            phase['fixture_measurement']=client.measure_fixture(scroll,seconds*1000)
            phase['fixture_cpu_after']=dict(elapsed=elapsed(),ps=snapshot())
            phase['compositor_stats']=stop(clock)
            phase['mode_observed']=any('pacer='+('late-latch' if mode=='on' else 'off') in line for line in phase['compositor_stats']['lines'])
            output.write_text(json.dumps(result,indent=2)+'\n')
    finally:
        flush_command('latch '+previous)
        result['restoration']=state()
        result['restored']=any('pacer='+('late-latch' if previous=='on' else 'off') in line for line in result['restoration']['lines'])
        result['complete']=result['restored'] and len(result['phases'])==len(modes) and all(phase.get('mode_observed') and phase.get('fixture_measurement',{}).get('hash')=='e4bc0ce1' and phase['fixture_measurement'].get('glError')==0 for phase in result['phases'])
        output.write_text(json.dumps(result,indent=2)+'\n')
    first=result['phases'][0]
    return dict(first,fixture_repetitions=result['phases'],compositor_pacing=result)
