# Trackpad enumeration helpers

Original claim, interface-2 header parse, and descriptor copy for T8103 SPI HID. There is no transport, descriptor parser, haptic command, or mode write in this directory. The frozen keyboard matcher `m1kbd_response_matches` is not modified.

The names match the adapter contract:

```c
int m1tp_claim_request(uint8_t *used, uint8_t report, uint8_t subdevice);
int m1tp_interface_info(const uint8_t *payload, size_t n,
                        struct m1tp_interface_info *out);
int m1tp_copy_descriptor(uint8_t *dst, size_t cap, uint32_t *length,
                         const uint8_t *src, size_t n);
```

`struct m1tp_interface_info` is 8 bytes: `uint16_t max_input, max_output, max_control`, then `uint8_t interface, country`. Claim returns 1 or 0. Metadata and copy return 0, or -1 argument, -2 length, -3 interface, -4 input. A refusal leaves the output unchanged.

The claim mask is 31. The keys are 01/d0, 02/01, 10/01, 02/02, and 10/02, one use each. It is not a lock. The caller claims a key before incrementing an ID, encoding, or sending.

Interface metadata requires 16..2038 bytes and byte 1 equal to 2. Country is byte 5. The little-endian input length at offset 6 is any nonzero uint16, including 2048 and 65535, and is stored as a diagnostic. It is not a transport or parser capacity. Zero is refused. Output at offset 8 and control at offset 10 are stored for any uint16 value, including 0 and 4086. A name span is not read. A descriptor copy accepts 1..512 bytes into caller storage and does not invent bytes. Revision 001 is archived under revisions/001 and still refuses input above 2038. This revision does not change the collector or the adapter.

Run `python3 test_m1tp_enum.py` from this directory. Build products stay in a temporary directory. License selection for this original code remains pending under AGENTS.md.
