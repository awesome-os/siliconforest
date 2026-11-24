# SPDX-License-Identifier: MIT
"""Record native/JS/IPC Gecko evidence for the owned browser family only.

Profiling runs are diagnostic; their frame/CPU numbers do not qualify speed.
"""
import json


def command(client, source, arguments=None):
    client.command('Marionette:SetContext', {'value': 'chrome'})
    return client.command('WebDriver:ExecuteAsyncScript', dict(
        script=source,args=arguments or [],newSandbox=False,scriptTimeout=45000))


def start(client, media=False):
    result=command(client, '''const done=arguments[arguments.length-1];
      if(Services.profiler.IsActive()){done({error:"Profiler already active"});return;}
      const available=Services.profiler.GetFeatures();
      const features=["js","stackwalk","leaf","cpu","threads","ipcmessages"]
        .filter(value=>available.includes(value));
      const filters=["GeckoMain","Compositor","Renderer","RenderBackend","Vsync",
        "WRWorker","IPC","IPDL","Canvas","Video","StyleThread","DOM Worker","JS Helper"];
      if(arguments[0])filters.push("Media","ImageBridge","Audio","RDD");
      Services.profiler.StartProfiler(2000000,1,features,filters,0).then(
        ()=>done({started:true,available,features,filters,configuration:Services.profiler.activeConfiguration}),
        error=>done({error:String(error)}));''',[media])
    if not result.get('started'):
        raise RuntimeError(str(result))
    return result


def stop(client, directory, admission):
    result=command(client, '''const path=arguments[0],done=arguments[arguments.length-1];
      (async()=>{try{
        await Services.profiler.Pause();
        await Services.profiler.dumpProfileToFileAsync(path);
        done({complete:true,path});
      }catch(error){done({complete:false,error:String(error)});}
      finally{await Services.profiler.StopProfiler();}})();''',
      [str(directory/'pipeline-gecko-profile.json')])
    result.update(admission=admission,diagnostic_only=True,timing_qualification=False)
    (directory/'pipeline-profile.json').write_text(json.dumps(result,indent=2)+'\n')
    return result
