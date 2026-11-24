"""
kernel: build the core tier without mm, pid namespaces, credentials, fs and device

LinuxKPI's task_struct has no mm_struct users, pid namespaces or credentials, and the core
tier (alloc, sync, time, print, ...) is built without the fs, device and uaccess modules.
Each item that needs one of them gets a #[cfg(sf_mod_<module>)], so it comes back by
enabling that module; nothing is removed.
"""
from sfpatch import *
# task: no mm, pid namespaces or credentials in LinuxKPI's task_struct
unuse("kernel/task.rs", "mm::MmWithUser,", "sf_mod_mm", "use crate::mm::MmWithUser;")
unuse("kernel/task.rs", "pid_namespace::PidNamespace,", "sf_mod_pid_namespace", "use crate::pid_namespace::PidNamespace;")
for pat, cfg in [(r"pub struct Kuid", "sf_mod_cred"), (r"pub fn uid\(&self\)", "sf_mod_cred"), (r"pub fn euid\(&self\)", "sf_mod_cred"),
                 (r"pub fn get_pid_ns", "sf_mod_pid_namespace"), (r"pub fn tgid_nr_ns", "sf_mod_pid_namespace"),
                 (r"pub fn mm\(&self\)", "sf_mod_mm"), (r"pub fn active_pid_ns", "sf_mod_pid_namespace"),
                 (r"^impl Kuid", "sf_mod_cred"), (r"^impl PartialEq for Kuid", "sf_mod_cred"), (r"^impl Eq for Kuid", "sf_mod_cred")]:
    gate("kernel/task.rs", pat, cfg)
# sync::poll needs fs::File
gate("kernel/sync.rs", r"^pub mod poll;", "sf_mod_fs")
# prelude: device macros and UserPtr only with their modules
for name in ["dev_alert", "dev_crit", "dev_dbg", "dev_emerg", "dev_err", "dev_info", "dev_notice", "dev_warn"]:
    sub("kernel/prelude.rs", f"    {name},\n", "")
sub("kernel/prelude.rs", "    uaccess::UserPtr,\n", "")
sub("kernel/prelude.rs", "    ThisModule, //\n};\n", "    ThisModule, //\n};\n#[cfg(sf_mod_device)]\n#[doc(no_inline)]\npub use super::{dev_alert, dev_crit, dev_dbg, dev_emerg, dev_err, dev_info, dev_notice, dev_warn};\n#[cfg(sf_mod_uaccess)]\n#[doc(no_inline)]\npub use super::uaccess::UserPtr;\n")
# page: the user-copy method only with uaccess
unuse("kernel/page.rs", "uaccess::UserSliceReader, //", "sf_mod_uaccess", "use crate::uaccess::UserSliceReader;")
gate("kernel/page.rs", r"pub unsafe fn copy_from_user_slice_raw", "sf_mod_uaccess")
