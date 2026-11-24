#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Remove PCI endpoint matching from a copied native USB bundle's plist.

This changes only an explicitly selected build artifact. The Mach-O and the
SFAppleTypeC -> SFAppleUSB personality remain intact. No tunnel compatibility
or DMA admission is added. Use before including the bundle in a private PCI
enumeration collection; the ordinary USB driver build is unaffected.
"""
import hashlib
import json
import pathlib
import plistlib
import sys


def main():
    path = pathlib.Path(sys.argv[1])
    original = path.read_bytes()
    info = plistlib.loads(original)
    personalities = info.get("IOKitPersonalities")
    if not isinstance(personalities, dict):
        raise ValueError("missing personality dictionary")
    native = [name for name, value in personalities.items()
              if value.get("IOClass") == "SFAppleUSB"
              and value.get("IOProviderClass") == "SFAppleTypeC"]
    removed = [name for name, value in personalities.items()
               if value.get("IOProviderClass") == "IOPCIDevice"]
    if len(native) != 1 or not removed:
        raise ValueError("expected one native Type-C personality and PCI matches")
    result = dict(info)
    result["IOKitPersonalities"] = {
        name: value for name, value in personalities.items() if name not in removed
    }
    encoded = plistlib.dumps(result, fmt=plistlib.FMT_XML, sort_keys=False)
    check = plistlib.loads(encoded)
    if check["IOKitPersonalities"][native[0]] != personalities[native[0]]:
        raise ValueError("native personality changed")
    if any(value.get("IOProviderClass") == "IOPCIDevice"
           for value in check["IOKitPersonalities"].values()):
        raise ValueError("PCI endpoint match remains")
    temporary = path.with_name(path.name + ".enumeration-next")
    with temporary.open("xb") as stream:
        stream.write(encoded)
    temporary.replace(path)
    print(json.dumps({"removed_pci_personalities": removed,
                      "preserved_native_personality": native[0],
                      "original_sha256": hashlib.sha256(original).hexdigest(),
                      "result_sha256": hashlib.sha256(encoded).hexdigest()}))


if __name__ == "__main__":
    main()
