"""
kernel: io, irq and uaccess without the device model

The device/devres modules are replaced separately (thin modules over the IOKit nub), so the
parts of io, irq and uaccess that are written against them, or against dma and fs, are
compiled only with those modules:
  io::mem (IoMem/IoRequest are Devres-managed)      sf_mod_device
  irq::request (registrations are Devres-managed)   sf_mod_device
  UserSliceWriter::write_dma                        sf_mod_dma
  UserSlice{Reader,Writer}::{read,write}_slice_file sf_mod_fs
What remains is the register access (Io, Mmio, the io_read!/io_write! macros, polling),
io::Resource/Region, irq::Flags and the user-copy types.
"""
from sfpatch import *
sub("kernel/io.rs", "#[cfg(CONFIG_HAS_IOMEM)]\npub mod mem;", "#[cfg(all(CONFIG_HAS_IOMEM, sf_mod_device))]\npub mod mem;")
gate("kernel/irq.rs", r"^mod request;", "sf_mod_device")
gate("kernel/irq.rs", r"^pub use request::\{", "sf_mod_device")
unuse("kernel/uaccess.rs", "dma::Coherent,", "sf_mod_dma", "use crate::dma::Coherent;")
unuse("kernel/uaccess.rs", "fs::file,", "sf_mod_fs", "use crate::fs::file;")
gate("kernel/uaccess.rs", r"pub fn write_dma<", "sf_mod_dma")
gate("kernel/uaccess.rs", r"pub fn read_slice_file\(", "sf_mod_fs")
gate("kernel/uaccess.rs", r"pub fn write_slice_file\(", "sf_mod_fs")
