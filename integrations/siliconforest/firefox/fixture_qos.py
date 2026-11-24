# SPDX-License-Identifier: MIT
"""Change only the owned browser main thread for an explicit paired diagnostic."""


def set_main(client, library, mode):
    requested={'default':0x15,'initiated':0x19,'interactive':0x21}[mode]
    client.command('Marionette:SetContext', {'value':'chrome'})
    try:
        result=client.command('WebDriver:ExecuteScript', dict(script='''
          if(Services.appinfo.processType!==Ci.nsIXULRuntime.PROCESS_TYPE_DEFAULT)
            throw Error("Not the browser parent process");
          const {ctypes}=ChromeUtils.importESModule("resource://gre/modules/ctypes.sys.mjs");
          const library=ctypes.open(arguments[0]);
          try {const values=ctypes.uint64_t.array(4)();
            const change=library.declare("SFFXSetMainQoS",ctypes.default_abi,
              ctypes.int,ctypes.uint32_t,ctypes.uint64_t.ptr);
            const status=change(arguments[1],values);
            const v=Array.from({length:4},(_,i)=>Number(values[i].toString()));
            return {status,pid:v[0],beforeCPU:v[1]&4095,beforeCluster:v[1]>>>12,
              afterCPU:v[2]&4095,afterCluster:v[2]>>>12,requested:v[3]};
          }finally{library.close();}''',args=[library,requested],newSandbox=False))
    finally:
        client.command('Marionette:SetContext', {'value':'content'})
    if result['status']!=0 or result['requested']!=requested:
        raise RuntimeError('Main-thread QoS request failed: '+str(result))
    result.update(mode=mode,method='Real pthread_set_qos_class_self_np on browser main; no affinity or frequency writes. CPU is instantaneous TPIDR_EL0 placement, not sustained affinity.')
    return result
