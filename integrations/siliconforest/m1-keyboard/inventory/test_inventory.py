#!/usr/bin/env python3
"""Host-only CLI checks for keyboard_inventory.c.

Compiles the inventory source with temporary fake PDIOKitLib and mach
headers. Fakes answer from environment variables in that test process.
No HID open, device node, or production edit.
"""
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

SRC = Path(__file__).resolve().parent / "keyboard_inventory.c"
CLASSES = ("IOHIDDevice", "IOHIDKeyboard", "AppleEmbeddedKeyboard",
           "PDSerialHID", "PDM1SPIHID", "IOGOPFramebuffer")
NODES = ("/dev/usb_hid_kbd", "/dev/usb_hid_mouse")

MACH_H = r"""
#ifndef INV_FAKE_MACH_H
#define INV_FAKE_MACH_H
typedef int kern_return_t;
typedef unsigned int mach_port_t;
#define KERN_SUCCESS 0
#define MACH_PORT_NULL ((mach_port_t)0)
mach_port_t mach_task_self(void);
kern_return_t mach_port_deallocate(mach_port_t task, mach_port_t name);
#endif
"""

PDIOKIT_H = r"""
#ifndef _PD_IOKITLIB_H
#define _PD_IOKITLIB_H
#include <mach/mach.h>
typedef mach_port_t io_object_t;
typedef io_object_t io_iterator_t;
typedef io_object_t io_registry_entry_t;
typedef char io_name_t[128];
#define IO_OBJECT_NULL ((io_object_t)0)
kern_return_t IOMasterPort(mach_port_t bootstrapPort, mach_port_t *masterPort);
char *IOServiceMatching(const char *className);
kern_return_t IOServiceGetMatchingServices(mach_port_t masterPort,
    char *matching, io_iterator_t *existing);
io_object_t IOIteratorNext(io_iterator_t iterator);
kern_return_t IORegistryEntryGetName(io_registry_entry_t entry, io_name_t name);
kern_return_t IOObjectGetClass(io_object_t object, io_name_t className);
kern_return_t IOObjectRelease(io_object_t object);
#endif
"""

FAKE_C = r"""
#include <PDIOKitLib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int pending;

static int code_of(const char *key)
{
    const char *value = getenv(key);
    if (!value || !value[0]) return 0;
    return atoi(value);
}

char *IOServiceMatching(const char *className)
{
    return (char *)className;
}

kern_return_t IOMasterPort(mach_port_t bootstrap, mach_port_t *master)
{
    (void)bootstrap;
    if (!master) return 4;
    *master = 4;
    return KERN_SUCCESS;
}

kern_return_t IOServiceGetMatchingServices(mach_port_t master, char *matching,
                                           io_iterator_t *existing)
{
    const char *fail_class = getenv("INV_MATCH_CLASS");
    (void)master;
    if (!existing) return 4;
    *existing = IO_OBJECT_NULL;
    pending = 0;
    if (fail_class && matching && strcmp(fail_class, matching) == 0)
        return code_of("INV_MATCH_CODE") ? code_of("INV_MATCH_CODE") : 5;
    if (matching && strcmp(matching, "PDM1SPIHID") == 0 && code_of("INV_NATIVE")) {
        *existing = 3;
        pending = 1;
    }
    return KERN_SUCCESS;
}

io_object_t IOIteratorNext(io_iterator_t iterator)
{
    (void)iterator;
    if (!pending) return IO_OBJECT_NULL;
    pending--;
    return 7;
}

static kern_return_t fill(io_name_t dst, const char *key, const char *text)
{
    int code = code_of(key);
    if (!dst) return 4;
    dst[0] = 0;
    if (code) return code;
    snprintf(dst, sizeof(io_name_t), "%s", text);
    return KERN_SUCCESS;
}

kern_return_t IORegistryEntryGetName(io_registry_entry_t entry, io_name_t name)
{
    (void)entry;
    return fill(name, "INV_NAME_CODE", "built-in");
}

kern_return_t IOObjectGetClass(io_object_t object, io_name_t className)
{
    (void)object;
    return fill(className, "INV_CLASS_CODE", "PDM1SPIHID");
}

kern_return_t IOObjectRelease(io_object_t object)
{
    (void)object;
    return KERN_SUCCESS;
}

mach_port_t mach_task_self(void) { return 1; }

kern_return_t mach_port_deallocate(mach_port_t task, mach_port_t name)
{
    (void)task;
    (void)name;
    return KERN_SUCCESS;
}
"""


class InventoryCLI(unittest.TestCase):
    tmp = None
    binary = None

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        root = Path(cls.tmp.name)
        include = root / "include"
        (include / "mach").mkdir(parents=True)
        (include / "PDIOKitLib.h").write_text(PDIOKIT_H)
        (include / "mach" / "mach.h").write_text(MACH_H)
        fake = root / "fake_iokit.c"
        fake.write_text(FAKE_C)
        cls.binary = root / "keyboard-inventory"
        built = subprocess.run(
            ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
             "-I", str(include), str(SRC), str(fake), "-o", str(cls.binary)],
            capture_output=True, text=True, check=False)
        if built.returncode != 0:
            raise AssertionError(built.stderr)

    @classmethod
    def tearDownClass(cls):
        if cls.tmp is not None:
            cls.tmp.cleanup()

    def run_cli(self, args, **env):
        clean = {"PATH": os.environ.get("PATH", "/usr/bin"), "LC_ALL": "C"}
        clean.update(env)
        return subprocess.run([str(self.binary), *args], capture_output=True,
                              text=True, env=clean, check=False)

    def observation(self, proc):
        self.assertEqual(proc.stderr, "")
        self.assertTrue(proc.stdout.endswith("\n"))
        self.assertEqual(proc.stdout.count("\n"), 1)
        data = json.loads(proc.stdout)
        self.assertEqual(data["status"], "GUEST_REGISTRY_OBSERVATION")
        self.assertIs(data["HID_open_performed"], False)
        self.assertIs(data["native_keyboard_working_claimed"], False)
        self.assertEqual([item["requested_class"] for item in data["classes"]],
                         list(CLASSES))
        self.assertEqual([item["path"] for item in data["nodes"]], list(NODES))
        for node in data["nodes"]:
            path = Path(node["path"])
            self.assertIs(node["exists"], path.exists())
            self.assertIs(node["character_device"], path.is_char_device())
        return data

    def native_count(self, data):
        for item in data["classes"]:
            if item["requested_class"] == "PDM1SPIHID":
                return item["count"], item
        self.fail("PDM1SPIHID missing")

    def test_default_null_iterator_exits_0(self):
        proc = self.run_cli([])
        data = self.observation(proc)
        self.assertEqual(proc.returncode, 0)
        self.assertIs(data["query_errors"], False)
        for item in data["classes"]:
            self.assertEqual(item["match_error"], 0)
            self.assertEqual(item["services"], [])
            self.assertEqual(item["count"], 0)

    def test_require_native_without_service_exits_77(self):
        proc = self.run_cli(["--require-native-keyboard"])
        data = self.observation(proc)
        self.assertEqual(proc.returncode, 77)
        self.assertIs(data["query_errors"], False)
        count, item = self.native_count(data)
        self.assertEqual(count, 0)
        self.assertEqual(item["services"], [])

    def test_native_class_present_exits_0(self):
        for args in ([], ["--require-native-keyboard"]):
            with self.subTest(args=args):
                proc = self.run_cli(args, INV_NATIVE="1")
                data = self.observation(proc)
                self.assertEqual(proc.returncode, 0)
                self.assertIs(data["query_errors"], False)
                count, item = self.native_count(data)
                self.assertEqual(count, 1)
                self.assertEqual(item["services"], [{
                    "name": "built-in", "class": "PDM1SPIHID",
                    "name_error": 0, "class_error": 0}])

    def test_match_name_and_class_errors_exit_1_even_if_native(self):
        cases = (
            {"INV_NATIVE": "1", "INV_MATCH_CLASS": "IOHIDDevice", "INV_MATCH_CODE": "5"},
            {"INV_NATIVE": "1", "INV_NAME_CODE": "5"},
            {"INV_NATIVE": "1", "INV_CLASS_CODE": "5"},
        )
        for toggles in cases:
            for args in ([], ["--require-native-keyboard"]):
                with self.subTest(args=args, toggles=sorted(toggles)):
                    proc = self.run_cli(args, **toggles)
                    data = self.observation(proc)
                    self.assertEqual(proc.returncode, 1)
                    self.assertIs(data["query_errors"], True)
                    count, _item = self.native_count(data)
                    if "INV_MATCH_CLASS" in toggles:
                        self.assertEqual(count, 1)
                        failed = data["classes"][0]
                        self.assertEqual(failed["requested_class"], "IOHIDDevice")
                        self.assertEqual(failed["match_error"], 5)
                        self.assertEqual(failed["services"], [])
                    else:
                        self.assertEqual(count, 1)
                        service = data["classes"][4]["services"][0]
                        if "INV_NAME_CODE" in toggles:
                            self.assertEqual(service["name_error"], 5)
                        if "INV_CLASS_CODE" in toggles:
                            self.assertEqual(service["class_error"], 5)

    def test_unknown_arguments_exit_2(self):
        for args in (["--help"], ["--require-native"],
                     ["--require-native-keyboard", "extra"]):
            with self.subTest(args=args):
                proc = self.run_cli(args, INV_NATIVE="1")
                self.assertEqual(proc.returncode, 2)
                self.assertEqual(proc.stdout, "")
                self.assertEqual(proc.stderr, "")


if __name__ == "__main__":
    unittest.main()
