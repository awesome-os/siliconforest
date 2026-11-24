"""
kernel: c"..." literals passed to C: as_char_ptr(), not as_ptr()

`CStr::as_ptr()` is `*const core::ffi::c_char`, which is i8 on Apple targets, while the
kernel's `ffi::c_char` is u8 everywhere. The three call sites only compile where the two
happen to agree (arm64 Linux); the crate's own `CStrExt::as_char_ptr` is the portable form.
"""
from sfpatch import *
sub("kernel/drm/file.rs", 'c"system".as_ptr()', 'c"system".as_char_ptr()')
sub("kernel/io/mem.rs", 'c"rust-io-first".as_ptr()', 'c"rust-io-first".as_char_ptr()')
sub("kernel/io/mem.rs", 'c"rust-io-second".as_ptr()', 'c"rust-io-second".as_char_ptr()')
