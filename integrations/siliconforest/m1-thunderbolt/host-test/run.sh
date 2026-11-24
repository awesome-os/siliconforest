#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
${CC:-cc} -std=gnu11 -O1 -g -fsanitize=address,undefined \
  -I "$here/upstream" -I "$here/portable" \
  "$here/upstream/apple-dpin-state.c" "$here/src/sf_dpin_policy.c" \
  "$here/upstream/apple-dpin-test.c" -o "$tmp/dpin"
"$tmp/dpin"
${CC:-cc} -std=c11 -O1 -g -fsanitize=address,undefined -I "$here/include" -c "$here/src/sf_apple_nhi.c" -o "$tmp/nhi.o"
${CXX:-c++} -std=c++14 -O1 -g -fsanitize=address,undefined -I "$here/include" "$here/host-test/nhi.cpp" "$tmp/nhi.o" -o "$tmp/nhi"
"$tmp/nhi"
${CC:-cc} -std=c11 -O1 -g -fsanitize=address,undefined -I "$here/include" -c "$here/src/sf_cio_lifecycle.c" -o "$tmp/cio.o"
${CXX:-c++} -std=c++14 -O1 -g -fsanitize=address,undefined -I "$here/include" "$here/host-test/cio.cpp" "$tmp/cio.o" -o "$tmp/cio"
"$tmp/cio"
${CC:-cc} -std=c11 -O1 -g -fsanitize=address,undefined -I "$here/include" -c "$here/src/sf_pciec_port.c" -o "$tmp/pciec.o"
${CXX:-c++} -std=c++14 -O1 -g -fsanitize=address,undefined -I "$here/include" "$here/host-test/pciec.cpp" "$tmp/pciec.o" -o "$tmp/pciec"
"$tmp/pciec"
${CC:-cc} -std=c11 -O1 -g -fsanitize=address,undefined -I "$here/include" -c "$here/src/sf_cio_tunables.c" -o "$tmp/tunables.o"
${CXX:-c++} -std=c++14 -O1 -g -fsanitize=address,undefined -I "$here/include" "$here/host-test/tunables.cpp" "$tmp/tunables.o" -o "$tmp/tunables"
"$tmp/tunables"
${CC:-cc} -std=c11 -O1 -g -fsanitize=address,undefined -I "$here/include" "$here/host-test/wire.c" -o "$tmp/wire"
"$tmp/wire"
${CC:-cc} -std=c11 -O1 -g -fsanitize=address,undefined -I "$here/include" -c "$here/src/sf_tb_registry.c" -o "$tmp/registry.o"
${CXX:-c++} -std=c++14 -O1 -g -pthread -fsanitize=address,undefined -I "$here/include" "$here/host-test/registry.cpp" "$tmp/registry.o" -o "$tmp/registry"
"$tmp/registry"
cp -R "$here/upstream/family" "$tmp/family"
python3 "$here/tools/prepare_cm.py" "$tmp/family"
python3 - "$tmp/family/usb4_port.c" "$tmp/usb4_port_add.inc" <<'PYEXTRACT'
import pathlib,sys
s=pathlib.Path(sys.argv[1]).read_text();a=s.index('struct usb4_port *usb4_port_device_add(');b=s.index('\n/**',a)
pathlib.Path(sys.argv[2]).write_text('/* GPL-2.0: actual staged Linux usb4_port.c add function. */\n'+s[a:b])
PYEXTRACT
${CC:-cc} -std=c11 -O1 -g -fsanitize=address,undefined \
  -I "$tmp" -I "$here/host-test/device-linux" -I "$here/native" -I "$here/nativeinc" -I "$here/pininc" -I "$here/include" \
  "$here/native/sf_tb_device.c" "$here/src/sf_tb_registry.c" "$here/host-test/device.c" -pthread -o "$tmp/device"
"$tmp/device"
"$tmp/device" fault
"$tmp/device" query
"$here/host-test/crypto.sh"
${CC:-cc} -std=gnu11 -O1 -g -pthread -fsanitize=address,undefined -I "$here/native" "$here/host-test/groups.c" -o "$tmp/groups"
"$tmp/groups"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -I "$here/include" -c "$here/src/sf_pciec_config.c" -o "$tmp/pciec-config.o"
${CXX:-c++} -std=c++14 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -I "$here/include" "$here/host-test/pciec-config.cpp" "$tmp/pciec-config.o" -o "$tmp/pciec-config"
"$tmp/pciec-config"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -I "$here/native" "$here/host-test/lane-observation.c" -o "$tmp/lane-observation"
"$tmp/lane-observation"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -I "$here/include" -c "$here/src/sf_pciec_tunables.c" -o "$tmp/pciec-tunables.o"
${CXX:-c++} -std=c++14 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -I "$here/include" "$here/host-test/pciec-tunables.cpp" "$tmp/pciec-tunables.o" -o "$tmp/pciec-tunables"
"$tmp/pciec-tunables"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -I "$here/native" "$here/host-test/dock-policy.c" -o "$tmp/dock-policy"
"$tmp/dock-policy"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -I "$here/kernel" "$here/host-test/pci-dma-retire.c" -o "$tmp/pci-dma-retire"
"$tmp/pci-dma-retire"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -I "$here/kernel" "$here/host-test/requesters.c" -o "$tmp/requesters"
"$tmp/requesters"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -I "$here/kernel" "$here/host-test/pciec-dart-block.c" -o "$tmp/pciec-dart-block"
"$tmp/pciec-dart-block"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -I "$here/kernel" "$here/host-test/pciec-dart-sid0.c" -o "$tmp/pciec-dart-sid0"
"$tmp/pciec-dart-sid0"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -I "$here/native" "$here/host-test/retrain-policy.c" -o "$tmp/retrain-policy"
"$tmp/retrain-policy"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -I "$here/native" "$here/host-test/ida-range.c" -o "$tmp/ida-range"
"$tmp/ida-range"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -I "$here/include" "$here/host-test/msi-setup.c" -o "$tmp/msi-setup"
"$tmp/msi-setup"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -I "$here/include" "$here/host-test/bar.c" -o "$tmp/bar"
"$tmp/bar"
