# SPDX-License-Identifier: MIT
"""Host policy checks for safe installed-runtime launch plans, not native GUI proof."""
import importlib.util
import contextlib
import io
import sys
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('daily_launch', Path(__file__).with_name('daily-launch.py'))
daily = importlib.util.module_from_spec(spec)
spec.loader.exec_module(daily)


class LaunchPlanPolicy(unittest.TestCase):
    def test_image_cache_is_explicit_and_cannot_leak_from_a_shell(self):
        os.environ['SFFX_GPU_IMAGE_CACHE'] = '1'
        _, env = self.plan()
        self.assertNotIn('SFFX_GPU_IMAGE_CACHE', env)
        self.manifest.update(system_ui=True, system_gpu_providers=True, gpu_image_cache=True)
        command, env = self.plan()
        self.assertEqual(env['SFFX_GPU_IMAGE_CACHE'], '1')
        self.assertEqual(command[1], '/usr/libexec/iosurfaced')
        self.assertEqual(env['MOZ_CRASHREPORTER_DISABLE'], '1')
        self.manifest['gpu_image_cache'] = False
        self.assertNotIn('SFFX_GPU_IMAGE_CACHE', self.plan()[1])

    def test_image_cache_rejects_unqualified_cohorts_and_non_booleans(self):
        self.manifest['gpu_image_cache'] = True
        with self.assertRaises(ValueError):
            self.plan()
        self.manifest.update(system_ui=True, system_gpu_providers=True)
        for value in [0, 1, 'true', []]:
            self.manifest['gpu_image_cache'] = value
            with self.assertRaises(ValueError):
                self.plan()

    def test_versioned_audio_family_preserves_system_ui_and_selected_graphics(self):
        self.manifest.update(system_ui=True, graphics_override=str(self.root/'overrides/nv12-v1'),
                             audio_override=str(self.root/'overrides/audio-clock-v1/Frameworks'),
                             audio_output_clock=True)
        command, env = self.plan()
        self.assertEqual(env['DYLD_FRAMEWORK_PATH'].split(':')[0], self.manifest['audio_override'])
        self.assertIn('/System/Library/Frameworks', env['DYLD_FRAMEWORK_PATH'].split(':'))
        self.assertIn(str(self.root/'overrides/nv12-v1/gl/lib'), env['DYLD_LIBRARY_PATH'].split(':'))
        self.assertEqual(command[1], str(self.root/'overrides/nv12-v1/surface/bin/iosurfaced'))
        self.assertEqual(env['SF_AU_NORMALIZE_HAL_CLOCK'], '1')

    def test_audio_family_cannot_escape_or_use_an_unversioned_runtime_prefix(self):
        for value in ['/System/Library/Frameworks', str(self.root/'overrides/Frameworks'),
                      str(self.root/'overrides/audio/../Frameworks'), str(self.root/'overrides/audio')]:
            self.manifest['audio_override'] = value
            with self.assertRaises(ValueError):
                self.plan()

    def test_audio_clock_requires_a_family_and_false_removes_an_inherited_enable(self):
        for value in [True, 1, 'true']:
            self.manifest['audio_output_clock'] = value
            with self.assertRaises(ValueError):
                self.plan()
        self.manifest['audio_output_clock'] = False
        os.environ['SF_AU_NORMALIZE_HAL_CLOCK'] = '1'
        _, env = self.plan()
        self.assertNotIn('SF_AU_NORMALIZE_HAL_CLOCK', env)

    def test_versioned_graphics_cohort_keeps_storage_dispatch_and_broker_together(self):
        graphics = self.root / 'overrides/nv12-v1'
        self.manifest.update(system_ui=True, graphics_override=str(graphics),
                             surface_override=str(self.root / 'overrides/surface-old/Frameworks'))
        command, env = self.plan()
        self.assertEqual(command[1], str(graphics / 'surface/bin/iosurfaced'))
        self.assertEqual(env['DYLD_FRAMEWORK_PATH'].split(':')[0], str(graphics / 'surface/Frameworks'))
        self.assertIn(str(graphics / 'gl/Frameworks'), env['DYLD_FRAMEWORK_PATH'].split(':'))
        self.assertIn(str(graphics / 'gl/lib'), env['DYLD_LIBRARY_PATH'].split(':'))
        for path in [str(self.root / 'gl/lib'), str(self.root / 'gl/Frameworks'),
                     str(self.root / 'overrides/surface-old/Frameworks')]:
            self.assertNotIn(path, env['DYLD_FRAMEWORK_PATH'].split(':') + env['DYLD_LIBRARY_PATH'].split(':'))

    def test_graphics_cohort_refuses_mixed_system_or_foreign_prefix(self):
        self.manifest.update(system_ui=True)
        for value in ['/System/Library', str(self.root / 'overrides'),
                      str(self.root / 'overrides/nv12/../other')]:
            self.manifest['graphics_override'] = value
            with self.assertRaises(ValueError):
                self.plan()
        self.manifest.update(graphics_override=str(self.root / 'overrides/nv12-v1'), system_gpu_providers=True)
        with self.assertRaises(ValueError):
            self.plan()

    def test_each_observer_in_a_combined_diagnostic_is_validated(self):
        os.environ['SFFX_DIAGNOSTIC_OBSERVER'] = '/private/var/db/sffx/hal.dylib:/private/var/db/sffx/loss.dylib'
        _, env = self.plan(['--profile', '/private/var/db/sffx/control/profile'])
        self.assertTrue(env['DYLD_INSERT_LIBRARIES'].endswith(os.environ['SFFX_DIAGNOSTIC_OBSERVER']))
        for suffix in ['/tmp/foreign.dylib', '/private/var/db/sffx/../foreign.dylib', '']:
            os.environ['SFFX_DIAGNOSTIC_OBSERVER'] = '/private/var/db/sffx/hal.dylib:' + suffix
            with self.assertRaises(ValueError):
                self.plan(['--profile', '/private/var/db/sffx/control/profile'])

    def test_system_gpu_selection_removes_private_surface_and_gl_and_uses_system_broker(self):
        self.manifest.update(system_ui=True, system_gpu_providers=True,
                             surface_override=str(self.root / 'overrides/surface-old/Frameworks'))
        command, env = self.plan()
        self.assertEqual(command[1], '/usr/libexec/iosurfaced')
        self.assertIn(str(self.root / 'surface/bin/surface-session'), command)
        self.assertEqual(env['DYLD_LIBRARY_PATH'].split(':')[0],
                         '/usr/local/lib/siliconforest/opengl/v1/lib')
        for field in ['DYLD_FRAMEWORK_PATH', 'DYLD_LIBRARY_PATH']:
            for path in env[field].split(':'):
                self.assertFalse(any(part in Path(path).parts for part in ['surface', 'gl', 'overrides']))
        self.assertIn('/System/Library/Frameworks', env['DYLD_FRAMEWORK_PATH'].split(':'))

    def test_system_gpu_selection_requires_the_system_ui_cohort(self):
        self.manifest['system_gpu_providers'] = True
        with self.assertRaises(ValueError):
            self.plan()

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.manifest = dict(target=str(self.root), system_layout=True, private_fonts=True, gpu_presentation=True)
        self.environment = patch.dict(os.environ, {}, clear=True)
        self.environment.start()
        self.addCleanup(self.environment.stop)

    def plan(self, args=None):
        (self.root / 'release.json').write_text(json.dumps(self.manifest))
        return daily.plan(self.root, args or [])

    def test_dry_plan_exports_selected_audio_clock_and_reporter_containment_only(self):
        stream = io.StringIO()
        environment = {'SF_AU_NORMALIZE_HAL_CLOCK': '1', 'MOZ_CRASHREPORTER_DISABLE': '1',
                       'SF_OTHER_SECRET': 'hidden', 'UNRELATED_SECRET': 'hidden'}
        with patch.object(daily, 'plan', return_value=([], environment)), \
             patch.object(sys, 'argv', ['daily-launch.py', '--sf-plan']), \
             contextlib.redirect_stdout(stream):
            daily.main()
        exported = json.loads(stream.getvalue())['environment']
        self.assertEqual(exported, {'SF_AU_NORMALIZE_HAL_CLOCK': '1', 'MOZ_CRASHREPORTER_DISABLE': '1'})

    def test_damage_is_explicit_and_requires_canonical_providers(self):
        os.environ['SFFX_GPU_DAMAGE']='1'
        _,env=self.plan()
        self.assertNotIn('SFFX_GPU_DAMAGE',env)
        self.manifest.update(system_ui=True,system_gpu_providers=True,gpu_damage=True)
        _,env=self.plan()
        self.assertEqual(env['SFFX_GPU_DAMAGE'],'1')
        self.manifest['system_gpu_providers']=False
        with self.assertRaises(ValueError):self.plan()

    def test_damage_selector_rejects_truthy_non_booleans(self):
        self.manifest.update(system_ui=True,system_gpu_providers=True)
        for value in ['true',1,None]:
            self.manifest['gpu_damage']=value
            with self.assertRaises(ValueError):self.plan()

    def test_network_override_keeps_canonical_ui_and_precedes_system_frameworks(self):
        self.manifest.update(system_ui=True, system_gpu_providers=True,
                             network_override=str(self.root / 'overrides/network-v1/Frameworks'))
        _, env = self.plan()
        self.assertEqual(env['DYLD_FRAMEWORK_PATH'].split(':')[0], self.manifest['network_override'])
        self.assertIn('/System/Library/Frameworks', env['DYLD_FRAMEWORK_PATH'].split(':'))
        self.assertEqual(env['DYLD_LIBRARY_PATH'].split(':')[0], '/usr/local/lib/siliconforest/opengl/v1/lib')

    def test_network_override_cannot_escape_owned_runtime(self):
        for value in ['/System/Library/Frameworks', str(self.root / 'overrides/../Frameworks'),
                      str(self.root / 'overrides/Frameworks')]:
            self.manifest['network_override'] = value
            with self.assertRaises(ValueError):
                self.plan()

    def test_ssh_environment_cannot_select_an_absent_linux_display(self):
        os.environ.update(XDG_RUNTIME_DIR='/var/run/user/0', WAYLAND_DISPLAY='wayland-stale')
        _, env = self.plan()
        self.assertEqual(env['XDG_RUNTIME_DIR'], '/private/var/run/gpu-desktop')
        self.assertEqual(env['WAYLAND_DISPLAY'], 'wayland-0')
        self.assertEqual(os.environ['WAYLAND_DISPLAY'], 'wayland-stale')

    def test_hung_crash_reporter_is_disabled_for_owner_and_agent_launch_plans(self):
        os.environ['MOZ_CRASHREPORTER_DISABLE'] = '0'
        _, env = self.plan()
        self.assertEqual(env['MOZ_CRASHREPORTER_DISABLE'], '1')
        self.assertEqual(os.environ['MOZ_CRASHREPORTER_DISABLE'], '0')

    def test_coherent_ui_selection_and_system_coretext(self):
        ui = self.root / 'overrides/ui-c17-v1'
        self.manifest.update(ui_override=str(ui), coretext_source='system')
        _, env = self.plan()
        self.assertIn(str(ui / 'appkit/Frameworks'), env['DYLD_FRAMEWORK_PATH'].split(':'))
        self.assertEqual(env['DYLD_LIBRARY_PATH'].split(':')[0], str(ui / 'windowserver/usr/lib'))
        self.assertNotIn(str(self.root / 'coretext/Frameworks'), env['DYLD_FRAMEWORK_PATH'].split(':'))
        self.assertEqual(env['FONTCONFIG_FILE'], str(self.root / 'fonts/fonts.conf'))

    def test_ui_selection_cannot_escape_the_owned_runtime(self):
        for ui in ['/System/Library/Frameworks', str(self.root / 'overrides/../appkit'), str(self.root / 'overrides')]:
            self.manifest['ui_override'] = ui
            with self.assertRaises(ValueError):
                self.plan()

    def test_onyx_selection_replaces_only_the_private_glyph_provider(self):
        provider = self.root / 'overrides/glyph-generation-v1/Frameworks'
        self.manifest['onyx_override'] = str(provider)
        _, env = self.plan()
        frames = env['DYLD_FRAMEWORK_PATH'].split(':')
        self.assertIn(str(provider), frames)
        self.assertNotIn(str(self.root / 'onyx/Frameworks'), frames)
        self.assertIn(str(self.root / 'appkit/Frameworks'), frames)
        self.assertIn(str(self.root / 'coretext/Frameworks'), frames)

    def test_onyx_selection_cannot_escape_the_owned_runtime(self):
        for value in ['/System/Library/Frameworks', str(self.root / 'overrides/../onyx/Frameworks'),
                      str(self.root / 'overrides'), str(self.root / 'overrides/glyph-generation-v1')]:
            self.manifest['onyx_override'] = value
            with self.assertRaises(ValueError):
                self.plan()

    def test_observer_requires_an_explicit_private_profile(self):
        os.environ['SFFX_DIAGNOSTIC_OBSERVER'] = '/private/var/db/sffx/audio-test/observer.dylib'
        for args in [[], ['--profile', '/var/root/Library/Application Support/Firefox/Profiles/siliconforest']]:
            with self.assertRaises(ValueError):
                self.plan(args)
        _, env = self.plan(['--profile', '/private/var/db/sffx/audio-test/profile'])
        self.assertTrue(env['DYLD_INSERT_LIBRARIES'].endswith(':' + os.environ['SFFX_DIAGNOSTIC_OBSERVER']))

    def test_disabled_sandboxes_are_still_refused(self):
        os.environ['MOZ_DISABLE_GPU_SANDBOX'] = '1'
        with self.assertRaises(RuntimeError):
            self.plan()

    def test_system_ui_cannot_reselect_legacy_private_overrides(self):
        self.manifest.update(system_ui=True,
            ui_override=str(self.root / 'overrides/ui-c17-v1'),
            coretext_override=str(self.root / 'overrides/memory-font-v1/Frameworks'),
            onyx_override=str(self.root / 'overrides/glyph-generation-v1/Frameworks'))
        _, env = self.plan()
        paths = env['DYLD_FRAMEWORK_PATH'].split(':') + env['DYLD_LIBRARY_PATH'].split(':')
        for path in paths:
            self.assertFalse(any(part in Path(path).parts for part in
                ['appkit', 'quartz', 'onyx', 'coretext', 'windowserver', 'overrides']))
        self.assertIn('/System/Library/Frameworks', paths)
        self.assertIn(str(self.root / 'surface/Frameworks'), paths)
        self.assertIn(str(self.root / 'gl/Frameworks'), paths)
        self.assertEqual(env['FONTCONFIG_FILE'], str(self.root / 'fonts/fonts.conf'))
        self.assertIn(str(self.root / 'sandbox/libsffx-sandbox.dylib'), env['DYLD_INSERT_LIBRARIES'])

    def test_system_ui_requires_boolean_and_installed_layout(self):
        for value in ['system', 1, None]:
            self.manifest['system_ui'] = value
            with self.assertRaises(ValueError):
                self.plan()
        self.manifest.update(system_ui=True, system_layout=False)
        with self.assertRaises(ValueError):
            self.plan()

    def test_system_ui_keeps_the_qualified_iosurface_lifetime_provider(self):
        provider = self.root / 'overrides/surface-lookup-v1/Frameworks'
        self.manifest.update(system_ui=True,surface_override=str(provider))
        _, env = self.plan()
        self.assertEqual(env['DYLD_FRAMEWORK_PATH'].split(':')[0],str(provider))
        self.assertNotIn(str(self.root / 'appkit/Frameworks'),env['DYLD_FRAMEWORK_PATH'].split(':'))


class CompositorPreferenceSafety(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.settings = Path(self.temporary.name) / 'user.js'

    def test_codec_default_and_switch_back_preserve_explicit_choices(self):
        original = b'// existing profile\nuser_pref("browser.zoom.full",false);\n'
        self.settings.write_bytes(original)
        daily.update_codec_preference(self.settings, False)
        self.assertIn(b'user_pref("media.av1.enabled",false);', self.settings.read_bytes())
        daily.update_codec_preference(self.settings, True)
        self.assertIn(b'user_pref("media.av1.enabled",true);', self.settings.read_bytes())
        self.settings.write_bytes(self.settings.read_bytes() + b'user_pref("media.av1.enabled",true);\n')
        daily.update_codec_preference(self.settings, False)
        self.assertEqual(self.settings.read_bytes(), original + b'user_pref("media.av1.enabled",true);\n')

    def test_removing_codec_policy_removes_only_its_block(self):
        original = b'// profile\n'
        self.settings.write_bytes(original)
        daily.update_codec_preference(self.settings, False)
        daily.update_codec_preference(self.settings, None)
        self.assertEqual(self.settings.read_bytes(), original)

    def test_codec_policy_is_strict_and_explicit_override_is_reversible(self):
        self.assertIsNone(daily.av1_preference({}, {}))
        self.assertIs(daily.av1_preference({'av1_enabled': False}, {}), False)
        self.assertIs(daily.av1_preference({'av1_enabled': False}, {'SFFX_AV1': '1'}), True)
        for value in [0, 'false', [], {}]:
            with self.assertRaises(ValueError):
                daily.av1_preference({'av1_enabled': value}, {})
        with self.assertRaises(ValueError):
            daily.av1_preference({}, {'SFFX_AV1': 'false'})

    def test_existing_other_preferences_still_receive_safe_window_and_gl_defaults(self):
        original = b'// existing profile\nuser_pref("browser.zoom.full",false);\n'
        self.settings.write_bytes(original)
        self.settings.chmod(0o640)
        daily.update_platform_preferences(self.settings)
        result = self.settings.read_bytes()
        self.assertTrue(result.startswith(original))
        self.assertIn(b'user_pref("browser.tabs.inTitlebar",0);', result)
        self.assertIn(b'user_pref("webgl.disable-angle",true);', result)
        self.assertNotIn(daily.COMPOSITOR_KEY.encode(), result)
        self.assertEqual(self.settings.stat().st_mode & 0o777, 0o640)
        self.assertEqual(daily.update_platform_preferences(self.settings), 'unchanged')
        self.assertEqual(self.settings.read_bytes(), result)

    def test_explicit_platform_choices_win_and_later_choices_remove_managed_defaults(self):
        original = b'user_pref("browser.tabs.inTitlebar",1);\n'
        self.settings.write_bytes(original)
        daily.update_platform_preferences(self.settings)
        self.assertNotIn(b'user_pref("browser.tabs.inTitlebar",0);', self.settings.read_bytes())
        explicit = b'user_pref("webgl.disable-angle",false);\n'
        self.settings.write_bytes(self.settings.read_bytes()+explicit)
        daily.update_platform_preferences(self.settings)
        self.assertEqual(self.settings.read_bytes(), original+explicit)
        self.assertEqual(daily.update_platform_preferences(self.settings), 'explicit-user-preference')

    def test_unselected_policy_never_creates_or_modifies_preferences(self):
        daily.update_compositor_preference(self.settings, None)
        self.assertFalse(self.settings.exists())
        self.settings.write_bytes(b'// owner settings without a final newline')
        daily.update_compositor_preference(self.settings, None)
        self.assertEqual(self.settings.read_bytes(), b'// owner settings without a final newline')

    def test_switch_back_preserves_unrelated_bytes_and_permissions(self):
        original = b'// owner \xff\nuser_pref("browser.zoom.full",false);\n'
        self.settings.write_bytes(original)
        self.settings.chmod(0o640)
        daily.update_compositor_preference(self.settings, False)
        first = self.settings.read_bytes()
        self.assertTrue(first.startswith(original))
        self.assertIn(b'user_pref("gfx.webrender.compositor",false);', first)
        daily.update_compositor_preference(self.settings, True)
        self.assertEqual(self.settings.read_bytes(), first.replace(b'compositor",false', b'compositor",true'))
        self.assertEqual(self.settings.stat().st_mode & 0o777, 0o640)
        self.assertEqual(daily.update_compositor_preference(self.settings, True), 'unchanged')

    def test_explicit_owner_or_test_choice_wins(self):
        original = b"user_pref('gfx.webrender.compositor', true);\n// other choice\n"
        self.settings.write_bytes(original)
        daily.update_compositor_preference(self.settings, False)
        self.assertEqual(self.settings.read_bytes(), original)

    def test_owner_override_removes_our_old_assignment_without_touching_the_override(self):
        prefix = b'user_pref("browser.zoom.full",false);\n'
        self.settings.write_bytes(prefix)
        daily.update_compositor_preference(self.settings, False)
        suffix = b'user_pref("gfx.webrender.compositor", true);\n// owner addition\n'
        self.settings.write_bytes(self.settings.read_bytes() + suffix)
        daily.update_compositor_preference(self.settings, False)
        self.assertEqual(self.settings.read_bytes(), prefix + suffix)

    def test_malformed_or_duplicate_markers_fail_without_rewriting(self):
        for text in [daily.COMPOSITOR_BEGIN, daily.COMPOSITOR_END + daily.COMPOSITOR_BEGIN,
                     (daily.COMPOSITOR_BEGIN + daily.COMPOSITOR_END) * 2]:
            self.settings.write_text(text)
            with self.assertRaises(ValueError):
                daily.update_compositor_preference(self.settings, False)
            self.assertEqual(self.settings.read_text(), text)

    def test_symlink_target_is_not_replaced_or_modified(self):
        target = self.settings.with_name('owner.js')
        target.write_bytes(b'// owner')
        self.settings.symlink_to(target)
        with self.assertRaises(ValueError):
            daily.update_compositor_preference(self.settings, False)
        self.assertTrue(self.settings.is_symlink())
        self.assertEqual(target.read_bytes(), b'// owner')

    def test_failed_atomic_replace_keeps_original_and_removes_temporary_file(self):
        self.settings.write_bytes(b'// owner\n')
        with patch.object(daily.os, 'replace', side_effect=OSError('disk error')):
            with self.assertRaises(OSError):
                daily.update_compositor_preference(self.settings, False)
        self.assertEqual(self.settings.read_bytes(), b'// owner\n')
        self.assertEqual(sorted(p.name for p in self.settings.parent.iterdir()), ['user.js'])

    def test_policy_and_switch_are_strict_booleans(self):
        self.assertIsNone(daily.native_compositor_preference({}, {}))
        self.assertIs(daily.native_compositor_preference({'native_compositor': False}, {}), False)
        self.assertIs(daily.native_compositor_preference({'native_compositor': False},
                       {'SFFX_NATIVE_COMPOSITOR': '1'}), True)
        for value in [0, 1, 'false', []]:
            with self.assertRaises(ValueError):
                daily.native_compositor_preference({'native_compositor': value}, {})
        with self.assertRaises(ValueError):
            daily.native_compositor_preference({}, {'SFFX_NATIVE_COMPOSITOR': 'false'})


if __name__ == '__main__':
    unittest.main()
