# Host tests for the C this directory adds to the layer (README.md, "Host tests").
#
#   make test                 build and run (120 seeds x 20000 steps, under a minute)
#   make test SEEDS=3000      longer
#   make test LAYER=/path     the SiliconForest tree to take linux_radix.c/linux_xarray.c from;
#                             default: the layer ../m1-rust-kext/lkpi builds from (`finalTree`, else `tree`)
#
# The test links the layer's own, unmodified linux_radix.c and linux_xarray.c with
# c/sf_xarray.c; only the headers underneath them are stand-ins (tests/shim).
NIX     ?= $(HOME)/.nix-profile/bin/nix-build
NIXFLAGS = --extra-experimental-features nix-command --extra-experimental-features flakes
LAYER   ?= $(shell $(NIX) $(NIXFLAGS) -E "let l = import ../m1-rust-kext/lkpi; in l.finalTree or l.tree" --no-out-link 2>/dev/null)
LKPI     = $(LAYER)/src/Kernel/fbsdkpi/freebsd/linuxkpi/common
CC      ?= cc
CFLAGS  ?= -O1 -g -std=gnu11 -Wall -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=undefined
B        = build
SEEDS   ?= 120
STEPS   ?= 20000

test: $(B)/test_xarray
	$(B)/test_xarray $(SEEDS) $(STEPS)

$(B)/include/linux/xarray.h: $(LKPI)/include/linux/xarray.h $(LKPI)/include/linux/radix-tree.h
	@test -n "$(LAYER)" || { echo "LAYER is empty: nix-build of ../m1-rust-kext/lkpi -A tree failed" >&2; exit 1; }
	mkdir -p $(B)/include/linux
	cp $(LKPI)/include/linux/xarray.h $(LKPI)/include/linux/radix-tree.h $(B)/include/linux/

SRCS = tests/test_xarray.c c/sf_xarray.c $(LKPI)/src/linux_radix.c $(LKPI)/src/linux_xarray.c
$(B)/test_xarray: $(SRCS) c/include/linux/sf_xarray.h tests/shim/sf_host.h $(B)/include/linux/xarray.h
	$(CC) $(CFLAGS) -Itests/shim -I$(B)/include -Ic/include -include sf_host.h $(SRCS) -o $@

clean:
	rm -rf $(B)

.PHONY: test clean
