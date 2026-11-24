/* SPDX-License-Identifier: MIT */
/*
 * sfplay: plays PCM through SFAppleSound: /dev/sfaudio (the J293 headphone jack) or
 * /dev/sfspeaker.
 *
 *   sfplay file.wav              a RIFF/WAVE file: PCM, 8/16/24/32 bit, mono or stereo,
 *                                44100 or 48000 Hz
 *   sfplay -t <hz> [-s <sec>] [-d <dB below full scale>] [-r <rate>] [-c l|r|lr]
 *                                a sine tone (default 2 s at -20 dB, both channels)
 *   sfplay -v <dB>               set the headphone codec attenuation first (0..63, 63 mutes)
 *   sfplay -o speakers ...       play through /dev/sfspeaker (the built-in speakers; their
 *                                volume is the safety daemon's, not this tool's)
 *
 * The device takes signed 16-bit little-endian stereo at kern.sf_sound.rate; this tool
 * sets that rate (it can only change while nothing plays) and converts.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>

/* -o speakers: the built-in speakers instead of the headphone jack */
static const char *DEVICE = "/dev/sfaudio";
static const char *RATE_SYSCTL = "kern.sf_sound.rate";

static int
set_sysctl(const char *name, int value)
{
	if (sysctlbyname(name, NULL, NULL, &value, sizeof(value)) != 0) {
		fprintf(stderr, "sfplay: %s=%d: %s\n", name, value, strerror(errno));
		return -1;
	}
	return 0;
}

/* The rate changes only when the previous session has ended: wait for that briefly. */
static int
set_rate(int rate)
{
	for (int i = 0; i < 50; i++) {
		int now = 0;
		size_t len = sizeof(now);
		if (sysctlbyname(RATE_SYSCTL, &now, &len, NULL, 0) == 0 && now == rate)
			return 0;
		if (sysctlbyname(RATE_SYSCTL, NULL, NULL, &rate, sizeof(rate)) == 0)
			return 0;
		if (errno != EBUSY)
			break;
		usleep(100000);
	}
	fprintf(stderr, "sfplay: cannot set %s=%d: %s\n", RATE_SYSCTL, rate, strerror(errno));
	return -1;
}

static int
write_all(int fd, const void *buf, size_t len)
{
	const char *p = buf;
	while (len > 0) {
		ssize_t n = write(fd, p, len);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "sfplay: write: %s\n", strerror(errno));
			return -1;
		}
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

/* sin(pi * t / 32768) in Q15 for t in [-32768, 32767]: a parabola and one correction step */
static int32_t
sine_q15(int32_t t)
{
	int32_t a = t < 0 ? -t : t;
	int32_t y = (int32_t)(((int64_t)4 * t * (32768 - a)) >> 15);
	int32_t ay = y < 0 ? -y : y;
	int32_t z = (int32_t)(((int64_t)y * ay) >> 15);

	y += (int32_t)(((int64_t)7373 * (z - y)) >> 15);
	return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}

static int
play_tone(int fd, int hz, int seconds, int db, int rate, const char *channels)
{
	static const uint32_t gainq16[6] = { 65536, 58409, 52057, 46396, 41350, 36854 };
	uint32_t gain = db / 6 >= 16 ? 0 : gainq16[db % 6] >> (db / 6);
	uint32_t phase = 0, step = (uint32_t)(((uint64_t)hz << 32) / (uint32_t)rate);
	int left = strchr(channels, 'l') != NULL, right = strchr(channels, 'r') != NULL;
	int16_t buf[2 * 1024];
	long total = (long)seconds * rate, done = 0;

	while (done < total) {
		int n = total - done > 1024 ? 1024 : (int)(total - done);
		for (int i = 0; i < n; i++) {
			int16_t s = (int16_t)(((int64_t)sine_q15((int32_t)(phase >> 16) - 32768) * gain) >> 16);
			/* fade the first and last 10 ms so the tone does not click */
			long edge = done + i < total - done - i ? done + i : total - done - i - 1;
			long ramp = rate / 100;
			if (edge < ramp)
				s = (int16_t)((long)s * edge / ramp);
			buf[2 * i] = left ? s : 0;
			buf[2 * i + 1] = right ? s : 0;
			phase += step;
		}
		if (write_all(fd, buf, (size_t)n * 4) != 0)
			return 1;
		done += n;
	}
	printf("sfplay: tone %d Hz, %d s, -%d dB, %d Hz, channels %s: %ld frames\n", hz, seconds, db, rate, channels, done);
	return 0;
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static int
read_all(int fd, void *buf, size_t len)
{
	char *p = buf;
	size_t got = 0;
	while (got < len) {
		ssize_t n = read(fd, p + got, len - got);
		if (n <= 0)
			break;
		got += (size_t)n;
	}
	return (int)got;
}

static int
play_wav(const char *path)
{
	uint8_t hdr[12], chunk[8], fmt[40];
	int in = open(path, O_RDONLY), out, channels = 0, bits = 0, rate = 0, format = 0;
	uint32_t datalen = 0;
	long frames = 0;

	if (in < 0) {
		fprintf(stderr, "sfplay: %s: %s\n", path, strerror(errno));
		return 1;
	}
	if (read_all(in, hdr, 12) != 12 || memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
		fprintf(stderr, "sfplay: %s is not a RIFF/WAVE file\n", path);
		return 1;
	}
	for (;;) {
		uint32_t len;
		if (read_all(in, chunk, 8) != 8) {
			fprintf(stderr, "sfplay: %s has no data chunk\n", path);
			return 1;
		}
		len = rd32(chunk + 4);
		if (memcmp(chunk, "fmt ", 4) == 0 && len >= 16 && len <= sizeof(fmt)) {
			if (read_all(in, fmt, len) != (int)len)
				return 1;
			format = rd16(fmt);
			channels = rd16(fmt + 2);
			rate = (int)rd32(fmt + 4);
			bits = rd16(fmt + 14);
			if (format == 0xfffe && len >= 26)      /* extensible: the sub-format's first word */
				format = rd16(fmt + 24);
			if (len & 1)
				lseek(in, 1, SEEK_CUR);
		} else if (memcmp(chunk, "data", 4) == 0) {
			datalen = len;
			break;
		} else {
			lseek(in, (off_t)len + (len & 1), SEEK_CUR);
		}
	}
	if (format != 1 || (channels != 1 && channels != 2) || (bits != 8 && bits != 16 && bits != 24 && bits != 32) ||
	    (rate != 44100 && rate != 48000)) {
		fprintf(stderr, "sfplay: unsupported format %d, %d channels, %d bits, %d Hz\n", format, channels, bits, rate);
		return 1;
	}
	if (set_rate(rate) != 0)
		return 1;
	out = open(DEVICE, O_WRONLY);
	if (out < 0) {
		fprintf(stderr, "sfplay: %s: %s\n", DEVICE, strerror(errno));
		return 1;
	}
	{
		int bytes = bits / 8, framebytes = bytes * channels;
		uint8_t raw[1024 * 8];
		int16_t buf[2 * 1024];
		uint32_t left = datalen;

		while (left >= (uint32_t)framebytes) {
			int want = left / (uint32_t)framebytes > 1024 ? 1024 : (int)(left / (uint32_t)framebytes);
			int got = read_all(in, raw, (size_t)want * (size_t)framebytes) / framebytes;
			if (got <= 0)
				break;
			for (int i = 0; i < got; i++) {
				int16_t s[2];
				for (int c = 0; c < channels; c++) {
					const uint8_t *p = raw + i * framebytes + c * bytes;
					/* the two most significant bytes; 8-bit WAVE is unsigned */
					s[c] = bytes == 1 ? (int16_t)((p[0] - 128) << 8) : (int16_t)rd16(p + bytes - 2);
				}
				buf[2 * i] = s[0];
				buf[2 * i + 1] = channels == 2 ? s[1] : s[0];
			}
			if (write_all(out, buf, (size_t)got * 4) != 0)
				return 1;
			frames += got;
			left -= (uint32_t)got * (uint32_t)framebytes;
		}
	}
	close(out);
	printf("sfplay: %s: %d channels, %d bits, %d Hz: %ld frames\n", path, channels, bits, rate, frames);
	return 0;
}

int
main(int argc, char **argv)
{
	int hz = 0, seconds = 2, db = 20, rate = 48000, volume = -1, c;
	const char *channels = "lr";

	while ((c = getopt(argc, argv, "t:s:d:r:c:v:o:")) != -1) {
		switch (c) {
		case 't': hz = atoi(optarg); break;
		case 's': seconds = atoi(optarg); break;
		case 'd': db = atoi(optarg); break;
		case 'r': rate = atoi(optarg); break;
		case 'c': channels = optarg; break;
		case 'v': volume = atoi(optarg); break;
		case 'o':
			if (strcmp(optarg, "speakers") == 0) {
				DEVICE = "/dev/sfspeaker";
				RATE_SYSCTL = "kern.sf_speaker.rate";
			} else if (strcmp(optarg, "headphones") != 0) {
				fprintf(stderr, "sfplay: -o headphones|speakers\n");
				return 2;
			}
			break;
		default:
			fprintf(stderr, "usage: sfplay [-v dB] file.wav | sfplay [-v dB] -t hz [-s sec] [-d dB] [-r rate] [-c l|r|lr]\n");
			return 2;
		}
	}
	if (volume >= 0 && set_sysctl("kern.sf_sound.volume_db", volume) != 0)
		return 1;
	if (hz > 0) {
		int fd, ret;
		if (hz > rate / 2 || seconds < 1 || seconds > 600 || db < 6 || db > 96 || (rate != 44100 && rate != 48000)) {
			fprintf(stderr, "sfplay: tone parameters out of range\n");
			return 2;
		}
		if (set_rate(rate) != 0)
			return 1;
		fd = open(DEVICE, O_WRONLY);
		if (fd < 0) {
			fprintf(stderr, "sfplay: %s: %s\n", DEVICE, strerror(errno));
			return 1;
		}
		ret = play_tone(fd, hz, seconds, db, rate, channels);
		close(fd);
		return ret;
	}
	if (optind < argc)
		return play_wav(argv[optind]);
	if (volume >= 0)
		return 0;
	fprintf(stderr, "sfplay: nothing to play\n");
	return 2;
}
