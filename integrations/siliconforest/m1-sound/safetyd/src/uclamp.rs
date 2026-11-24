// SPDX-License-Identifier: MIT
// (C) 2022 The Asahi Linux Contributors
//
// Darwin port: utilization clamping (sched_setattr with SCHED_FLAG_UTIL_CLAMP_*) is a
// Linux scheduler feature and XNU has nothing equivalent, so on Darwin set_uclamp()
// only logs that the config's uclamp_min/uclamp_max are ignored. No QoS class is set
// in its place on purpose: the background/utility classes throttle a thread, and the
// daemon has to make the 250 ms lease. The Linux implementation below is upstream's,
// unchanged, and is what the host build uses.

#[cfg(target_os = "linux")]
use log::{info, warn};

#[cfg(target_os = "linux")]
#[derive(Default)]
#[repr(C)]
struct SchedAttr {
    size: u32,
    sched_policy: u32,
    sched_flags: u64,
    sched_nice: i32,
    sched_priority: u32,
    sched_runtime: u64,
    sched_deadline: u64,
    sched_period: u64,
    sched_util_min: u32,
    sched_util_max: u32,
}

#[cfg(target_os = "linux")]
pub fn set_uclamp(uclamp_min: u32, uclamp_max: u32) {
    let mut attr: SchedAttr = Default::default();
    let pid = unsafe { libc::getpid() };

    if unsafe {
        libc::syscall(
            libc::SYS_sched_getattr,
            pid,
            &mut attr,
            core::mem::size_of::<SchedAttr>(),
            0,
        )
    } != 0
    {
        warn!("Failed to set uclamp");
        return;
    }

    /* SCHED_FLAG_KEEP_POLICY |
     * SCHED_FLAG_KEEP_PARAMS |
     * SCHED_FLAG_UTIL_CLAMP_MIN |
     * SCHED_FLAG_UTIL_CLAMP_MAX */
    attr.sched_flags = 0x8 | 0x10 | 0x20 | 0x40;
    attr.sched_util_min = uclamp_min;
    attr.sched_util_max = uclamp_max;

    if unsafe { libc::syscall(libc::SYS_sched_setattr, pid, &mut attr, 0) } != 0 {
        warn!("Failed to set uclamp");
        return;
    }

    info!("Set task uclamp to {}:{}", uclamp_min, uclamp_max);
}

#[cfg(not(target_os = "linux"))]
pub fn set_uclamp(uclamp_min: u32, uclamp_max: u32) {
    log::info!(
        "uclamp {}:{} from the config ignored (not available on this system)",
        uclamp_min,
        uclamp_max
    );
}
