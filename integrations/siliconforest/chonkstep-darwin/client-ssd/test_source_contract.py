"""Host-only source wiring/provenance checks; no transport or runtime claims."""
from pathlib import Path
import hashlib
import tomllib
import unittest

HERE = Path(__file__).resolve().parent
ORIGINAL = HERE.parent / "client"
SOURCE = (HERE / "src/main.rs").read_text()


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
            if package["name"] == "darwin-wayland-client-ssd":
                package["name"] = "darwin-wayland-client"
        self.assertEqual(after, before)
        manifest = tomllib.loads((HERE / "Cargo.toml").read_text())
        self.assertEqual(manifest["package"]["name"], "darwin-wayland-client-ssd")
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
        self.assertNotIn('"wl_seat"', SOURCE)

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
