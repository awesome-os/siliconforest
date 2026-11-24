// SPDX-License-Identifier: GPL-2.0
/*
 * Apple Z2 touch digitizer. Ported from Linux drivers/input/touchscreen/apple_z2.c as
 * carried in omacom/linux-aurora (apple_z2_parse_touches, apple_z2_read_packet,
 * apple_z2_build_cal_blob, apple_z2_send_firmware_blob, apple_z2_upload_firmware,
 * apple_z2_boot):
 *   Copyright (C) The Asahi Linux Contributors
 */
#include "sf_z2.h"

#include <IOKit/IOLib.h>
#include <string.h>
#include <sys/errno.h>

#define LOG(fmt, ...) IOLog("SFZ2: " fmt "\n", ##__VA_ARGS__)

#define APPLE_Z2_NUM_FINGERS_OFFSET      16
#define APPLE_Z2_FINGERS_OFFSET          24
#define APPLE_Z2_FINGER_BYTES            30     /* struct apple_z2_finger */
#define APPLE_Z2_CMD_READ_INTERRUPT_DATA 0xEB
#define APPLE_Z2_REPLY_INTERRUPT_DATA    0xE1
#define APPLE_Z2_HBPP_CMD_BLOB           0x3001
#define APPLE_Z2_FW_MAGIC                0x5746325A
#define LOAD_COMMAND_INIT_PAYLOAD        0
#define LOAD_COMMAND_SEND_BLOB           1
#define LOAD_COMMAND_SEND_CALIBRATION    2
#define HBPP_HDR_BYTES                   10     /* struct apple_z2_hbpp_blob_hdr */

static inline uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

bool
SFZ2::attach(SFSpi *s, SFGpio *g, unsigned cs, unsigned rst, uint32_t speed)
{
	spi = s;
	gpio = g;
	pinCS = cs;
	pinReset = rst;
	hz = speed;
	rx = (uint8_t *)IOMalloc(SF_Z2_RX_BYTES);
	if (rx == NULL)
		return false;
	gpio->output(pinCS, 1);         /* chip select, active low: released */
	reset(true);                    /* devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH) */
	return true;
}

void
SFZ2::reset(bool asserted)
{
	/* reset-gpios is GPIO_ACTIVE_LOW */
	gpio->output(pinReset, asserted ? 0 : 1);
	if (asserted)
		booted = false;
}

/* spi-cs-setup-delay-ns and spi-cs-hold-delay-ns of the device tree: 2000 */
static inline void cs_assert(SFGpio *g, unsigned pin) { g->set(pin, 0); IODelay(2); }
static inline void cs_release(SFGpio *g, unsigned pin) { IODelay(2); g->set(pin, 1); IODelay(2); }

void
SFZ2::parse(const uint8_t *msg, size_t len)
{
	if (len <= APPLE_Z2_NUM_FINGERS_OFFSET)
		return;
	unsigned n = msg[APPLE_Z2_NUM_FINGERS_OFFSET];
	if (APPLE_Z2_FINGERS_OFFSET + (size_t)n * APPLE_Z2_FINGER_BYTES > len) {
		/* Linux trusts the count; a count the packet cannot hold is counted and dropped here */
		badReports++;
		return;
	}
	for (unsigned i = 0; i < n; i++) {
		const uint8_t *f = msg + APPLE_Z2_FINGERS_OFFSET + i * APPLE_Z2_FINGER_BYTES;
		fingersSeen++;
		if (finger != NULL)
			finger(fingerCtx, f[0], f[1], le16(f + 4), le16(f + 6));
	}
}

int
SFZ2::readPacket()
{
	int err;

	memset(tx, 0, sizeof(tx));
	tx[0] = APPLE_Z2_CMD_READ_INTERRUPT_DATA;
	tx[1] = (uint8_t)(indexParity + 1);
	tx[14] = (uint8_t)((APPLE_Z2_CMD_READ_INTERRUPT_DATA + tx[1]) & 0xff);
	tx[15] = (uint8_t)((APPLE_Z2_CMD_READ_INTERRUPT_DATA + tx[1]) >> 8);
	indexParity = !indexParity;

	cs_assert(gpio, pinCS);
	err = spi->transfer(tx, rx, sizeof(tx), 8, hz);
	cs_release(gpio, pinCS);
	polls++;
	if (err != 0) {
		spiErrors++;
		return err;
	}
	memcpy(lastHeader, rx, sizeof(lastHeader));
	if (rx[0] != APPLE_Z2_REPLY_INTERRUPT_DATA)
		return 0;
	replies++;

	size_t pktLen = ((size_t)le16(rx + 1) + 8) & 0xfffffffc;
	if (pktLen < 8 || pktLen > SF_Z2_RX_BYTES) {
		badReports++;
		return 0;
	}
	cs_assert(gpio, pinCS);
	err = spi->transfer(NULL, rx, (uint32_t)pktLen, 8, hz);
	cs_release(gpio, pinCS);
	if (err != 0) {
		spiErrors++;
		return err;
	}
	reports++;
	lastReportLen = (uint32_t)pktLen;
	memcpy(lastReport, rx, pktLen < sizeof(lastReport) ? pktLen : sizeof(lastReport));
	parse(rx + 5, pktLen - 5);
	return 0;
}

/* apple_z2_send_firmware_blob(): the blob and the two-byte acknowledge in one chip select */
static int
send_blob(SFZ2 *z2, const uint8_t *data, uint32_t size, bool init, void (*wait)(unsigned ms))
{
	static const uint8_t ack[2] = { 0x1a, 0xa1 };
	int err;

	cs_assert(z2->gpio, z2->pinCS);
	err = z2->spi->transfer(data, NULL, size, init ? 8 : 16, z2->hz);
	if (err == 0)
		err = z2->spi->transfer(ack, NULL, sizeof(ack), 8, z2->hz);
	cs_release(z2->gpio, z2->pinCS);
	if (err != 0) {
		z2->spiErrors++;
		return err;
	}
	z2->blobs++;
	/* "Irq only happens sometimes, but the thing boots reliably nonetheless" */
	wait(20);
	return 0;
}

int
SFZ2::upload(const uint8_t *fw, size_t fwLen, const uint8_t *cal, size_t calLen, void (*wait)(unsigned ms))
{
	size_t idx = 8;
	int err = 0;

	if (fwLen < 8 || le32(fw) != APPLE_Z2_FW_MAGIC || le32(fw + 4) != 1) {
		LOG("invalid firmware header");
		return EINVAL;
	}
	while (err == 0 && idx < fwLen) {
		if (fwLen - idx < 8) {
			err = EINVAL;
			break;
		}
		uint32_t cmd = le32(fw + idx);
		idx += 4;
		if (cmd == LOAD_COMMAND_INIT_PAYLOAD || cmd == LOAD_COMMAND_SEND_BLOB) {
			uint32_t size = le32(fw + idx);
			idx += 4;
			if (fwLen - idx < size) {
				err = EINVAL;
				break;
			}
			err = send_blob(this, fw + idx, size, cmd == LOAD_COMMAND_INIT_PAYLOAD, wait);
			idx += size;
		} else if (cmd == LOAD_COMMAND_SEND_CALIBRATION) {
			uint32_t address = le32(fw + idx);
			idx += 4;
			if (cal != NULL && calLen != 0) {
				/* apple_z2_build_cal_blob() */
				size_t blobSize = HBPP_HDR_BYTES + calLen + 4;
				uint8_t *blob = (uint8_t *)IOMalloc(blobSize);
				if (blob == NULL) {
					err = ENOMEM;
					break;
				}
				memset(blob, 0, blobSize);
				blob[0] = APPLE_Z2_HBPP_CMD_BLOB & 0xff;
				blob[1] = APPLE_Z2_HBPP_CMD_BLOB >> 8;
				uint16_t words = (uint16_t)(((calLen + 3) & ~(size_t)3) / 4);
				blob[2] = (uint8_t)words;
				blob[3] = (uint8_t)(words >> 8);
				blob[4] = (uint8_t)address;
				blob[5] = (uint8_t)(address >> 8);
				blob[6] = (uint8_t)(address >> 16);
				blob[7] = (uint8_t)(address >> 24);
				uint16_t sumHdr = 0;
				for (int i = 2; i < 8; i++)
					sumHdr += blob[i];
				blob[8] = (uint8_t)sumHdr;
				blob[9] = (uint8_t)(sumHdr >> 8);
				memcpy(blob + HBPP_HDR_BYTES, cal, calLen);
				uint32_t sum = 0;
				for (size_t i = 0; i < calLen; i++)
					sum += cal[i];
				blob[HBPP_HDR_BYTES + calLen] = (uint8_t)sum;
				blob[HBPP_HDR_BYTES + calLen + 1] = (uint8_t)(sum >> 8);
				blob[HBPP_HDR_BYTES + calLen + 2] = (uint8_t)(sum >> 16);
				blob[HBPP_HDR_BYTES + calLen + 3] = (uint8_t)(sum >> 24);
				err = send_blob(this, blob, (uint32_t)blobSize, false, wait);
				IOFree(blob, blobSize);
			}
		} else {
			err = EINVAL;
		}
		idx = (idx + 3) & ~(size_t)3;
	}
	if (err != 0) {
		LOG("firmware upload failed at offset %lu of %lu: error %d (%u blobs sent)", (unsigned long)idx,
		    (unsigned long)fwLen, err, blobs);
		reset(true);
		return err;
	}
	booted = true;
	readPacket();
	return 0;
}
