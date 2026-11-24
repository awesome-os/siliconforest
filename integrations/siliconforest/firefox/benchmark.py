# SPDX-License-Identifier: MIT
"""Drive stock BrowserBench pages in this owned Firefox profile; retain failures."""
import json
import math
import time
from urllib.parse import urlsplit


def measure(client, kind, deadline, output, seat_activity=False):
    client.command('Marionette:SetContext', {'value': 'content'})
    found = None
    names = {'speedometer': 'Speedometer3.1', 'motionmark': 'MotionMark1.3', 'jetstream': 'JetStream2.2'}
    for handle in client.command('WebDriver:GetWindowHandles'):
        client.command('WebDriver:SwitchToWindow', {'handle': handle})
        url = client.command('WebDriver:GetCurrentURL')
        if url.startswith('https://browserbench.org/' + names[kind] + '/'):
            found = url
            break
    if found is None:
        raise RuntimeError('Owned stock benchmark tab not found')
    if urlsplit(found).query:
        raise RuntimeError('Stock benchmark runs require the unmodified URL without query parameters')
    result = dict(kind=kind, url=found, stock_workload=True, programmatic_start=True)
    if kind == 'motionmark':
        try:
            result['fullscreen'] = client.command('WebDriver:FullscreenWindow')
        except RuntimeError as error:
            result['fullscreen_error'] = str(error)
    script = '''const kind=arguments[0];
      if(kind==='speedometer'){
        if(!globalThis.benchmarkClient)return {ready:false};
        return {ready:true,done:!!benchmarkClient._hasResults,running:!!benchmarkClient._isRunning,
          score:document.querySelector('#result-number')?.textContent||null,
          confidence:document.querySelector('#confidence-number')?.textContent||null,
          progress:document.querySelector('#info-progress')?.textContent||null,
          metrics:benchmarkClient._hasResults?benchmarkClient.metrics:null};
      }
      if(kind==='motionmark'){
        if(!globalThis.benchmarkController)return {ready:false};
        const done=document.querySelector('#results')?.classList.contains('selected');
        const results=globalThis.benchmarkRunnerClient?.results;
        return {ready:!!document.querySelector('#start-button')&&!document.querySelector('#start-button').disabled,done:!!done,
          score:done?results?.score:null,lower:done?results?.scoreLowerBound:null,
          upper:done?results?.scoreUpperBound:null,
          progress:document.querySelector('#test-container')?.innerText.slice(0,120)||null};
      }
      if(!globalThis.JetStream)return {ready:false};
      const done=document.querySelector('#result-summary')?.classList.contains('done');
      const ready=!!document.querySelector('#status a.button')&&document.querySelectorAll('#results [id^=benchmark-]').length>0;
      return {ready,done:!!done,score:done?document.querySelector('#result-summary .score')?.textContent:null,
        progress:document.querySelector('#status')?.innerText.slice(0,200)||null,
        metrics:done?JetStream.resultsJSON():null};'''
    started = False
    admission = None
    next_activity = 0
    start = time.monotonic()
    while time.monotonic() < deadline - 8:
        state = client.command('WebDriver:ExecuteScript', {
            'script': script, 'args': [kind], 'newSandbox': False})
        result['elapsed'] = time.monotonic() - start
        result['state'] = state
        output.write_text(json.dumps(result, indent=2) + '\n')
        if state.get('ready') and not started:
            if seat_activity:
                from presentation_seat import admit
                admission = output.with_name('benchmark-seat.json')
                result['seat_admission'] = admit(client, admission, fixture=False)
            geometry = client.command('WebDriver:ExecuteScript', {
                'script': 'return {width:innerWidth,height:innerHeight,devicePixelRatio,visibility:document.visibilityState};',
                'args': [], 'newSandbox': False})
            result['viewport'] = geometry
            if geometry['visibility'] != 'visible' or geometry['width'] < 850 or geometry['height'] < 650:
                raise RuntimeError('Benchmark requires a visible viewport of at least850x650')
            action = {'speedometer': 'document.querySelector(".start-tests-button").click();',
                      'motionmark': 'document.querySelector("#start-button").click();',
                      'jetstream': 'document.querySelector("#status a.button").click();'}[kind]
            client.command('WebDriver:ExecuteScript', {
                'script': action, 'args': [], 'newSandbox': False})
            started = True
            next_activity = time.monotonic()+30
        elif started and state.get('done'):
            try:
                score = float(str(state.get('score')).replace(',', ''))
                if not math.isfinite(score) or score <= 0:
                    raise ValueError('Nonpositive or nonfinite score')
            except (ValueError, TypeError):
                result['error'] = 'Benchmark finished without a valid positive numeric score'
                output.write_text(json.dumps(result, indent=2) + '\n')
                return result
            result['score'] = score
            result['complete'] = True
            output.write_text(json.dumps(result, indent=2) + '\n')
            return result
        if started and admission is not None and time.monotonic() >= next_activity:
            from presentation_seat import pulse
            pulses = result.setdefault('synthetic_pointer_wake_pulses', [])
            pulses.append(pulse(admission, output.with_name('benchmark-pulse-%02d.json' % len(pulses))))
            next_activity = time.monotonic()+30
        time.sleep(3)
    result['error'] = 'Stock benchmark did not complete before the owned run deadline'
    output.write_text(json.dumps(result, indent=2) + '\n')
    return result
