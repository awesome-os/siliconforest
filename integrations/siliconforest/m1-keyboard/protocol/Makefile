# Host-only checks. No Nix, no cross link, no hardware.
CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror

.PHONY: test
test:
	$(CC) $(CFLAGS) -o /tmp/pd-spi-hid-protocol-test \
		test_pd_spi_hid_protocol.c pd_spi_hid_protocol.c
	/tmp/pd-spi-hid-protocol-test
