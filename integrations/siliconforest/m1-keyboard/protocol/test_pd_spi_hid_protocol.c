#include "pd_spi_hid_protocol.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void check(int cond, const char *name)
{
	if (!cond) {
		fprintf(stderr, "FAIL %s\n", name);
		failures++;
	}
}

static int silent(const pd_spi_hid_delivery *d)
{
	pd_spi_hid_delivery zero;

	memset(&zero, 0, sizeof(zero));
	return memcmp(d, &zero, sizeof(zero)) == 0;
}

static void seal(pd_spi_hid_packet *packet)
{
	pd_spi_hid_store_le16(packet->crc16_le,
		pd_spi_hid_crc16((const uint8_t *)packet, PD_SPI_HID_PACKET_PREFIX));
}

static int decode(pd_spi_hid_reassembly *state, const pd_spi_hid_packet *packet,
	pd_spi_hid_delivery *out)
{
	memset(out, 0xa5, sizeof(*out));
	return pd_spi_hid_decode_packet(state, packet, out);
}

static void fill_message(uint8_t *msg, uint16_t message_len,
	uint8_t type0, uint8_t type1, uint8_t type2, uint8_t id,
	uint16_t response_len, uint16_t payload_len, uint8_t fill)
{
	memset(msg, fill, message_len);
	msg[0] = type0;
	msg[1] = type1;
	msg[2] = type2;
	msg[3] = id;
	pd_spi_hid_store_le16(msg + 4, response_len);
	pd_spi_hid_store_le16(msg + 6, payload_len);
	pd_spi_hid_store_le16(msg + message_len - 2,
		pd_spi_hid_crc16(msg, (size_t)message_len - 2));
}

static void test_crc(void)
{
	const uint8_t digits[] = "123456789";
	const uint8_t one[] = { 0x01 };

	check(pd_spi_hid_crc16(digits, 9) == 0xbb3d, "crc16 arc check value");
	check(pd_spi_hid_crc16(one, 1) == 0xc0c1, "crc16 arc one byte");
	check(pd_spi_hid_crc16(NULL, 0) == 0, "crc16 empty");
	check(pd_spi_hid_crc16(NULL, 4) == 0, "crc16 null");
}

static void test_requests(void)
{
	pd_spi_hid_packet a, b;
	uint16_t msg_crc;

	check(pd_spi_hid_encode_device_info_request(&a, 7) == 0, "info encode");
	check(a.flags == PD_SPI_HID_FLAG_WRITE, "info flags");
	check(a.device == PD_SPI_HID_DEV_INFO, "info device");
	check(pd_spi_hid_load_le16(a.offset_le) == 0, "info offset");
	check(pd_spi_hid_load_le16(a.remain_le) == 0, "info remain");
	check(pd_spi_hid_load_le16(a.length_le) == 10, "info packet length");
	check(a.data[0] == 0x20 && a.data[1] == 0x01 && a.data[2] == 0xd0, "info header");
	check(a.data[3] == 7, "info request id");
	check(pd_spi_hid_load_le16(a.data + 4) == 0, "info rsplen");
	check(pd_spi_hid_load_le16(a.data + 6) == 0, "info payload length");
	msg_crc = pd_spi_hid_crc16(a.data, 8);
	check(pd_spi_hid_load_le16(a.data + 8) == msg_crc, "info message crc");
	check(pd_spi_hid_load_le16(a.crc16_le) ==
		pd_spi_hid_crc16((const uint8_t *)&a, PD_SPI_HID_PACKET_PREFIX),
		"info packet crc");
	check(pd_spi_hid_encode_device_info_request(&b, 7) == 0, "info encode again");
	check(memcmp(&a, &b, sizeof(a)) == 0, "request id is not a hidden counter");

	check(pd_spi_hid_encode_iface_info_request(&a, 3, 2) == 0, "iface encode");
	check(a.data[0] == 0x20 && a.data[1] == 0x02 && a.data[2] == 2, "iface header");
	check(pd_spi_hid_load_le16(a.data + 4) == 512, "iface rsplen");
	check(pd_spi_hid_load_le16(a.length_le) == 10, "iface packet length");

	check(pd_spi_hid_encode_descriptor_request(&a, 4, 1) == 0, "desc encode");
	check(a.device == 0xd0, "desc target");
	check(a.data[0] == 0x20 && a.data[1] == 0x10 && a.data[2] == 1, "desc header");
	check(pd_spi_hid_load_le16(a.data + 4) == 512, "desc rsplen");

	check(pd_spi_hid_encode_request(&a, 1, 0x51, 0x01, 0x00, 9, 0,
		(const uint8_t *)"x", 237) == -1, "request over 236");
	check(pd_spi_hid_encode_packet(&a, 0x20, 1, 0, 0, (const uint8_t *)"x", 247) == -1,
		"encode length above 246");
}

static void test_round_trip(pd_spi_hid_reassembly *state)
{
	pd_spi_hid_packet packet;
	pd_spi_hid_delivery out;
	const uint8_t body[] = { 0x11, 0x22, 0x33, 0x44 };
	int rc;

	pd_spi_hid_reassembly_reset(state);
	check(pd_spi_hid_encode_request(&packet, PD_SPI_HID_DEV_KBD,
		PD_SPI_HID_TYPE_INPUT, 0x01, 0x00, 9, 0, body, sizeof(body)) == 0,
		"synthetic input encode");
	rc = decode(state, &packet, &out);
	check(rc == PD_SPI_HID_MESSAGE, "synthetic input decodes");
	check(out.device == PD_SPI_HID_DEV_KBD, "input device");
	check(out.flags == PD_SPI_HID_FLAG_READ || out.flags == PD_SPI_HID_FLAG_WRITE,
		"input flags stored");
	check(out.flags == PD_SPI_HID_FLAG_WRITE, "framed request uses write flag");
	check(out.request_id_valid == 1 && out.request_id == 9, "input request id");
	check(out.type0 == PD_SPI_HID_TYPE_INPUT && out.type1 == 0x01 && out.type2 == 0x00,
		"input type bytes");
	check(out.payload_len == sizeof(body) && out.payload != NULL &&
		memcmp(out.payload, body, sizeof(body)) == 0, "raw payload bytes");
}

static void test_read_input(pd_spi_hid_reassembly *state)
{
	pd_spi_hid_packet packet;
	pd_spi_hid_delivery out;
	uint8_t msg[8 + 4 + 2];
	const uint8_t body[] = { 0x5a, 0xa5, 0x01, 0x02 };

	fill_message(msg, sizeof(msg), PD_SPI_HID_TYPE_INPUT, 0x01, 0x00, 4,
		0, 4, 0);
	memcpy(msg + 8, body, sizeof(body));
	pd_spi_hid_store_le16(msg + sizeof(msg) - 2, pd_spi_hid_crc16(msg, sizeof(msg) - 2));
	check(pd_spi_hid_encode_packet(&packet, PD_SPI_HID_FLAG_READ, PD_SPI_HID_DEV_KBD,
		0, 0, msg, sizeof(msg)) == 0, "read input packet");
	pd_spi_hid_reassembly_reset(state);
	check(decode(state, &packet, &out) == PD_SPI_HID_MESSAGE, "read input result");
	check(out.flags == PD_SPI_HID_FLAG_READ && out.device == 1, "read input metadata");
	check(out.type0 == 0x10 && out.request_id == 4, "read input header");
	check(out.payload_len == 4 && memcmp(out.payload, body, 4) == 0,
		"read input is raw and not a key event");
}

static void test_boot(pd_spi_hid_reassembly *state)
{
	pd_spi_hid_packet packet;
	pd_spi_hid_delivery out;
	const uint8_t flags[] = { 0x00, 0x20, 0x40, 0xff };
	size_t i;

	for (i = 0; i < sizeof(flags); i++) {
		uint8_t data[4] = {
			PD_SPI_HID_BOOT_B0, PD_SPI_HID_BOOT_B1,
			PD_SPI_HID_BOOT_B2, PD_SPI_HID_BOOT_B3
		};

		pd_spi_hid_reassembly_reset(state);
		check(pd_spi_hid_encode_packet(&packet, flags[i], 0, 0, 0, data, 4) == 0,
			"boot encode");
		packet.data[4] = 0xff;
		packet.data[5] = 0x01;
		packet.data[6] = 0x02;
		packet.data[7] = 0x03;
		seal(&packet);
		check(decode(state, &packet, &out) == PD_SPI_HID_BOOT, "boot result");
		check(out.flags == flags[i], "boot keeps unknown flags");
		check(out.request_id_valid == 0 && out.type0 == 0 && out.type1 == 0,
			"boot has no message header");
		check(out.payload_len == 4 && memcmp(out.payload, data, 4) == 0, "boot bytes");
	}

	pd_spi_hid_reassembly_reset(state);
	memset(&packet, 0, sizeof(packet));
	packet.flags = PD_SPI_HID_FLAG_READ;
	packet.data[0] = 0xa0;
	packet.data[1] = 0x80;
	pd_spi_hid_store_le16(packet.length_le, 8);
	seal(&packet);
	check(decode(state, &packet, &out) == PD_SPI_HID_REJECT, "length 8 is not boot");
	check(silent(&out), "length 8 silent");

	memset(&packet, 0, sizeof(packet));
	packet.data[0] = 0xa0;
	packet.data[1] = 0x80;
	packet.data[2] = 0x00;
	packet.data[3] = 0x01;
	pd_spi_hid_store_le16(packet.length_le, 4);
	seal(&packet);
	check(decode(state, &packet, &out) == PD_SPI_HID_REJECT, "wrong boot byte");
	check(silent(&out), "wrong boot silent");
}

static void test_crc_and_status(pd_spi_hid_reassembly *state)
{
	pd_spi_hid_packet packet, bad;
	pd_spi_hid_delivery out;
	uint8_t status[4] = {
		PD_SPI_HID_STATUS_B0, PD_SPI_HID_STATUS_B1,
		PD_SPI_HID_STATUS_B2, PD_SPI_HID_STATUS_B3
	};
	uint8_t msg[14];

	fill_message(msg, sizeof(msg), 0x10, 0x01, 0x00, 1, 0, 4, 0x3c);
	check(pd_spi_hid_encode_packet(&packet, PD_SPI_HID_FLAG_READ, 1, 0, 0, msg, sizeof(msg)) == 0,
		"crc fixture");
	bad = packet;
	bad.crc16_le[0] ^= 0xff;
	pd_spi_hid_reassembly_reset(state);
	check(decode(state, &bad, &out) == PD_SPI_HID_REJECT, "bad packet crc");
	check(silent(&out), "bad packet crc silent");
	check(decode(state, &packet, &out) == PD_SPI_HID_MESSAGE, "good packet after bad crc");

	bad = packet;
	bad.data[8] ^= 0xff;
	seal(&bad);
	check(decode(state, &bad, &out) == PD_SPI_HID_REJECT, "bad message crc");
	check(silent(&out), "bad message crc silent");

	check(pd_spi_hid_write_status_ok(status) == 1, "status ok");
	status[3] ^= 0x01;
	check(pd_spi_hid_write_status_ok(status) == 0, "status mismatch is failure");
	check(pd_spi_hid_write_status_ok(NULL) == 0, "status null");
	check(decode(state, &packet, &out) == PD_SPI_HID_MESSAGE,
		"decode does not consult write status");
}

static void test_bounds(pd_spi_hid_reassembly *state)
{
	pd_spi_hid_packet packet;
	pd_spi_hid_delivery out;
	uint8_t msg[20];

	pd_spi_hid_reassembly_reset(state);
	memset(&packet, 0, sizeof(packet));
	packet.flags = PD_SPI_HID_FLAG_READ;
	packet.device = 1;
	pd_spi_hid_store_le16(packet.length_le, 247);
	seal(&packet);
	check(decode(state, &packet, &out) == PD_SPI_HID_REJECT, "length 247");
	check(silent(&out), "length 247 silent");

	memset(&packet, 0, sizeof(packet));
	packet.flags = PD_SPI_HID_FLAG_READ;
	packet.device = 1;
	pd_spi_hid_store_le16(packet.length_le, 246);
	pd_spi_hid_store_le16(packet.remain_le, 1803);
	seal(&packet);
	check(decode(state, &packet, &out) == PD_SPI_HID_REJECT, "total 2049");
	check(silent(&out), "total 2049 silent");

	memset(&packet, 0, sizeof(packet));
	packet.flags = PD_SPI_HID_FLAG_READ;
	pd_spi_hid_store_le16(packet.offset_le, 65530);
	pd_spi_hid_store_le16(packet.length_le, 10);
	pd_spi_hid_store_le16(packet.remain_le, 10);
	seal(&packet);
	check(decode(state, &packet, &out) == PD_SPI_HID_REJECT, "span above uint16");
	check(silent(&out), "span silent");

	fill_message(msg, sizeof(msg), 0x10, 1, 0, 1, 0, 5, 0);
	/* payload_len 5 plus header and crc is 15, message buffer is 20. */
	check(5 + 10 <= sizeof(msg), "fixture has a gap");
	check(pd_spi_hid_encode_packet(&packet, PD_SPI_HID_FLAG_READ, 1, 0, 0,
		msg, sizeof(msg)) == 0, "gap encode");
	check(decode(state, &packet, &out) == PD_SPI_HID_MESSAGE, "payload_len+10 < length");
	check(out.payload_len == 5, "gap payload length");

	fill_message(msg, sizeof(msg), 0x10, 1, 0, 1, 0, 20, 0);
	check(pd_spi_hid_encode_packet(&packet, PD_SPI_HID_FLAG_READ, 1, 0, 0,
		msg, sizeof(msg)) == 0, "oversize header length encode");
	check(decode(state, &packet, &out) == PD_SPI_HID_REJECT, "payload_len+10 > length");
	check(silent(&out), "overlong payload silent");

	fill_message(msg, 10, 0x10, 1, 0, 1, 0, 0, 0);
	check(pd_spi_hid_encode_packet(&packet, PD_SPI_HID_FLAG_READ, 1, 0, 0, msg, 10) == 0,
		"zero payload encode");
	check(decode(state, &packet, &out) == PD_SPI_HID_REJECT, "zero payload");
	check(silent(&out), "zero payload silent");
}

static void test_fragments(pd_spi_hid_reassembly *state)
{
	pd_spi_hid_packet first, second, bad;
	pd_spi_hid_delivery out;
	uint8_t msg[30];
	uint8_t saved[16];

	fill_message(msg, sizeof(msg), PD_SPI_HID_TYPE_INPUT, 0x01, 0x00, 6, 0, 20, 0x2a);
	check(pd_spi_hid_encode_packet(&first, PD_SPI_HID_FLAG_READ, PD_SPI_HID_DEV_KBD,
		0, 14, msg, 16) == 0, "frag first");
	check(pd_spi_hid_encode_packet(&second, PD_SPI_HID_FLAG_READ, PD_SPI_HID_DEV_KBD,
		16, 0, msg + 16, 14) == 0, "frag second");

	pd_spi_hid_reassembly_reset(state);
	check(decode(state, &second, &out) == PD_SPI_HID_REJECT, "wrong order");
	check(silent(&out), "wrong order silent");
	check(decode(state, &first, &out) == PD_SPI_HID_NEED_MORE, "ordered first");
	check(silent(&out), "partial has no payload");
	check(decode(state, &second, &out) == PD_SPI_HID_MESSAGE, "ordered second");
	check(out.payload_len == 20 && memcmp(out.payload, msg + 8, 20) == 0, "joined payload");

	pd_spi_hid_reassembly_reset(state);
	check(decode(state, &first, &out) == PD_SPI_HID_NEED_MORE, "restart first");
	bad = second;
	bad.device = PD_SPI_HID_DEV_TP;
	seal(&bad);
	check(decode(state, &bad, &out) == PD_SPI_HID_REJECT, "device mismatch");
	check(silent(&out), "device mismatch silent");
	check(decode(state, &second, &out) == PD_SPI_HID_REJECT,
		"cleared fragment does not resume");
	check(silent(&out), "resume silent");

	check(decode(state, &first, &out) == PD_SPI_HID_NEED_MORE, "continuity first");
	bad = second;
	pd_spi_hid_store_le16(bad.remain_le, 1);
	seal(&bad);
	check(decode(state, &bad, &out) == PD_SPI_HID_REJECT, "total changed");
	check(silent(&out), "total changed silent");
	check(decode(state, &second, &out) == PD_SPI_HID_MESSAGE, "original total still open");

	pd_spi_hid_reassembly_reset(state);
	check(decode(state, &first, &out) == PD_SPI_HID_NEED_MORE, "crc prefix");
	bad = second;
	bad.crc16_le[0] ^= 1;
	check(decode(state, &bad, &out) == PD_SPI_HID_REJECT, "bad fragment crc");
	check(silent(&out), "bad fragment crc silent");
	check(decode(state, &second, &out) == PD_SPI_HID_MESSAGE, "crc failure keeps prefix");

	pd_spi_hid_reassembly_reset(state);
	check(decode(state, &first, &out) == PD_SPI_HID_NEED_MORE, "flag prefix");
	memcpy(saved, state->assembling, 16);
	bad = second;
	bad.flags = 0x00;
	seal(&bad);
	check(decode(state, &bad, &out) == PD_SPI_HID_REJECT, "unknown flags");
	check(silent(&out), "unknown flags silent");
	check(memcmp(state->assembling, saved, 16) == 0, "unknown flags leave the prefix");
	check(decode(state, &second, &out) == PD_SPI_HID_MESSAGE, "prefix survives unknown flags");

	pd_spi_hid_reassembly_reset(state);
	check(decode(state, &first, &out) == PD_SPI_HID_NEED_MORE, "boot during fragment");
	check(pd_spi_hid_encode_packet(&bad, 0x7f, 9, 0, 0, pd_spi_hid_boot_notice, 4) == 0,
		"boot during encode");
	check(decode(state, &bad, &out) == PD_SPI_HID_BOOT, "boot during result");
	check(out.request_id_valid == 0 && out.payload_len == 4, "boot during metadata");
	check(decode(state, &second, &out) == PD_SPI_HID_MESSAGE, "fragment continues after boot");

	/* Length below 10 is not a fragment and does not clear one. */
	pd_spi_hid_reassembly_reset(state);
	check(decode(state, &first, &out) == PD_SPI_HID_NEED_MORE, "short-junk prefix");
	check(pd_spi_hid_encode_packet(&bad, PD_SPI_HID_FLAG_READ, PD_SPI_HID_DEV_KBD,
		0, 0, msg, 3) == 0, "short junk encode");
	check(decode(state, &bad, &out) == PD_SPI_HID_REJECT, "short junk");
	check(silent(&out), "short junk silent");
	check(state->filled == 16 && state->total == 30, "short junk keeps prefix");
	check(decode(state, &second, &out) == PD_SPI_HID_MESSAGE, "short junk then complete");

	/* A complete interleaved packet with a bad message CRC is unrelated. */
	{
		uint8_t one[15];

		fill_message(one, sizeof(one), 0x10, 0, 0, 1, 0, 5, 0x11);
		one[13] ^= 1;
		pd_spi_hid_reassembly_reset(state);
		check(decode(state, &first, &out) == PD_SPI_HID_NEED_MORE, "badmsg prefix");
		check(pd_spi_hid_encode_packet(&bad, PD_SPI_HID_FLAG_READ, 1, 0, 0,
			one, sizeof(one)) == 0, "badmsg encode");
		check(decode(state, &bad, &out) == PD_SPI_HID_REJECT,
			"bad interleaved message crc");
		check(silent(&out), "bad interleaved silent");
		check(state->filled == 16, "bad interleaved keeps prefix");
		check(decode(state, &second, &out) == PD_SPI_HID_MESSAGE,
			"bad interleaved then complete");
	}

	/* The last piece below 10 bytes never joins. The prefix stays open. */
	{
		uint8_t tight[20];

		fill_message(tight, sizeof(tight), 0x10, 1, 0, 3, 0, 10, 0x44);
		pd_spi_hid_reassembly_reset(state);
		check(pd_spi_hid_encode_packet(&first, PD_SPI_HID_FLAG_READ, 1,
			0, 5, tight, 15) == 0, "short-final first");
		check(pd_spi_hid_encode_packet(&second, PD_SPI_HID_FLAG_READ, 1,
			15, 0, tight + 15, 5) == 0, "short-final second");
		check(decode(state, &first, &out) == PD_SPI_HID_NEED_MORE, "short-final stored");
		check(decode(state, &second, &out) == PD_SPI_HID_REJECT, "final fragment under 10");
		check(silent(&out), "short-final silent");
		check(state->filled == 15 && state->total == 20, "short-final prefix remains");
	}
}

static void test_descriptor_cap(pd_spi_hid_reassembly *state)
{
	uint8_t msg[8 + 513 + 2];
	pd_spi_hid_packet a, b, c;
	pd_spi_hid_delivery out;
	uint16_t message_len;
	uint16_t payload_len;

	for (payload_len = 512; payload_len <= 513; payload_len++) {
		message_len = (uint16_t)(8 + payload_len + 2);
		fill_message(msg, message_len, PD_SPI_HID_TYPE_RESPONSE,
			PD_SPI_HID_U1_DESCRIPTOR, 1, 2, 512, payload_len, 0x5a);
		check(pd_spi_hid_encode_packet(&a, PD_SPI_HID_FLAG_WRITE, PD_SPI_HID_DEV_INFO,
			0, (uint16_t)(message_len - 246), msg, 246) == 0, "desc frag 0");
		check(pd_spi_hid_encode_packet(&b, PD_SPI_HID_FLAG_WRITE, PD_SPI_HID_DEV_INFO,
			246, (uint16_t)(message_len - 492), msg + 246, 246) == 0, "desc frag 1");
		check(pd_spi_hid_encode_packet(&c, PD_SPI_HID_FLAG_WRITE, PD_SPI_HID_DEV_INFO,
			492, 0, msg + 492, (uint16_t)(message_len - 492)) == 0, "desc frag 2");
		pd_spi_hid_reassembly_reset(state);
		check(decode(state, &a, &out) == PD_SPI_HID_NEED_MORE, "desc need 0");
		check(silent(&out), "desc partial 0 silent");
		check(decode(state, &b, &out) == PD_SPI_HID_NEED_MORE, "desc need 1");
		check(silent(&out), "desc partial 1 silent");
		if (payload_len == 512) {
			check(decode(state, &c, &out) == PD_SPI_HID_MESSAGE, "desc 512");
			check(out.payload_len == 512 && out.type1 == 0x10, "desc 512 metadata");
			check(out.payload[0] == 0x5a && out.payload[511] == 0x5a, "desc bytes");
		} else {
			check(decode(state, &c, &out) == PD_SPI_HID_REJECT, "desc 513");
			check(silent(&out), "desc 513 silent");
		}
	}
}

static void test_copy(void)
{
	uint8_t small[4];
	uint8_t dst[8];
	const uint8_t src[] = { 1, 2, 3, 4, 5, 6 };

	memset(small, 0xff, sizeof(small));
	check(pd_spi_hid_copy_bounded(small, sizeof(small), src, sizeof(src)) == 4,
		"copy min");
	check(small[0] == 1 && small[3] == 4, "copy bytes");
	memset(dst, 0xff, sizeof(dst));
	check(pd_spi_hid_copy_bounded(dst, sizeof(dst), src, sizeof(src)) == 6,
		"copy src smaller");
	check(dst[4] == 5 && dst[5] == 6 && dst[6] == 0xff, "copy stopped at src");
	check(pd_spi_hid_copy_bounded(NULL, sizeof(small), src, sizeof(src)) == 0,
		"copy null dst");
	check(pd_spi_hid_copy_bounded(dst, sizeof(dst), NULL, sizeof(src)) == 0,
		"copy null src");
}

int main(void)
{
	static pd_spi_hid_reassembly state;

	test_crc();
	test_requests();
	test_round_trip(&state);
	test_read_input(&state);
	test_boot(&state);
	test_crc_and_status(&state);
	test_bounds(&state);
	test_fragments(&state);
	test_descriptor_cap(&state);
	test_copy();
	if (failures != 0) {
		fprintf(stderr, "pd_spi_hid_protocol: %d failed\n", failures);
		return 1;
	}
	printf("pd_spi_hid_protocol: host checks passed\n");
	return 0;
}
