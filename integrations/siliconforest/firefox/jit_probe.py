# SPDX-License-Identifier: MIT
"""Observe actual JIT implementation frames in Firefox's own Gecko profiler."""
import gzip
import json


def measure(client, output):
    def command(script, context='chrome', async_=False):
        client.command('Marionette:SetContext', {'value': context})
        return client.command('WebDriver:ExecuteAsyncScript' if async_ else 'WebDriver:ExecuteScript', {
            'script': script, 'args': [], 'newSandbox': False, 'scriptTimeout': 22000})

    command('''const done=arguments[arguments.length-1];
      Services.profiler.StartProfiler(500000,1,["js"],["GeckoMain"],0)
        .then(()=>done({started:true}),e=>done({error:String(e)}));''', async_=True)
    try:
        client.command('Marionette:SetContext', {'value': 'content'})
        for handle in client.command('WebDriver:GetWindowHandles'):
            client.command('WebDriver:SwitchToWindow', {'handle': handle})
            if command('return typeof window.fxMeasureFrames==="function";', 'content'):
                break
        exercise = command('''const done=arguments[arguments.length-1];
          const source=document.createElement('script');
          source.textContent=`window.fxJitExercise=new Promise(resolve=>{
            const data=new Int32Array(1024);for(let i=0;i<data.length;i++)data[i]=i*17;
            function fxJitArithmetic(data,seed){
              for(let i=0;i<200000;i++)seed=(Math.imul(seed^data[i&1023],1664525)+1013904223)|0;
              return seed;
            }
            let checksum=1,calls=0,start=performance.now();
            function chunk(){
              for(let n=0;n<15;n++){checksum=fxJitArithmetic(data,checksum);calls++;}
              if(performance.now()-start<8000)setTimeout(chunk,0);
              else resolve({checksum,calls,elapsed:performance.now()-start});
            }chunk();
          });`;
          document.documentElement.append(source);
          window.fxJitExercise.then(done,e=>done({error:String(e)}));''', 'content', True)
        profile = command('''const done=arguments[arguments.length-1];
          Services.profiler.getProfileDataAsync().then(done,e=>done({error:String(e)}));''', async_=True)
    finally:
        command('''const done=arguments[arguments.length-1];
          Services.profiler.StopProfiler().then(()=>done(true),e=>done({error:String(e)}));''', async_=True)
    with gzip.open(output.with_name('jit-gecko-profile.json.gz'), 'wt') as stream:
        json.dump(profile, stream)
    result = summarize(profile, exercise)
    output.write_text(json.dumps(result, indent=2) + '\n')
    return result


def summarize(profile, exercise=None):
    """Trailing null columns are omitted in Gecko's sparse frame rows."""
    result = dict(exercise=exercise, implementations={}, own_arithmetic_frames=[],
                  profile_error=profile.get('error'), invalid_rows=0)

    def walk(process):
        for thread in process.get('threads', []):
            table = thread.get('frameTable', {})
            schema = table.get('schema', {})
            strings = thread.get('stringTable', [])
            if 'implementation' not in schema or 'location' not in schema:
                continue
            for row in table.get('data', []):
                index = row[schema['implementation']] if len(row) > schema['implementation'] else None
                if index is None:
                    continue
                if not isinstance(index, int) or not 0 <= index < len(strings):
                    result['invalid_rows'] += 1
                    continue
                implementation = strings[index]
                result['implementations'][implementation] = result['implementations'].get(implementation, 0) + 1
                location_index = row[schema['location']] if len(row) > schema['location'] else None
                if not isinstance(location_index, int) or not 0 <= location_index < len(strings):
                    result['invalid_rows'] += 1
                    continue
                location = strings[location_index]
                if 'fxJitArithmetic' in location:
                    result['own_arithmetic_frames'].append(dict(pid=thread.get('pid'), thread=thread.get('name'),
                        implementation=implementation, location=location))
        for child in process.get('processes', []):
            walk(child)
    walk(profile)
    tiers = [name.lower() for name in result['implementations']]
    result['baseline_observed'] = any('baseline' in name for name in tiers)
    result['ion_observed_in_own_function'] = any('ion' in frame['implementation'].lower()
                                              for frame in result['own_arithmetic_frames'])
    result['complete'] = result['baseline_observed'] and result['ion_observed_in_own_function'] and result['invalid_rows'] == 0
    return result
