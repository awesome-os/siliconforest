"""Host-only source wiring/provenance checks; no transport or runtime claims."""
from pathlib import Path
import hashlib
import json
import tomllib
import unittest

HERE = Path(__file__).resolve().parent
ORIGINAL = HERE.parent / "client"
SOURCE = (HERE / "src/main.rs").read_text()
EVENTS = (HERE / "src/input_events.rs").read_text()


class SourceContract(unittest.TestCase):
    def test_original_sources_still_match(self):
        expected = {
            "Cargo.toml": "5832f67a27d929a8b164e785fc2087800431cd3c90b59eb44b2e27c14a8457b0",
            "Cargo.lock": "ea3f3b8a61afa422aa68ddee338e59c10b30ccf93a456c6ae2c32d743127d50a",
            "src/main.rs": "e6ea5ee3503dadd58a20b628f7f8d92672e689a45311f2b4e195cdeaf2f79b4a",
        }
        for name, digest in expected.items():
            with self.subTest(name=name):
                self.assertEqual(hashlib.sha256((ORIGINAL / name).read_bytes()).hexdigest(), digest)

    def test_dependencies_unchanged_except_feature_and_binary_name(self):
        before = tomllib.loads((ORIGINAL / "Cargo.lock").read_text())
        after = tomllib.loads((HERE / "Cargo.lock").read_text())
        for package in after["package"]:
            if package["name"] == "darwin-wayland-client-input":
                package["name"] = "darwin-wayland-client"
        self.assertEqual(after, before)
        manifest = tomllib.loads((HERE / "Cargo.toml").read_text())
        self.assertEqual(manifest["package"]["name"], "darwin-wayland-client-input")
        self.assertEqual(manifest["dependencies"]["wayland-protocols"]["features"], ["client", "unstable"])

    def test_request_precedes_initial_commit_and_attach_is_gated(self):
        create = SOURCE.index("manager.get_toplevel_decoration(")
        request = SOURCE.index("decoration.set_mode(")
        commit = SOURCE.index("surface.commit()")
        attach = SOURCE.index("surface.attach(")
        self.assertLess(create, request)
        self.assertLess(request, commit)
        self.assertLess(commit, attach)
        self.assertEqual(SOURCE.count("decoration.set_mode("), 1)
        self.assertEqual(SOURCE.count("surface.attach("), 1)
        loop = SOURCE[SOURCE.index("while start.elapsed()") : attach]
        self.assertIn("if s.negotiation.ready() && s.released", loop)
        self.assertIn("if let Some(error)=s.negotiation.error()", loop)

    def test_real_callbacks_supply_mode_and_ack_serial(self):
        self.assertRegex(SOURCE, r"p\.ack_configure\(serial\);\s*s\.negotiation\.surface_configured\(serial\);")
        self.assertIn("s.negotiation.decoration_configured(mode);", SOURCE)
        self.assertEqual(SOURCE.count(".surface_configured("), 1)
        self.assertEqual(SOURCE.count(".decoration_configured("), 1)
        self.assertIn("ok_or(\"SSD required: no zxdg_decoration_manager_v1\")?", SOURCE)
        self.assertIn('"wl_seat"', SOURCE)

    def test_frozen_ssd_project_unchanged(self):
        lab = HERE.parents[3]
        receipt = json.loads((lab / "reports/sprint-010/desktop-ssd-client-preparation.json").read_text())
        for name, digest in receipt["files_sha256"].items():
            with self.subTest(name=name):
                self.assertEqual(hashlib.sha256((HERE.parent / "client-ssd" / name).read_bytes()).hexdigest(), digest)

    def test_observer_is_wired_to_real_protocol_events(self):
        for event, method in [("Capabilities", "seat(exact)"), ("Keymap", "keymap()"),
                              ("Modifiers", "modifiers(mods_depressed,mods_latched,mods_locked,group)"),
                              ("Key", "key(key,down)"), ("Button", "button(button,down)"),
                              ("Frame", "pointer_frame()")]:
            self.assertIn("::" + event, EVENTS)
            self.assertIn("s.input." + method, EVENTS)
        self.assertIn("s.own_surface.as_ref()==Some(&surface)", EVENTS)
        self.assertIn("keys.is_empty()", EVENTS)
        self.assertIn("file.read_exact(&mut bytes)", EVENTS)
        self.assertIn("meta.is_file()", EVENTS)
        self.assertIn("s.input.tick(start.elapsed().as_millis() as u64)", SOURCE)
        self.assertIn("if s.input.complete() && !input_announced", SOURCE)
        self.assertIn("if !s.input.complete() { return Err(", SOURCE)
        self.assertEqual(SOURCE.count("DL10_INPUT_OBSERVED"), 1)
        self.assertNotIn("/dev/usb_hid", SOURCE + EVENTS)
        self.assertNotIn("sf_serial", SOURCE + EVENTS)

    def test_input_markers_are_host_origin_and_readiness_is_single_use(self):
        for text in (SOURCE, EVENTS):
            for line in text.splitlines():
                if 'println!("DL10_INPUT_' in line:
                    self.assertIn("origin=host_sfserialhid", line)
        self.assertIn("s.input.arm(start.elapsed().as_millis() as u64)", SOURCE)

    def test_original_pixels_and_release_discipline_unchanged(self):
        original = (ORIGINAL / "src/main.rs").read_text()
        for start, end in [("fn glyph(", "fn run("), ("let mut pixels=", "last=Instant::now();")]:
            self.assertEqual(SOURCE[SOURCE.index(start):SOURCE.index(end)],
                             original[original.index(start):original.index(end)])
        self.assertIn("!(1..=120).contains(&seconds)", SOURCE)
        self.assertIn("while start.elapsed()<Duration::from_secs(seconds)", SOURCE)
        self.assertIn("Duration::from_millis(500)", SOURCE)
        self.assertIn("if let wl_buffer::Event::Release = e { s.released = true; }", SOURCE)

    def test_decoration_destroy_precedes_toplevel(self):
        self.assertLess(SOURCE.index("decoration.destroy()"), SOURCE.index("top.destroy()"))
        self.assertIn("if !ssd_announced { return Err(", SOURCE)
        self.assertIn("if s.frames==0 { return Err(", SOURCE)


if __name__ == "__main__":
    unittest.main(verbosity=2)
