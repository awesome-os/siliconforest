#ifndef PD_SPI_HID_PROTOCOL_H
#define PD_SPI_HID_PROTOCOL_H

/*
 * Preparatory Apple SPI-HID packet engine for the PureDarwin adapter.
 *
 * Astra owns the IOHIDDevice adapter and the native SPI driver. This
 * module only encodes and decodes the 256-byte packets, checks CRC-16/ARC,
 * reassembles ordered fragments, and frames the three empty requests.
 * It does not open SPI, GPIO, PMGR, or MMIO, and it does not decode HID
 * usages into key events. A delivered payload is raw bytes.
 *
 * Wire grammar follows omacom/linux-aurora commit 6a08a08
 * drivers/hid/spi-hid/spi-hid-apple-core.c. The source text is not copied.
 * See PROVENANCE.md in this directory.
 *
 * Flags. A normal message is delivered only when packet flags are
 * PD_SPI_HID_FLAG_READ (0x20) or PD_SPI_HID_FLAG_WRITE (0x40). Any other
 * flags value on a packet whose length is at least 10 is rejected and
 * delivers no payload. That check happens before reassembly, so an
 * unknown-flag packet does not discard an in-progress message.
 *
 * Boot is not part of the flag rule. After the packet CRC matches, a
 * packet whose length field is 4 and whose data begins a0 80 00 00 is a
 * boot notice. Recognition happens before the 8-byte message header is
 * required and before flags are consulted. Unknown flags still return
 * PD_SPI_HID_BOOT. The four bytes are the notice itself, not a header.
 *
 * Caps:
 * - Packet data length is at most 246. A larger length is rejected.
 * - A reassembled message is at most 2048 bytes. The reference bounded
 *   the sum by U16_MAX while the buffer is 0x800; this decoder refuses
 *   a total above 2048 and does not copy it.
 * - A descriptor response (flags 0x40, type0 0x20, type1 0x10) whose
 *   payload is longer than 512 bytes is not delivered.
 * - A message is delivered only when its CRC matches and
 *   payload_len + 10 <= the reassembled length, with payload_len != 0.
 * - The encoder writes packet length = 8 + payload + 2. The decoder
 *   accepts a reassembled buffer that is longer than that sum.
 * - Continuations must arrive at the next byte. offset + length + remain
 *   must stay equal to the total declared by the first fragment.
 * - Write status other than ac 27 68 d5 is a failed write. The reference
 *   only warned and still returned success. pd_spi_hid_write_status_ok
 *   returns 0 for a mismatch. Packet decode never reads those four bytes.
 *   The adapter serializes TX and RX and calls the status check itself.
 * - pd_spi_hid_copy_bounded copies the smaller length. The reference
 *   GET_REPORT path copied the larger length into the caller buffer.
 * - Device-info serial bytes are not parsed, copied into a vendor
 *   buffer, or logged. The adapter redacts them if it prints a payload.
 */

#include <stddef.h>
#include <stdint.h>

#define PD_SPI_HID_PACKET_SIZE 256
#define PD_SPI_HID_PACKET_PAYLOAD 246
#define PD_SPI_HID_PACKET_PREFIX 254
#define PD_SPI_HID_MSG_HEADER_SIZE 8
#define PD_SPI_HID_MSG_CRC_SIZE 2
#define PD_SPI_HID_MESSAGE_MAX 0x800
#define PD_SPI_HID_DESCRIPTOR_MAX 512
#define PD_SPI_HID_REQUEST_PAYLOAD_MAX \
	(PD_SPI_HID_PACKET_PAYLOAD - PD_SPI_HID_MSG_HEADER_SIZE - PD_SPI_HID_MSG_CRC_SIZE)

#define PD_SPI_HID_FLAG_READ 0x20
#define PD_SPI_HID_FLAG_WRITE 0x40

#define PD_SPI_HID_DEV_MNGT 0x00
#define PD_SPI_HID_DEV_KBD 0x01
#define PD_SPI_HID_DEV_TP 0x02
#define PD_SPI_HID_DEV_AUX 0x03
#define PD_SPI_HID_DEV_INFO 0xd0

#define PD_SPI_HID_TYPE_INPUT 0x10
#define PD_SPI_HID_TYPE_RESPONSE 0x20
#define PD_SPI_HID_U1_DEVICE_INFO 0x01
#define PD_SPI_HID_U1_IFACE_INFO 0x02
#define PD_SPI_HID_U1_DESCRIPTOR 0x10
#define PD_SPI_HID_U1_STATUS 0xe0

#define PD_SPI_HID_BOOT_LEN 4
#define PD_SPI_HID_BOOT_B0 0xa0
#define PD_SPI_HID_BOOT_B1 0x80
#define PD_SPI_HID_BOOT_B2 0x00
#define PD_SPI_HID_BOOT_B3 0x00

#define PD_SPI_HID_WRITE_STATUS_LEN 4
#define PD_SPI_HID_STATUS_B0 0xac
#define PD_SPI_HID_STATUS_B1 0x27
#define PD_SPI_HID_STATUS_B2 0x68
#define PD_SPI_HID_STATUS_B3 0xd5

enum pd_spi_hid_result {
	PD_SPI_HID_MESSAGE = 0,
	PD_SPI_HID_NEED_MORE = 1,
	PD_SPI_HID_BOOT = 2,
	PD_SPI_HID_REJECT = -1
};

/* Fixed 256-byte packet. Multi-byte fields are little-endian bytes.
 * Alignment is 1. A SPI transfer of PD_SPI_HID_PACKET_SIZE bytes overlays
 * this struct. CRC covers bytes 0..253. */
typedef struct pd_spi_hid_packet {
	uint8_t flags;
	uint8_t device;
	uint8_t offset_le[2];
	uint8_t remain_le[2];
	uint8_t length_le[2];
	uint8_t data[PD_SPI_HID_PACKET_PAYLOAD];
	uint8_t crc16_le[2];
} pd_spi_hid_packet;

_Static_assert(sizeof(pd_spi_hid_packet) == PD_SPI_HID_PACKET_SIZE,
	"outer SPI-HID packet is 256 bytes");
_Static_assert(offsetof(pd_spi_hid_packet, data) == 8,
	"packet payload starts at byte 8");
_Static_assert(offsetof(pd_spi_hid_packet, crc16_le) == PD_SPI_HID_PACKET_PREFIX,
	"packet CRC covers bytes 0..253");
_Static_assert(PD_SPI_HID_REQUEST_PAYLOAD_MAX == 236,
	"one request body fits in the 246-byte packet data");

/* Filled only for PD_SPI_HID_MESSAGE and PD_SPI_HID_BOOT.
 * On PD_SPI_HID_NEED_MORE and PD_SPI_HID_REJECT every field is zero and
 * payload is NULL. payload is valid until the next decode call on the
 * same reassembly object. */
typedef struct pd_spi_hid_delivery {
	uint8_t device;
	uint8_t flags;
	uint8_t request_id;
	uint8_t request_id_valid;
	uint8_t type0;
	uint8_t type1;
	uint8_t type2;
	uint16_t response_len;
	const uint8_t *payload;
	uint16_t payload_len;
} pd_spi_hid_delivery;

/* No heap use. The adapter embeds this and serializes access.
 * assembling holds an in-progress message. delivered holds the last
 * completed message. boot holds the four notice bytes. A boot packet
 * does not overwrite either message buffer and does not cancel an
 * in-progress message. An outer length below 10 never joins a message. */
typedef struct pd_spi_hid_reassembly {
	uint8_t device;
	uint8_t flags;
	uint16_t filled;
	uint16_t total;
	uint8_t assembling[PD_SPI_HID_MESSAGE_MAX];
	uint8_t delivered[PD_SPI_HID_MESSAGE_MAX];
	uint8_t boot[PD_SPI_HID_BOOT_LEN];
} pd_spi_hid_reassembly;

extern const uint8_t pd_spi_hid_boot_notice[PD_SPI_HID_BOOT_LEN];
extern const uint8_t pd_spi_hid_write_status[PD_SPI_HID_WRITE_STATUS_LEN];

static inline uint16_t pd_spi_hid_load_le16(const uint8_t le[2])
{
	return (uint16_t)le[0] | ((uint16_t)le[1] << 8);
}

static inline void pd_spi_hid_store_le16(uint8_t le[2], uint16_t value)
{
	le[0] = (uint8_t)value;
	le[1] = (uint8_t)(value >> 8);
}

/* CRC-16/ARC: reflected poly 0x8005 (0xA001), init 0, xorout 0.
 * Same function as Linux crc16(0, data, len). Not CRC-16/CCITT. */
uint16_t pd_spi_hid_crc16(const uint8_t *data, size_t len);

void pd_spi_hid_reassembly_reset(pd_spi_hid_reassembly *state);

/* Build one 256-byte packet and its packet CRC. length must be <= 246.
 * flags are stored as given, including values decode will later reject.
 * Returns 0, or -1 if the arguments cannot form a packet. */
int pd_spi_hid_encode_packet(pd_spi_hid_packet *packet,
	uint8_t flags, uint8_t device,
	uint16_t offset, uint16_t remain,
	const uint8_t *data, uint16_t length);

/* One write/response packet (flags 0x40, offset 0, remain 0) carrying
 * an 8-byte header, payload, and message CRC. payload_len 0 is a legal
 * transmit (the info, iface, and descriptor requests). payload_len above
 * 236 returns -1. request_id is stored and not incremented; the adapter
 * owns the counter. Returns 0, or -1. */
int pd_spi_hid_encode_request(pd_spi_hid_packet *packet,
	uint8_t target,
	uint8_t type0, uint8_t type1, uint8_t type2,
	uint8_t request_id, uint16_t response_len,
	const uint8_t *payload, size_t payload_len);

/* target 0xd0, header 20 01 d0, rsplen 0, empty payload. */
int pd_spi_hid_encode_device_info_request(pd_spi_hid_packet *packet,
	uint8_t request_id);

/* target 0xd0, header 20 02 <iface>, rsplen 512, empty payload.
 * Reference range is iface 0 .. n-1. n comes from the device-info
 * response; this function does not know n. */
int pd_spi_hid_encode_iface_info_request(pd_spi_hid_packet *packet,
	uint8_t request_id, uint8_t iface);

/* target 0xd0, header 20 10 <iface>, rsplen 512, empty payload.
 * Reference range is iface 1 .. n-1. Management iface 0 is not queried. */
int pd_spi_hid_encode_descriptor_request(pd_spi_hid_packet *packet,
	uint8_t request_id, uint8_t iface);

/* Decode one 256-byte packet.
 * PD_SPI_HID_MESSAGE: device, flags, request_id, type0/1/2, response_len,
 * and payload are set. payload is the message body, not the header and
 * not the message CRC. An input report is flags 0x20, device 1, type0 0x10.
 * Device 0 with type1 0xe0 is the reference's status report; the payload
 * is still raw.
 * PD_SPI_HID_BOOT: device and flags are copied from the packet, including
 * unknown flags. request_id_valid is 0. payload is the four notice bytes.
 * PD_SPI_HID_NEED_MORE: a fragment was stored. No metadata and no payload.
 * PD_SPI_HID_REJECT: no metadata and no payload. */
int pd_spi_hid_decode_packet(pd_spi_hid_reassembly *state,
	const pd_spi_hid_packet *packet,
	pd_spi_hid_delivery *out);

/* Compare the separate 4-byte write-status transfer. Returns 1 only when
 * the bytes are ac 27 68 d5. Returns 0 for a mismatch or a NULL pointer.
 * A 0 means the write failed. Does not touch reassembly. */
int pd_spi_hid_write_status_ok(const uint8_t status[PD_SPI_HID_WRITE_STATUS_LEN]);

/* Copy min(dst_cap, src_len) bytes. Returns the count copied. A NULL
 * pointer with a non-zero length copies nothing and returns 0. */
size_t pd_spi_hid_copy_bounded(uint8_t *dst, size_t dst_cap,
	const uint8_t *src, size_t src_len);

#endif /* PD_SPI_HID_PROTOCOL_H */
