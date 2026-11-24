# SPI-HID protocol engine provenance

Preparatory decoder and request framer. Not a SPI driver and not a keyboard.

Reference: `omacom/linux-aurora` commit `6a08a08e57558083f552733a5276d71b1a4d8ff6`.
Pinned files under `sources/linux-aurora-keyboard-reference`, rehashed locally:

| Path | Bytes | SHA-256 |
| --- | --- | --- |
| `drivers/spi/spi-apple.c` | 18501 | `0d627a7ee7258cae5128c29ae7eac4f65632e347b992018acdcfffdca7b476c9` |
| `drivers/hid/spi-hid/spi-hid-apple-core.c` | 35218 | `9c18211ef16e1c4ff5bf2c8025d24de649092148e3e181b91948cd98a0c3a595` |
| `drivers/hid/spi-hid/spi-hid-apple-of.c` | 3962 | `d065034745f99a37aebca6736b1bc3f052c3f16ac46a10677b8d58286fe5ce42` |
| `drivers/hid/spi-hid/spi-hid-apple.h` | 1104 | `98ecccaeb7eefe03af64e0c066ddf961a1dca7ac55838e4707ed368b5d9541c5` |

SPDX identifiers on those files: `GPL-2.0` on the three `.c` files, `GPL-2.0-only OR MIT` on `spi-hid-apple.h`. Copyright is The Asahi Linux Contributors. `spi-hid-apple-core.c` is based on applespi, Copyright Federico Lorenzi and Ronald Tschalär. `spi-apple.c` is based on spi-sifive, Copyright SiFive, Inc. This directory does not copy those sources or the GPL license text.

CRC-16/ARC (reflected polynomial 0x8005 / 0xA001, init 0, no final xor) is the public algorithm implemented by Linux `crc16(0, ...)`. It is not CRC-16/CCITT.

The packet engine does not port these reference defects:

- `spi-hid-apple-core.c:422` copies `max(len, reply_len)`. `pd_spi_hid_copy_bounded` copies the smaller length.
- `spi-hid-apple-core.c:605` copies the device-info serial into the vendor buffer. This module does not parse that payload and does not log it.
- `spi-hid-apple-core.c:814` and `:842` bound a fragment sum by `U16_MAX` while the buffer is `0x800`. Decode refuses a total above 2048 and a packet data length above 246, and does not copy either.
- `spi-hid-apple-core.c:330-336` warns on a write-status mismatch and still returns success. `pd_spi_hid_write_status_ok` returns 0 unless the four bytes are `ac 27 68 d5`.

`spi-apple.c` and `spi-hid-apple-of.c` describe the controller and the GPIO reset. They are not used here. No MMIO, GPIO, or PMGR access is compiled into this module.
