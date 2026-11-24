# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Host test of the table code. `make test`; needs only cc.
CC ?= cc
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined

build/table_test: test/table_test.c src/sf_dart_table.c include/sf_dart_table.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude -o $@ test/table_test.c src/sf_dart_table.c

build/retire_test: test/retire_test.c src/sf_dart_table.c include/sf_dart_table.h include/sf_dart_retire.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude -o $@ test/retire_test.c src/sf_dart_table.c

build/group_test: test/group_test.c src/sf_dart_table.c include/sf_dart_table.h include/sf_dart_retire.h include/sf_dart_group.h include/sf_dart_pair.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude -o $@ test/group_test.c src/sf_dart_table.c

build/spec_test: test/spec_test.c include/sf_dart_spec.h ../m1-gpu-dt/src/dispdt.c ../m1-gpu-dt/src/dispdt.h
	mkdir -p build
	$(CC) $(CFLAGS) -Iinclude -I../m1-gpu-dt/src -o $@ test/spec_test.c ../m1-gpu-dt/src/dispdt.c

test: build/table_test build/retire_test build/group_test build/spec_test
	./build/table_test
	./build/retire_test
	./build/group_test
	./build/spec_test

clean:
	rm -rf build

.PHONY: test clean
