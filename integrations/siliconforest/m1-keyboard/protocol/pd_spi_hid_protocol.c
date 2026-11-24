#include "pd_spi_hid_protocol.h"

#include <string.h>

const uint8_t pd_spi_hid_boot_notice[PD_SPI_HID_BOOT_LEN] = {
	PD_SPI_HID_BOOT_B0, PD_SPI_HID_BOOT_B1,
	PD_SPI_HID_BOOT_B2, PD_SPI_HID_BOOT_B3
};

const uint8_t pd_spi_hid_write_status[PD_SPI_HID_WRITE_STATUS_LEN] = {
	PD_SPI_HID_STATUS_B0, PD_SPI_HID_STATUS_B1,
	PD_SPI_HID_STATUS_B2, PD_SPI_HID_STATUS_B3
};

uint16_t pd_spi_hid_crc16(const uint8_t *data, size_t len)
{
	uint16_t crc = 0;
	size_t i;

	if (data == NULL)
		return 0;
	for (i = 0; i < len; i++) {
		int bit;

		crc = (uint16_t)(crc ^ data[i]);
		for (bit = 0; bit < 8; bit++) {
			if (crc & 1)
				crc = (uint16_t)((crc >> 1) ^ 0xA001u);
			else
				crc = (uint16_t)(crc >> 1);
		}
	}
	return crc;
}

void pd_spi_hid_reassembly_reset(pd_spi_hid_reassembly *state)
{
	if (state == NULL)
		return;
	memset(state, 0, sizeof(*state));
}

static void abandon(pd_spi_hid_reassembly *state)
{
	state->device = 0;
	state->flags = 0;
	state->filled = 0;
	state->total = 0;
}

int pd_spi_hid_encode_packet(pd_spi_hid_packet *packet,
	uint8_t flags, uint8_t device,
	uint16_t offset, uint16_t remain,
	const uint8_t *data, uint16_t length)
{
	uint32_t span;

	if (packet == NULL)
		return -1;
	if (length > PD_SPI_HID_PACKET_PAYLOAD)
		return -1;
	if (length != 0 && data == NULL)
		return -1;
	span = (uint32_t)offset + length + remain;
	if (span > 65535u)
		return -1;

	memset(packet, 0, sizeof(*packet));
	packet->flags = flags;
	packet->device = device;
	pd_spi_hid_store_le16(packet->offset_le, offset);
	pd_spi_hid_store_le16(packet->remain_le, remain);
	pd_spi_hid_store_le16(packet->length_le, length);
	if (length != 0)
		memcpy(packet->data, data, length);
	pd_spi_hid_store_le16(packet->crc16_le,
		pd_spi_hid_crc16((const uint8_t *)packet, PD_SPI_HID_PACKET_PREFIX));
	return 0;
}

int pd_spi_hid_encode_request(pd_spi_hid_packet *packet,
	uint8_t target,
	uint8_t type0, uint8_t type1, uint8_t type2,
	uint8_t request_id, uint16_t response_len,
	const uint8_t *payload, size_t payload_len)
{
	uint8_t body[PD_SPI_HID_PACKET_PAYLOAD];
	uint16_t message_len;

	if (packet == NULL)
		return -1;
	if (payload_len > PD_SPI_HID_REQUEST_PAYLOAD_MAX)
		return -1;
	if (payload_len != 0 && payload == NULL)
		return -1;

	memset(body, 0, sizeof(body));
	body[0] = type0;
	body[1] = type1;
	body[2] = type2;
	body[3] = request_id;
	pd_spi_hid_store_le16(body + 4, response_len);
	pd_spi_hid_store_le16(body + 6, (uint16_t)payload_len);
	if (payload_len != 0)
		memcpy(body + PD_SPI_HID_MSG_HEADER_SIZE, payload, payload_len);
	message_len = (uint16_t)(PD_SPI_HID_MSG_HEADER_SIZE + payload_len +
		PD_SPI_HID_MSG_CRC_SIZE);
	pd_spi_hid_store_le16(body + PD_SPI_HID_MSG_HEADER_SIZE + payload_len,
		pd_spi_hid_crc16(body, PD_SPI_HID_MSG_HEADER_SIZE + payload_len));
	return pd_spi_hid_encode_packet(packet, PD_SPI_HID_FLAG_WRITE, target,
		0, 0, body, message_len);
}

int pd_spi_hid_encode_device_info_request(pd_spi_hid_packet *packet,
	uint8_t request_id)
{
	return pd_spi_hid_encode_request(packet, PD_SPI_HID_DEV_INFO,
		PD_SPI_HID_TYPE_RESPONSE, PD_SPI_HID_U1_DEVICE_INFO,
		PD_SPI_HID_DEV_INFO, request_id, 0, NULL, 0);
}

int pd_spi_hid_encode_iface_info_request(pd_spi_hid_packet *packet,
	uint8_t request_id, uint8_t iface)
{
	return pd_spi_hid_encode_request(packet, PD_SPI_HID_DEV_INFO,
		PD_SPI_HID_TYPE_RESPONSE, PD_SPI_HID_U1_IFACE_INFO, iface,
		request_id, PD_SPI_HID_DESCRIPTOR_MAX, NULL, 0);
}

int pd_spi_hid_encode_descriptor_request(pd_spi_hid_packet *packet,
	uint8_t request_id, uint8_t iface)
{
	return pd_spi_hid_encode_request(packet, PD_SPI_HID_DEV_INFO,
		PD_SPI_HID_TYPE_RESPONSE, PD_SPI_HID_U1_DESCRIPTOR, iface,
		request_id, PD_SPI_HID_DESCRIPTOR_MAX, NULL, 0);
}

struct parsed_message {
	uint8_t type0;
	uint8_t type1;
	uint8_t type2;
	uint8_t request_id;
	uint16_t response_len;
	uint16_t payload_len;
};

static int message_acceptable(const uint8_t *msg, uint16_t message_len,
	struct parsed_message *parsed)
{
	uint16_t payload_len;
	uint16_t crc;

	if (msg == NULL || parsed == NULL)
		return 0;
	if (message_len < PD_SPI_HID_MSG_HEADER_SIZE + PD_SPI_HID_MSG_CRC_SIZE)
		return 0;
	if (message_len > PD_SPI_HID_MESSAGE_MAX)
		return 0;
	crc = pd_spi_hid_crc16(msg, (size_t)message_len - PD_SPI_HID_MSG_CRC_SIZE);
	if (crc != pd_spi_hid_load_le16(msg + message_len - PD_SPI_HID_MSG_CRC_SIZE))
		return 0;
	payload_len = pd_spi_hid_load_le16(msg + 6);
	if (payload_len == 0)
		return 0;
	if ((uint32_t)payload_len + PD_SPI_HID_MSG_HEADER_SIZE + PD_SPI_HID_MSG_CRC_SIZE
		> message_len)
		return 0;
	parsed->type0 = msg[0];
	parsed->type1 = msg[1];
	parsed->type2 = msg[2];
	parsed->request_id = msg[3];
	parsed->response_len = pd_spi_hid_load_le16(msg + 4);
	parsed->payload_len = payload_len;
	return 1;
}

static int publish(pd_spi_hid_reassembly *state, const uint8_t *msg,
	uint16_t message_len, uint8_t device, uint8_t flags,
	pd_spi_hid_delivery *out)
{
	struct parsed_message parsed;

	if (!message_acceptable(msg, message_len, &parsed))
		return 0;
	if (flags == PD_SPI_HID_FLAG_WRITE &&
		parsed.type0 == PD_SPI_HID_TYPE_RESPONSE &&
		parsed.type1 == PD_SPI_HID_U1_DESCRIPTOR &&
		parsed.payload_len > PD_SPI_HID_DESCRIPTOR_MAX)
		return 0;

	memcpy(state->delivered, msg, message_len);
	out->device = device;
	out->flags = flags;
	out->request_id = parsed.request_id;
	out->request_id_valid = 1;
	out->type0 = parsed.type0;
	out->type1 = parsed.type1;
	out->type2 = parsed.type2;
	out->response_len = parsed.response_len;
	out->payload = state->delivered + PD_SPI_HID_MSG_HEADER_SIZE;
	out->payload_len = parsed.payload_len;
	return 1;
}

static int assemble(pd_spi_hid_reassembly *state, const pd_spi_hid_packet *packet,
	uint16_t offset, uint16_t length, uint32_t span,
	pd_spi_hid_delivery *out)
{
	int continuous;

	continuous = state->total != 0 &&
		packet->device == state->device &&
		packet->flags == state->flags &&
		offset == state->filled;
	if (!continuous)
		abandon(state);

	if (offset == 0) {
		memcpy(state->assembling, packet->data, length);
		state->filled = length;
		state->total = (uint16_t)span;
		state->device = packet->device;
		state->flags = packet->flags;
		return PD_SPI_HID_NEED_MORE;
	}

	if (state->total == 0 || offset != state->filled)
		return PD_SPI_HID_REJECT;
	if (span != state->total)
		return PD_SPI_HID_REJECT;

	memcpy(state->assembling + offset, packet->data, length);
	state->filled = (uint16_t)(state->filled + length);
	if (state->filled != state->total)
		return PD_SPI_HID_NEED_MORE;

	if (!publish(state, state->assembling, state->total,
		state->device, state->flags, out)) {
		abandon(state);
		memset(out, 0, sizeof(*out));
		return PD_SPI_HID_REJECT;
	}
	abandon(state);
	return PD_SPI_HID_MESSAGE;
}

int pd_spi_hid_decode_packet(pd_spi_hid_reassembly *state,
	const pd_spi_hid_packet *packet, pd_spi_hid_delivery *out)
{
	uint16_t offset;
	uint16_t remain;
	uint16_t length;
	uint16_t actual;
	uint32_t span;

	if (out == NULL)
		return PD_SPI_HID_REJECT;
	memset(out, 0, sizeof(*out));
	if (state == NULL || packet == NULL)
		return PD_SPI_HID_REJECT;

	actual = pd_spi_hid_crc16((const uint8_t *)packet, PD_SPI_HID_PACKET_PREFIX);
	if (actual != pd_spi_hid_load_le16(packet->crc16_le))
		return PD_SPI_HID_REJECT;

	offset = pd_spi_hid_load_le16(packet->offset_le);
	remain = pd_spi_hid_load_le16(packet->remain_le);
	length = pd_spi_hid_load_le16(packet->length_le);
	span = (uint32_t)offset + length + remain;

	/* Boot and every other short packet, before a message header and
	 * before flags. Length 4 with a0 80 00 00 is the notice. */
	if (length < PD_SPI_HID_MSG_HEADER_SIZE + PD_SPI_HID_MSG_CRC_SIZE) {
		if (length == PD_SPI_HID_BOOT_LEN &&
			memcmp(packet->data, pd_spi_hid_boot_notice, PD_SPI_HID_BOOT_LEN) == 0) {
			memcpy(state->boot, packet->data, PD_SPI_HID_BOOT_LEN);
			out->device = packet->device;
			out->flags = packet->flags;
			out->payload = state->boot;
			out->payload_len = PD_SPI_HID_BOOT_LEN;
			return PD_SPI_HID_BOOT;
		}
		return PD_SPI_HID_REJECT;
	}

	if (length > PD_SPI_HID_PACKET_PAYLOAD)
		return PD_SPI_HID_REJECT;
	if (span > 65535u)
		return PD_SPI_HID_REJECT;
	if (packet->flags != PD_SPI_HID_FLAG_READ &&
		packet->flags != PD_SPI_HID_FLAG_WRITE)
		return PD_SPI_HID_REJECT;
	if (span > PD_SPI_HID_MESSAGE_MAX)
		return PD_SPI_HID_REJECT;

	if (offset == 0 && remain == 0) {
		if (!publish(state, packet->data, length, packet->device,
			packet->flags, out)) {
			memset(out, 0, sizeof(*out));
			return PD_SPI_HID_REJECT;
		}
		return PD_SPI_HID_MESSAGE;
	}

	return assemble(state, packet, offset, length, span, out);
}

int pd_spi_hid_write_status_ok(const uint8_t status[PD_SPI_HID_WRITE_STATUS_LEN])
{
	if (status == NULL)
		return 0;
	return memcmp(status, pd_spi_hid_write_status, PD_SPI_HID_WRITE_STATUS_LEN) == 0;
}

size_t pd_spi_hid_copy_bounded(uint8_t *dst, size_t dst_cap,
	const uint8_t *src, size_t src_len)
{
	size_t n = dst_cap < src_len ? dst_cap : src_len;

	if (n == 0 || dst == NULL || src == NULL)
		return 0;
	memcpy(dst, src, n);
	return n;
}
