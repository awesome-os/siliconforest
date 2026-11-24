# SPDX-License-Identifier: MIT
"""CGL's C CLOCK_MONOTONIC -> epoch bridge, not Python's monotonic epoch."""
import ctypes
import os
import subprocess
import time


def summarize(samples):
    if not samples:raise ValueError('No actual bracketed clock observations')
    intervals=[]
    for s in samples:
        before,after,native=s['epoch_before_ms'],s['epoch_after_ms'],s['native_monotonic_ms']
        import math
        if not all(math.isfinite(x) for x in [before,after,native]) or after<before or native<0:
            raise ValueError('Invalid clock bracket')
        intervals.append((before-native,after-native))
    lower=max(x[0] for x in intervals);upper=min(x[1] for x in intervals)
    if lower>upper:raise ValueError('No stable shared clock-offset interval')
    return dict(epoch_minus_native_ms=(lower+upper)/2,uncertainty_ms=(upper-lower)/2,
                samples=samples,scope='CGL clock_gettime(CLOCK_MONOTONIC), not Python time.monotonic; no GPU or process observation')


def paired(before,after,max_drift_ms=2):
    """Bound a run by actual observations from the same boot.

    Keep the union of the offset intervals, rather than assuming independent
    realtime/native clocks have zero drift or interpolating between them.
    """
    if not 0<=max_drift_ms<=10:raise ValueError('Explicit bounded clock drift required')
    for name in ['kernel_uuid','boot_time','clock_id']:
        if before.get(name) is None or before[name]!=after.get(name):
            raise ValueError('Native observations must belong to the same boot and clock')
    if not before['kernel_uuid'] or not before['boot_time']:raise ValueError('Actual native boot identity required')
    summaries=[summarize(item['samples']) for item in [before,after]]
    times=[[s['native_monotonic_ms'] for s in item['samples']] for item in [before,after]]
    if any(any(b<a for a,b in zip(row,row[1:])) for row in times) or max(times[0])>=min(times[1]):
        raise ValueError('Clock observations must bracket one ordered run')
    lo=min(s['epoch_minus_native_ms']-s['uncertainty_ms'] for s in summaries)
    hi=max(s['epoch_minus_native_ms']+s['uncertainty_ms'] for s in summaries)
    drift=abs(summaries[1]['epoch_minus_native_ms']-summaries[0]['epoch_minus_native_ms'])
    if hi-lo>max_drift_ms:raise ValueError('Native/realtime drift or uncertainty exceeds bound')
    return dict(epoch_minus_native_bounds_ms=[lo,hi],offset_center_drift_ms=drift,
                native_run_bounds_ms=[min(times[0]),max(times[1])],
                kernel_uuid=before['kernel_uuid'],boot_time=before['boot_time'],clock_id=before['clock_id'],
                scope='Conservative before/after C CLOCK_MONOTONIC to realtime alignment; not display timestamps')


def align_exports(samples,video,bridge):
    """Annotate native image samples relative to the admitted JS measurement."""
    import math
    start=video['timeOrigin']+video['measureStart'];end=video['timeOrigin']+video['measureEnd']
    if not all(math.isfinite(x) for x in [start,end]) or end<=start:raise ValueError('Invalid own JS measurement window')
    lo,hi=bridge['epoch_minus_native_bounds_ms'];first,last=bridge['native_run_bounds_ms'];rows=[]
    for sample in samples:
        a,b=sample['before_ms'],sample['after_ms']
        if not first<=a<=b<=last:raise ValueError('Native export falls outside clock observations')
        rows.append(dict(sequence=sample['sequence'],surface=sample['surface'],frame=sample['frame'],
            after_js_measure_start_bounds_ms=[a+lo-start,b+hi-start],
            inside_js_measurement=(a+lo>=start and b+hi<=end)))
    return dict(bridge=bridge,samples=rows,js_duration_ms=end-start,
        limits='JavaScript performance.timeOrigin plus performance.now is treated as realtime epoch. Native brackets include observed drift; browser clock semantics and intermediate clock adjustments are not independently proved. Export image identity is not compositor/panel presentation.')


def capture():
    if os.uname().sysname!='Darwin':raise ValueError('Native SiliconForest clock bridge only')
    class Timespec(ctypes.Structure):_fields_=[('sec',ctypes.c_long),('nsec',ctypes.c_long)]
    library=ctypes.CDLL('/usr/lib/libSystem.B.dylib',use_errno=True)
    clock=library.clock_gettime;clock.argtypes=[ctypes.c_int,ctypes.POINTER(Timespec)];clock.restype=ctypes.c_int
    samples=[]
    for _ in range(7):
        stamp=Timespec();before=time.time_ns()/1e6
        if clock(int(time.CLOCK_MONOTONIC),ctypes.byref(stamp)):raise OSError(ctypes.get_errno(),'Native monotonic query')
        after=time.time_ns()/1e6
        if not 0<=stamp.nsec<10**9:raise ValueError('Invalid native timespec')
        samples.append(dict(epoch_before_ms=before,epoch_after_ms=after,
                            native_monotonic_ms=stamp.sec*1000+stamp.nsec/1e6))
    result=summarize(samples);result.update(clock_id=int(time.CLOCK_MONOTONIC),
        kernel_uuid=subprocess.check_output(['sysctl','-n','kern.kernelcacheuuid'],text=True).strip(),
        boot_time=subprocess.check_output(['sysctl','-n','kern.boottime'],text=True).strip())
    return result


if __name__=='__main__':
    import json
    print(json.dumps(capture()))
