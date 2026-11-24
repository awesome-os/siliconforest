#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Prepare copied kext metadata for PCI inventory with no endpoint drivers.

Preserve PCI bridges and the exact existing Wi-Fi match; remove every other
IOPCIDevice personality. All non-PCI personalities and code images remain
untouched. The kernel independently checks its real IOCatalogue before it
allows the experimental tunnel. This tool never edits an installed collection.
"""
import json
import pathlib
import plistlib
import sys


def allowed(value):
    return (value.get("IOClass") == "IOPCI2PCIBridge"
            and value.get("CFBundleIdentifier") == "com.apple.iokit.IOPCIFamily") or (
        value.get("IOClass") == "SFBwfm"
        and value.get("CFBundleIdentifier") == "org.siliconforest.driver.SFBwfm"
        and value.get("IOPCIPrimaryMatch") == "0x442514e4"
        and "IOPCIMatch" not in value and "IOPCIClassMatch" not in value)


def main():
    root = pathlib.Path(sys.argv[1])
    changes = []
    for path in sorted(root.rglob("Info.plist")):
        info = plistlib.loads(path.read_bytes())
        personalities = info.get("IOKitPersonalities")
        if personalities is None:
            continue
        if not isinstance(personalities, dict):
            raise ValueError("invalid personality dictionary")
        removed = [name for name, value in personalities.items()
                   if value.get("IOProviderClass") == "IOPCIDevice" and not allowed(value)]
        if not removed:
            continue
        result = dict(info)
        result["IOKitPersonalities"] = {name: value for name, value in personalities.items()
                                       if name not in removed}
        encoded = plistlib.dumps(result, fmt=plistlib.FMT_XML, sort_keys=False)
        check = plistlib.loads(encoded)
        for name, value in check["IOKitPersonalities"].items():
            if value != personalities[name]:
                raise ValueError("retained personality changed")
        before = dict(info)
        after = dict(check)
        before.pop("IOKitPersonalities")
        after.pop("IOKitPersonalities")
        if before != after:
            raise ValueError("top-level metadata changed")
        temporary = path.with_name(path.name + ".enumeration-next")
        with temporary.open("xb") as stream:
            stream.write(encoded)
        temporary.replace(path)
        changes.append({"plist": str(path.relative_to(root)), "removed": removed})
    if not changes:
        raise ValueError("expected endpoint personalities in the selected base")
    print(json.dumps({"changed": changes, "dma_admitted": False}))


if __name__ == "__main__":
    main()
