#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Numbers and PASS/FAIL lines from a saved guest console of a SFCpuPower boot.

  th_summary.py <console.bin>          the experiment suite (guest/darwin-cpupower-suite.in)
  th_summary.py --soak <console.bin>   the long thermal run (guest/darwin-cpupower-soak.in)
  th_summary.py --fan <console.bin>    the fan experiment (guest/darwin-cpupower-fan.in)
  th_summary.py --tune <console.bin>   the policy settings comparison (guest/darwin-cpupower-tune.in)

Exit status 0 when every check passed.
"""
import re
import sys
from collections import OrderedDict

soak = '--soak' in sys.argv[1:]
fan_mode = '--fan' in sys.argv[1:]
path = [a for a in sys.argv[1:] if not a.startswith('--')][0]
text = open(path, 'rb').read().decode('latin-1')
results = []


def check(name, ok, detail=''):
    results.append(bool(ok))
    print(f'{"PASS" if ok else "FAIL"} {name}' + (f': {detail}' if detail else ''))


def fields(line):
    return dict(re.findall(r'(\w+)=("[^"]*"|\S+)', line))


def first(tag):
    m = re.search(r'^' + tag + r'\b.*$', text, re.M)
    return fields(m.group(0)) if m else None


def pair(value):            # "E5/P12" -> (5, 12)
    m = re.fullmatch(r'E(-?\d+)/P(-?\d+)', value or '')
    return (int(m.group(1)), int(m.group(2))) if m else (None, None)


phases = [(m.group(1), int(m.group(2))) for m in re.finditer(r'^TH_PHASE (\w+) t=(\d+)', text, re.M)]
mons = [fields(m.group(0)) for m in re.finditer(r'^TH_MON .*$', text, re.M)]


def phase_window(name):
    for i, (n, t) in enumerate(phases):
        if n == name:
            return t, phases[i + 1][1] if i + 1 < len(phases) else 10 ** 9
    return None


def mons_in(name):
    w = phase_window(name)
    return [m for m in mons if w and w[0] <= int(m['t']) < w[1]]


groups = OrderedDict()
for m in re.finditer(r'^TH_WORKER tag=(\w+) id=(\d+) megabytes=(\d+) seconds=(\d+) elapsed=(\d+)', text, re.M):
    groups.setdefault(m.group(1), []).append((int(m.group(3)), int(m.group(5))))


def rate(tag):
    rows = groups.get(tag, [])
    return sum(mb / t for mb, t in rows if t)


def total(tag):
    return sum(mb for mb, _ in groups.get(tag, []))


for key in ('smc_keys', 'temperature_keys', 'die_keys', 'sensors', 'fan_min_rpm', 'fan_max_rpm', 'fan_mode',
            'limit_c', 'critical_c'):
    m = re.search(r'^kern\.sf_thermal\.' + key + r': (.*)$', text, re.M)
    print(f'kern.sf_thermal.{key}: {m.group(1).strip() if m else "(missing)"}')
for m in re.finditer(r'^SFCpuPower: (?!thermal: (minute|keys))(.*)$', text, re.M):
    print('kernel:', m.group(0)[:230])
minute_lines = re.findall(r'^SFCpuPower: thermal: minute: .*$', text, re.M)
for line in minute_lines:
    print('kernel:', line[:230])

dies = [int(m['die_mc']) for m in mons if m.get('die_mc', '-1') not in ('-1', '')]
fans = [int(m['fan']) for m in mons if re.fullmatch(r'-?\d+', m.get('fan', ''))]
if dies:
    print(f'die temperature over {len(mons)} monitor lines: {min(dies) / 1000:.1f} .. {max(dies) / 1000:.1f} C')
if fans:
    print(f'fan: {min(fans)} .. {max(fans)} rpm')

sensor = first('TH_SENSOR_WAIT')
check('die sensors readable', sensor and sensor.get('valid') == '1',
      f'after {sensor.get("seconds")} s' if sensor else 'no TH_SENSOR_WAIT line')
panics = re.findall(r'(?im)^.*panic\(cpu.*$', text)
check('no panic', not panics, panics[0][:160] if panics else '')
check('guest script ran to its end', re.search(r'^TH_END_', text, re.M) is not None)

def print_monitor(lines):
    for m in lines:
        print(f"  t={m['t']:>4}s die {m.get('die_mc')} {m.get('status'):<9} cap {m.get('cap'):<7} pstate {m.get('pstate'):<7} "
              f"util {m.get('util'):<11} fan {m.get('fan')} target {m.get('target')} asked {m.get('asked', '-')} "
              f"mode {m.get('mode', '-')} mW {m.get('mw')}")


def stress_and_states():
    stress, loaded, idle2 = first('TH_STRESS'), first('TH_LOADED'), first('TH_IDLE2')
    check('transition stress', stress and stress.get('failures') == '0' and stress.get('busy_timeouts') == '0'
          and int(stress.get('done', 0)) >= 2 * int(stress.get('asked', 1)),
          f"{stress.get('done')} transitions, {stress.get('failures')} failed, slowest {stress.get('max_us')} us"
          if stress else 'no TH_STRESS line')
    le, lp = pair(loaded.get('pstate') if loaded else None)
    check('both clusters at their top state under load', (le, lp) == (5, 12),
          f"{loaded.get('pstate')} at utilisation {loaded.get('util')}" if loaded else 'no TH_LOADED line')
    ie, ip = pair(idle2.get('pstate') if idle2 else None)
    check('P cluster drops when idle', ip is not None and lp is not None and ip < lp and ip <= 4,
          f"{idle2.get('pstate')}" if idle2 else 'no TH_IDLE2 line')


if '--tune' in sys.argv[1:]:
    for m in re.finditer(r'^TH_TUNE name=.*$', text, re.M):
        f = fields(m.group(0))
        name = f['name']
        single = fields(re.search(r'^TH_TUNE_SINGLE name=' + name + r' .*$', text, re.M).group(0))
        print(f"{name}: mode {f['mode']} period {f['period']} ms up {f['up']} hold {f['hold']}: "
              f"single {total('single_' + name)} MB/20 s, 8 workers {total('par8_' + name)} MB/20 s, "
              f"idle {f['idle_pstate']} {f['idle_mw']} mW, die {int(f['die_mc']) / 1000:.1f} C {f['status']}, "
              f"{f['transitions']} transitions")
        print(f"   while single: P {single.get('p_residency')} E {single.get('e_residency')}")
    if first('TH_STRESS'):
        stress = first('TH_STRESS')
        check('transition stress', stress.get('failures') == '0' and stress.get('busy_timeouts') == '0'
              and int(stress.get('done', 0)) >= 2 * int(stress.get('asked', 1)),
              f"{stress.get('done')} transitions, {stress.get('failures')} failed, slowest {stress.get('max_us')} us")
    for name, _ in phases:
        lines = mons_in(name) if re.fullmatch(r'heat\d+', name) else []
        d = [int(m['die_mc']) for m in lines if m.get('die_mc') not in ('-1', '')]
        r = [int(m['fan']) for m in lines if re.fullmatch(r'-?\d+', m.get('fan', ''))]
        if not d or not r:
            continue
        seconds = int(lines[-1]['t']) - int(lines[0]['t'])
        manual = any(m.get('mode') == '1' for m in lines)
        pc = [pair(m.get('cap'))[1] for m in lines if pair(m.get('cap'))[1] is not None]
        ps = [pair(m.get('pstate'))[1] for m in lines if pair(m.get('pstate'))[1] is not None]
        mw = [int(m['mw']) for m in lines if re.fullmatch(r'\d+', m.get('mw', ''))]
        print(f"{name}: mean P cap {sum(pc) / len(pc):.1f}, mean P state {sum(ps) / len(ps):.1f}, "
              f"mean die {sum(d) / len(d) / 1000:.1f} C, mean system {sum(mw) / max(1, len(mw)) / 1000:.1f} W, "
              f"{total(name)} MB by {len(groups.get(name, []))} workers")
        print(f"{name}: {seconds} s, die {d[0] / 1000:.1f} -> {d[-1] / 1000:.1f} C (max {max(d) / 1000:.1f}), "
              f"fan {min(r)} .. {max(r)} rpm ({'set by this driver' if manual else 'left to the SMC'}), "
              f"{rate(name):.1f} MB/s, caps at the end {lines[-1].get('cap')}, status {lines[-1].get('status')}")
        if not manual:
            print('FINDING the SMC ' + ('started the fan by itself' if max(r) > 0 else
                  f'did NOT start the fan in {seconds} s at up to {max(d) / 1000:.1f} C'))
        else:
            tail = [m for m in lines[-5:-1] if re.fullmatch(r'\d+', m.get('asked', ''))]
            check('fan follows the written target', len(tail) >= 2 and all(
                abs(int(m['fan']) - int(m['asked'])) <= max(300, int(m['asked']) // 10) and int(m['asked']) >= 1199
                for m in tail), ', '.join(f"asked {m['asked']} running {m['fan']}" for m in tail))
        print_monitor(lines[::3])
    for m in re.finditer(r'^TH_SENSORS .*$', text, re.M):
        print(m.group(0)[:900])
    back = first('TH_BACK')
    if back:
        check('fan given back to the SMC afterwards', back.get('mode') == '0' and back.get('asked') == '0'
              and back.get('write_errors') == '0',
              f"mode {back.get('mode')}, asked {back.get('asked')}, running {back.get('fan')} rpm, "
              f"{back.get('writes')} writes")
    print(f'kernel thermal lines: {len(minute_lines)} a minute apart')
    for key in ('poll_us', 'poll_max_us', 'read_errors', 'polls'):
        m = re.findall(r'^kern\.sf_thermal\.' + key + r': (.*)$', text, re.M)
        print(f'kern.sf_thermal.{key}: {m[-1].strip() if m else "(missing)"}')
elif fan_mode:
    stress_and_states()
    auto, ctl, back = mons_in('auto'), mons_in('control'), first('TH_BACK')
    auto_end, ctl_end = first('TH_AUTO_END'), first('TH_CONTROL_END')
    rpm = lambda lines: [int(m['fan']) for m in lines if re.fullmatch(r'-?\d+', m.get('fan', ''))]
    die = lambda lines: [int(m['die_mc']) for m in lines if m.get('die_mc') not in ('-1', '')]
    if auto and die(auto):
        seconds = int(auto[-1]['t']) - int(auto[0]['t'])
        print(f'fan left to the SMC: {seconds} s of load, die {die(auto)[0] / 1000:.1f} -> {die(auto)[-1] / 1000:.1f} C '
              f'(max {max(die(auto)) / 1000:.1f}), fan {min(rpm(auto))} .. {max(rpm(auto))} rpm, '
              f'{rate("auto"):.1f} MB/s, mode {auto_end.get("mode") if auto_end else "?"}')
        print('FINDING the SMC ' + ('started the fan by itself' if max(rpm(auto)) > 0 else
              f'did NOT start the fan in {seconds} s at up to {max(die(auto)) / 1000:.1f} C'))
    if ctl and die(ctl):
        print(f'fan controlled: die {die(ctl)[0] / 1000:.1f} -> {die(ctl)[-1] / 1000:.1f} C, fan {min(rpm(ctl))} .. '
              f'{max(rpm(ctl))} rpm, {rate("control"):.1f} MB/s')
    # late in the phase, but not its last line: the load may have ended by then
    tail = [m for m in ctl[-5:-1] if re.fullmatch(r'\d+', m.get('asked', '')) and re.fullmatch(r'\d+', m.get('fan', ''))]
    follows = [abs(int(m['fan']) - int(m['asked'])) <= max(300, int(m['asked']) // 10) and int(m['asked']) >= 1199
               and m.get('mode') == '1' for m in tail]
    check('fan follows the written target', len(tail) >= 2 and all(follows) and ctl_end is not None
          and ctl_end.get('write_errors') == '0',
          ', '.join(f"asked {m['asked']} running {m['fan']}" for m in tail) +
          (f"; {ctl_end.get('writes')} writes, {ctl_end.get('write_errors')} errors" if ctl_end else ''))
    check('fan given back to the SMC afterwards', back is not None and back.get('mode') == '0' and back.get('asked') == '0',
          f"mode {back.get('mode')}, asked {back.get('asked')}, running {back.get('fan')} rpm" if back else 'no TH_BACK line')
    print_monitor(mons)
elif soak:
    w = re.search(r'^TH_PHASE soak t=\d+ seconds=(\d+) workers=(\d+)', text, re.M)
    seconds, workers = (int(w.group(1)), int(w.group(2))) if w else (0, 0)
    minutes = seconds // 60
    done = [t for t in groups if t.startswith('soak')]
    check('every one-minute group finished with all workers',
          minutes > 0 and len(done) == minutes and all(len(groups[t]) == workers for t in done),
          f'{len(done)} of {minutes} groups, {workers} workers')
    check('one kernel thermal line a minute', len(minute_lines) >= minutes - 2, f'{len(minute_lines)} lines in {minutes} minutes')
    crit = re.search(r'^kern\.sf_thermal\.critical_c: (\d+)', text, re.M)
    in_soak = mons_in('soak')
    check('a die temperature in every monitor line', in_soak and all(m.get('die_mc') not in ('-1', '') for m in in_soak),
          f'{len(in_soak)} lines')
    check('never critical, never without sensors',
          in_soak and all(m.get('status') in ('ok', 'capped') for m in in_soak),
          ', '.join(sorted({m.get('status', '?') for m in in_soak})))
    check('hottest reading below the critical temperature',
          bool(dies) and crit is not None and max(dies) < int(crit.group(1)) * 1000,
          f'{max(dies) / 1000:.1f} C, critical {crit.group(1) if crit else "?"} C' if dies else 'no readings')
    spinning = [m for m in in_soak if re.fullmatch(r'\d+', m.get('fan', '')) and int(m['fan']) > 0]
    manual = any(m.get('mode') == '1' for m in in_soak)
    if spinning:
        print(f"FINDING the fan ran ({'this driver set it: fan_control' if manual else 'the SMC started it by itself'}): "
              f"first at t={spinning[0]['t']} s, {min(int(m['fan']) for m in spinning)} .. "
              f"{max(int(m['fan']) for m in spinning)} rpm")
    elif in_soak:
        print(f"FINDING the fan never ran in {int(in_soak[-1]['t']) - int(in_soak[0]['t'])} s of load "
              f"(mode {in_soak[-1].get('mode', '?')}): the SMC does not run it by itself here; sffan=1 does")
    per_minute = [total(t) for t in done]
    if per_minute:
        print(f'throughput: {sum(per_minute)} MB in {len(per_minute)} minutes; per minute min {min(per_minute)} '
              f'max {max(per_minute)}; first {per_minute[0]} last {per_minute[-1]}')
    for m in in_soak[::max(1, len(in_soak) // 30)]:
        print(f"  t={m['t']:>5}s die {int(m['die_mc']) / 1000:5.1f} C {m['status']:<8} cap {m['cap']:<7} "
              f"pstate {m['pstate']:<7} fan {m['fan']} rpm {m['mw']} mW")
else:
    stress_and_states()
    spin = first('TH_SPIN_RESIDENCY')
    if spin:
        print(f"residency while spinning: E {spin.get('e')} P {spin.get('p')}")
    for tag in ('single', 'par8'):
        if tag in groups:
            print(f'{tag}: {total(tag)} MB in {max(t for _, t in groups[tag])} s, {len(groups[tag])} workers')
    forced = first('TH_FORCED')
    fm, hm = mons_in('forced'), mons_in('heat')
    if hm:
        hd = [int(m['die_mc']) for m in hm if m.get('die_mc') not in ('-1', '')]
        if hd:
            print(f'heat phase: die {hd[0] / 1000:.1f} -> {hd[-1] / 1000:.1f} C (max {max(hd) / 1000:.1f}), '
                  f'{rate("heat"):.1f} MB/s, fan {hm[0].get("fan")} -> {hm[-1].get("fan")} rpm')
    caps = [pair(m.get('cap')) for m in fm]
    pstates = [pair(m.get('pstate')) for m in fm]
    low_cap = min((c[1] for c in caps if c[1] is not None), default=None)
    low_p = min((p[1] for p in pstates if p[1] is not None), default=None)
    check('forced limit lowers the P cluster', forced is not None and low_cap is not None and low_cap < 12 and
          low_p is not None and low_p < 12,
          f"limit {forced.get('forced_limit')} C at die {int(forced.get('die_mc', 0)) / 1000:.1f} C: lowest cap P{low_cap}, "
          f"lowest state P{low_p}" if forced else 'no TH_FORCED line')
    check('forced limit lowers the throughput', rate('heat') > 0 and rate('forced') < 0.9 * rate('heat'),
          f'{rate("forced"):.1f} MB/s against {rate("heat"):.1f} MB/s before')
    if 'restored' in groups:
        r = first('TH_RESTORED')
        print(f'restored: {rate("restored"):.1f} MB/s, {r.get("status") if r else "?"}, cap {r.get("cap") if r else "?"}')
    ns, cr, rec = first('TH_NOSENSOR'), first('TH_CRITICAL'), first('TH_RECOVERED')
    check('failing polls fall back to E2/P6', ns and ns.get('status') == 'no-sensor' and ns.get('cap') == 'E2/P6'
          and pair(ns.get('pstate'))[1] is not None and pair(ns.get('pstate'))[1] <= 6,
          f"{ns.get('status')}, cap {ns.get('cap')}, state {ns.get('pstate')}" if ns else 'no TH_NOSENSOR line')
    check('critical reading cuts to E1/P1', cr and cr.get('status') == 'critical' and cr.get('cap') == 'E1/P1'
          and cr.get('pstate') == 'E1/P1',
          f"{cr.get('status')} at {int(cr.get('die_mc', 0)) / 1000:.1f} C (with the test offset), cap {cr.get('cap')}, "
          f"state {cr.get('pstate')}" if cr else 'no TH_CRITICAL line')
    check('caps come back afterwards', rec and rec.get('status') == 'ok' and rec.get('cap') == 'E5/P12',
          f"{rec.get('status')}, cap {rec.get('cap')}" if rec else 'no TH_RECOVERED line')
    print_monitor(mons)

print('RESULT', 'PASS' if all(results) else 'FAIL', f'({sum(results)} of {len(results)} checks)')
sys.exit(0 if all(results) else 1)
