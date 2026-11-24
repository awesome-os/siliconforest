# SPDX-License-Identifier: MIT
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('coord_gate', Path(__file__).with_name('coord-gate.py'))
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)

class HoldTests(unittest.TestCase):
    def test_two_minute_install_notice_blocks_before_reboot_or_lock(self):
        notice='2026-11-16T08:06:26Z CODEX-TB 2-MIN INSTALL NOTICE Firefox47; no queued work once acquired\n'
        self.assertIsNotNone(gate.pending_hold(notice,max_seconds=170))
        self.assertIsNotNone(gate.pending_hold(notice+'T OTHER M1 back in SiliconForest\n'))
        self.assertIsNotNone(gate.pending_hold(notice+'T CODEX-TB quoted: M1 back in SiliconForest\n'))
        self.assertIsNotNone(gate.pending_hold(notice+'T CODEX-TB gui.lock released\n'))
        self.assertIsNone(gate.pending_hold(notice+'T CODEX-TB M1 back in SiliconForest (Firefox47)\n'))

    def test_two_minute_notice_never_clears_other_install_or_future_notice(self):
        notice='T CODEX-TB 2-MIN INSTALL NOTICE Firefox47\n'
        other='U TERM10 core.lock: GUARDED INSTALL now\n'
        recovered='V CODEX-TB M1 back in SiliconForest\n'
        self.assertIsNotNone(gate.pending_hold(notice+other+recovered))
        self.assertIsNotNone(gate.pending_hold(notice+recovered+notice))
        self.assertIsNone(gate.pending_hold('T CODEX quoted: 2-MIN INSTALL NOTICE Firefox47\n'))

    def test_iso_offer_uses_actual_earliest_calendar_date(self):
        from datetime import datetime,timezone
        notice='2026-11-16T23:58:00Z CODEX-TB NOTICE next Firefox install earliest2026-11-17T00:02:00Z. Otheragents mayuseM1normallybefore23:59; owner offer.\n'
        self.assertIsNone(gate.pending_hold(notice,now=datetime(2026,11,16,23,58,10,tzinfo=timezone.utc),max_seconds=20))
        # A past calendar date cannot turn into a new same-day permission.
        notice='2026-11-16T21:00:00Z CODEX-TB NOTICE next Firefox install earliest2026-11-15T23:00:00Z. Otheragents mayuseM1normallybefore21:05; owner offer.\n'
        self.assertIsNotNone(gate.pending_hold(notice,now=datetime(2026,11,16,21,1,tzinfo=timezone.utc),max_seconds=20))

    def test_iso_dated_kernel_notice_blocks_until_its_real_recovery(self):
        text='2026-11-16T03:26:50Z CODEX-TB NOTICE next Firefox kernel install earliest 2026-11-16T03:28:50Z: Firefox45 reserved\n'
        self.assertIsNotNone(gate.pending_hold(text))
        self.assertIsNotNone(gate.pending_hold(text+'T OTHER M1 back in SiliconForest\n'))
        self.assertIsNotNone(gate.pending_hold(text+'T CODEX-TB quoted: M1 back in SiliconForest\n'))
        boot='T CODEX-TB INSTALL START Firefox45; Correct fenced /bin/reboot requested\n'
        self.assertIsNone(gate.pending_hold(text+boot+'U CODEX-TB M1 back in SiliconForest\n'))

    def test_iso_date_does_not_implicitly_offer_or_expire_a_slot(self):
        from datetime import datetime,timezone
        text='2026-11-16T23:58:00Z CODEX-TB NOTICE next Firefox kernel install earliest 2026-11-17T00:02:00Z: reserved\n'
        self.assertIsNotNone(gate.pending_hold(text,now=datetime(2026,11,16,23,59,tzinfo=timezone.utc)))
        self.assertIsNotNone(gate.pending_hold(text,now=datetime(2026,11,17,0,5,tzinfo=timezone.utc)))

    def test_actual_fenced_kernel_install_reboot_has_its_own_epoch(self):
        text='T DESK8 gui.lock: install desk8 compositor relaunch\nU CODEX-TB NOTICE next Firefox kernel install earliest01:07:30Z\n'
        boot='INSTALL START Firefox42 capsulehash; Correct fenced /bin/reboot requested, stderr retained; no idle gate.\n'
        self.assertIsNotNone(gate.pending_hold(text+'V CODEX-TB M1 back in SiliconForest\n'))
        for prefix in ['waiting for ','quoted: ']:
            self.assertIsNotNone(gate.pending_hold(text+'V CODEX-TB '+prefix+boot+'W CODEX-TB M1 back in SiliconForest\n'))
        self.assertIsNotNone(gate.pending_hold(text+'V OTHER '+boot+'W CODEX-TB M1 back in SiliconForest\n'))
        self.assertIsNone(gate.pending_hold(text+'V CODEX-TB '+boot+'W CODEX-TB M1 back in SiliconForest\n'))
        # A hold that starts after shutdown is still independent.
        self.assertIsNotNone(gate.pending_hold(text+'V CODEX-TB '+boot+'W TERM9 core.lock: GUARDED INSTALL now\nX CODEX-TB M1 back in SiliconForest\n'))

    def test_multiword_kernel_notice_is_a_real_reservation(self):
        from datetime import datetime,timezone
        text=('2026-11-16T01:05:09Z CODEX-TB NOTICE next Firefox kernel install '
              'earliest01:07:30Z (2min+). Otheragents mayuseM1normally before01:07:30, then honorlocks; source continues.\n')
        self.assertIsNotNone(gate.pending_hold(text,now=datetime(2026,11,16,1,8,tzinfo=timezone.utc)))
        self.assertIsNone(gate.pending_hold(text,now=datetime(2026,11,16,1,6,tzinfo=timezone.utc),max_seconds=60))
        self.assertIsNotNone(gate.pending_hold(text,now=datetime(2026,11,16,1,6,tzinfo=timezone.utc),max_seconds=91))
        self.assertIsNotNone(gate.pending_hold(text+'U CODEX-TB M1 back in SiliconForest\n'))
        self.assertIsNone(gate.pending_hold(text+'U CODEX-TB clean reboot to Linux\nV CODEX-TB M1 back in SiliconForest\n'))

    def test_devfs_exhaustion_needs_the_exact_installed_fix_marker(self):
        text="T COORDINATOR: GPU device slots exhausted AGAIN; until 'M1 back in SiliconForest' is logged.\n"
        self.assertIsNotNone(gate.pending_hold(text))
        self.assertIsNotNone(gate.pending_hold(text+'U CODEX-TB M1 back in SiliconForest (old kernel)\n'))
        self.assertIsNotNone(gate.pending_hold(text+'U CODEX-TB waiting for M1 back in SiliconForest (devfs clone-slot fix installed)\n'))
        self.assertIsNotNone(gate.pending_hold(text+'U OTHER M1 back in SiliconForest (devfs clone-slot fix installed)\n'))
        self.assertIsNone(gate.pending_hold(text+'U CODEX-TB M1 back in SiliconForest (devfs clone-slot fix installed)\n'))
    def test_coordinator_freeze_requires_actual_recovery_owner_marker(self):
        text="T COORDINATOR: desktop FROZEN since ~23:20Z; no work until 'M1 back in SiliconForest' is logged.\n"
        self.assertIsNotNone(gate.pending_hold(text))
        self.assertIsNotNone(gate.pending_hold(text+'U CODEX-TB waiting for M1 back in SiliconForest\n'))
        self.assertIsNotNone(gate.pending_hold(text+'U OTHER M1 back in SiliconForest\n'))
        self.assertIsNotNone(gate.pending_hold(text+'U CODEX-TB gui.lock released\n'))
        self.assertIsNone(gate.pending_hold(text+'U CODEX-TB M1 back in SiliconForest; recovery verified\n'))
        self.assertIsNone(gate.pending_hold(text+'U COORDINATOR: M1 back in SiliconForest\n'))
        self.assertIsNone(gate.pending_hold(text+'U CODEX-TB cio29 recovered qualified29; M1 back in SiliconForest\n'))
    def test_frozen_marker_does_not_expire_or_override_other_holds(self):
        from datetime import datetime,timezone
        text='T COORDINATOR: desktop FROZEN since23:20\n'
        self.assertIsNotNone(gate.pending_hold(text,now=datetime(2030,1,1,tzinfo=timezone.utc)))
        self.assertIsNotNone(gate.pending_hold(text+'U TERM8 core.lock: GUARDED INSTALL now\nV CODEX-TB M1 back in SiliconForest\n'))
    def test_quoted_freeze_is_not_actual_coordinator_event(self):
        self.assertIsNone(gate.pending_hold('T CODEX quoted COORDINATOR: desktop FROZEN\n'))
    def test_explicit_preboot_offer_requires_runtime_to_fit(self):
        from datetime import datetime,timezone
        now=datetime(2026,11,15,21,30,tzinfo=timezone.utc)
        text='2026-11-15T21:20:57Z CODEX-TB NOTICE nextCIO40TX earliest21:34Z AFTER GUI release. Otheragents mayuseM1normallybefore21:34; no TBSSH whiletheyholdGUI.\n'
        self.assertIsNotNone(gate.pending_hold(text,now=now))
        self.assertIsNone(gate.pending_hold(text,now=now,max_seconds=240))
        self.assertIsNotNone(gate.pending_hold(text,now=now,max_seconds=241))
        self.assertIsNotNone(gate.pending_hold(text,now=now,max_seconds=0))
        self.assertIsNotNone(gate.pending_hold(text,now=now,max_seconds=-1))

    def test_preboot_offer_never_clears_other_holds(self):
        from datetime import datetime,timezone
        now=datetime(2026,11,15,21,30,tzinfo=timezone.utc)
        text='2026-11-15T21:20:57Z CODEX-TB NOTICE nextCIO40TX earliest21:34:00Z. Otheragents mayuseM1normallybefore21:34; idle required.\n'
        for added in ['U TERM8 core.lock: GUARDED INSTALL now\n','U CODEX-TB M1 REBOOT now\n','U GFX2 risky GPU test over SSH\n','U DESK7 gui.lock: install compositor now\n']:
            with self.subTest(added=added):
                self.assertIsNotNone(gate.pending_hold(text+added,now=now,max_seconds=120))

    def test_new_notice_revokes_previous_offer(self):
        from datetime import datetime,timezone
        now=datetime(2026,11,15,21,30,tzinfo=timezone.utc)
        text='2026-11-15T21:20:57Z CODEX-TB NOTICE nextCIO40TX earliest21:34:00Z. Otheragents mayuseM1normallybefore21:34; idle required.\n'
        text+='2026-11-15T21:25:57Z CODEX-TB NOTICE nextCIO41 earliest21:35:00Z reserved\n'
        self.assertIsNotNone(gate.pending_hold(text,now=now,max_seconds=120))

    def test_scheduling_correction_revokes_previous_offer(self):
        from datetime import datetime,timezone
        now=datetime(2026,11,15,21,30,tzinfo=timezone.utc)
        text='2026-11-15T21:20:57Z CODEX-TB NOTICE nextCIO40TX earliest21:34Z. Otheragents mayuseM1normallybefore21:34; idle required.\n'
        text+='2026-11-15T21:25:57Z CODEX-TB Scheduling correction: earliestCIO40TX21:31Z reserved\n'
        self.assertIsNotNone(gate.pending_hold(text,now=now,max_seconds=60))

    def test_expired_unoffered_or_quoted_window_stays_reserved(self):
        from datetime import datetime,timezone
        now=datetime(2026,11,15,21,30,tzinfo=timezone.utc)
        prefix='2026-11-15T21:20:57Z CODEX-TB NOTICE nextCIO40TX earliest21:34:00Z. '
        for suffix in ['No offered window.','Otheragents mayuseM1normallybefore21:29; idle required.', 'Otheragents mayuseM1normallybefore21:35; idle required.', 'Someone said "Otheragents mayuseM1normallybefore21:34;".']:
            with self.subTest(suffix=suffix):
                self.assertIsNotNone(gate.pending_hold(prefix+suffix,now=now,max_seconds=120))

    def test_quoted_marker_is_not_completion(self):
        text = "T VK M1 back in SiliconForest\nU VK Please stay off the M1 until 'VK kernel test done'.\n"
        self.assertIsNotNone(gate.pending_hold(text))
        self.assertIsNotNone(gate.pending_hold(text+"V VK Waiting for 'VK kernel test done'.\n"))
        self.assertIsNone(gate.pending_hold(text+"V VK kernel test done: returned to daily\n"))

    def test_second_hold_requires_new_completion(self):
        text = "T VK Please stay off the M1 until 'VK M1 back in SiliconForest'.\nU VK M1 back in SiliconForest\n"
        self.assertIsNone(gate.pending_hold(text))
        self.assertIsNotNone(gate.pending_hold(text+"V VK Stay off SSH until 'VK M1 back in SiliconForest'.\n"))

    def test_alternative_completions(self):
        text = "T VK Please stay off the M1 until 'VK teardown session done' or 'VK M1 back in SiliconForest'.\n"
        for marker in ['teardown session done', 'M1 back in SiliconForest']:
            self.assertIsNone(gate.pending_hold(text+'U VK '+marker+' (daily)\n'))

    def test_explicit_codex_hold(self):
        text = 'T CODEX received coordinator M1 SSH hold; do not connect\n'
        self.assertIsNotNone(gate.pending_hold(text))
        self.assertIsNone(gate.pending_hold(text+'U COCOA4 M1 back in SiliconForest\n'))

    def test_core_install_blocks_new_programs(self):
        text = "T COCOA4 core.lock: GUARDED INSTALL now; done when I log 'COCOA4 core install finished'\n"
        self.assertIsNotNone(gate.pending_hold(text))
        self.assertIsNotNone(gate.pending_hold(text+"U CODEX waiting for COCOA4 core install finished\n"))
        self.assertIsNone(gate.pending_hold(text+'U COCOA4 core install finished: verified\n'))

    def test_reboot_announcement_and_actual_return(self):
        text = 'T GFX2 in ~2 min: M1 REBOOT for kernel cycle\n'
        self.assertIsNotNone(gate.pending_hold(text))
        self.assertIsNone(gate.pending_hold(text+'U GFX2 M1 back in SiliconForest after kernel cycle\n'))

    def test_gpu_run_requires_its_owner_completion(self):
        text = 'T GFX2 risky GPU test over SSH: avd-run\n'
        self.assertIsNotNone(gate.pending_hold(text+'U VK risky GPU test avd-run finished: rc=0\n'))
        self.assertIsNone(gate.pending_hold(text+'U GFX2 risky GPU test avd-run finished: rc=0\n'))

    def test_overlapping_holds_are_independent(self):
        text = 'T PERF core.lock: GUARDED INSTALL now\nU GFX2 in ~2 min: M1 REBOOT\n'
        self.assertIsNotNone(gate.pending_hold(text+'V GFX2 M1 back in SiliconForest\n'))
        self.assertIsNone(gate.pending_hold(text+'V GFX2 M1 back in SiliconForest\nW PERF core install finished: checked\n'))

    def test_qemu_rehearsal_is_not_live_install(self):
        text = 'T PERF core.lock: QEMU rehearsal; the guarded install follows later and will be announced\n'
        self.assertIsNone(gate.pending_hold(text))

    def test_cancelled_reboot_and_historical_mentions(self):
        text = "T COCOA4 Stay off SSH until 'M1 back in SiliconForest'\nU COCOA4 CANCELLED the visit: not run, no reboot\n"
        self.assertIsNone(gate.pending_hold(text))
        self.assertIsNone(gate.pending_hold(text+'V TERM5 correction: I missed the guarded-install notice\n'))

    def test_future_guarded_install_announcement_blocks(self):
        text = 'T PERF in 2 minutes: guarded core install on the M1\n'
        self.assertIsNotNone(gate.pending_hold(text))
        self.assertIsNone(gate.pending_hold(text+'U PERF core install finished: 18 checks\n'))

    def test_gpu_session_announcement_survives_individual_run(self):
        text = 'T GFX2 in ~2 min: risky GPU tests with UART capture\n'
        text += 'U GFX2 risky GPU test over SSH: first\nV GFX2 risky GPU test first finished: rc=0\n'
        self.assertIsNotNone(gate.pending_hold(text))
        self.assertIsNone(gate.pending_hold(text+'W GFX2 gui.lock released: all tests done\n'))

    def test_owner_withdrawn_session_needs_new_announcement(self):
        text = 'T GFX2 in 2 min: risky GPU tests\nU GFX2 my announced GPU tests did not run; re-announced when they run\n'
        self.assertIsNone(gate.pending_hold(text))
        self.assertIsNotNone(gate.pending_hold(text+'V GFX2 in 2 min: risky GPU tests\n'))

    def test_recovered_boot_ends_interrupted_gpu_session(self):
        text = 'T GFX2 in 2 min: risky GPU tests\nU GFX2 risky GPU test over SSH: interrupted\n'
        self.assertIsNone(gate.pending_hold(text+'V GFX2 M1 back in SiliconForest (other agents may use SSH again)\n'))

    def test_new_kernel_owner_reboot_outlives_postponed_gpu(self):
        text = 'T GFX2 in 2 min: risky GPU tests\nU CODEX-TB tc01 clean reboot to Linux\nV GFX2 postponed: an install started\n'
        self.assertIsNotNone(gate.pending_hold(text))
        self.assertIsNone(gate.pending_hold(text+'W CODEX-TB M1 back in SiliconForest\n'))

    def test_serialized_core_start_closes_older_actual_only(self):
        text = 'T COCOA3 core.lock: guarded install of Foundation\nU PERF in 2 minutes: guarded core install\nV COCOA4 core.lock: GUARDED INSTALL now\nW COCOA4 core install finished: checked\n'
        self.assertIsNotNone(gate.pending_hold(text))
        self.assertIsNone(gate.pending_hold(text+'X PERF core install finished: checked\n'))

    def test_kernel_install_end_reports_actual_return(self):
        text = 'T CODEX-TB tc01 clean reboot to Linux\nU CODEX-TB INSTALL END tc01: M1 back in SiliconForest; evidence saved\n'
        self.assertIsNone(gate.pending_hold(text))

    def test_kernel_hang_reboot_warning_survives_cts_release(self):
        text = 'T GFX2 WARNING: a kernel client hung; I will REBOOT the M1 after CTS\n'
        text += 'U GFX2 risky GPU test third finished: timeout\nV GFX2 gui.lock released: CTS done\n'
        self.assertIsNotNone(gate.pending_hold(text))
        self.assertIsNone(gate.pending_hold(text+'W GFX2 M1 back in SiliconForest; recovered\n'))

    def test_explicit_future_install_offer_requires_locked_reservation(self):
        text = 'T GFX2 in ~2 min (gui.lock; skipped if an install starts): risky GPU test with UART\n'
        self.assertIsNotNone(gate.pending_hold(text))
        self.assertIsNone(gate.pending_hold(text, install_reservation=True))
        self.assertIsNotNone(gate.pending_hold(text+'U GFX2 risky GPU test over SSH: cube\n', install_reservation=True))

    def test_install_offer_never_overrides_core_or_reboot(self):
        text = 'T GFX2 in ~2 min (gui.lock; skipped if an install starts): risky GPU test\n'
        for hold in ['U PERF core.lock: GUARDED INSTALL now\n', 'U GFX2 M1 REBOOT now\n']:
            self.assertIsNotNone(gate.pending_hold(text+hold, install_reservation=True))

    def test_actual_libobjc_completion_clears_guarded_hold(self):
        text = "T PERF core.lock: GUARDED INSTALL now; done when I log 'PERF libobjc install finished'\n"
        self.assertIsNotNone(gate.pending_hold(text))
        self.assertIsNone(gate.pending_hold(text+'U PERF libobjc install finished: checks pass\n'))

    def test_ab_hold_survives_temporary_boot_and_ends_after_recovery(self):
        text = 'T CODEX-TB IDLE A/B START under gui.lock: USB04 -> k2 -> USB04\n'
        text += 'U CODEX-TB idle-k2 clean reboot to Linux\n'
        text += 'V CODEX-TB temporary k2 back in SiliconForest under held gui.lock\n'
        self.assertIsNotNone(gate.pending_hold(text))
        text += 'W CODEX-TB IDLE-K2 restored USB04; M1 back in SiliconForest\n'
        self.assertIsNotNone(gate.pending_hold(text))
        text += 'X CODEX-TB IDLE-K2 owned UART reader stopped before gui.lock release\n'
        self.assertIsNone(gate.pending_hold(text))

    def test_owner_released_completed_core_install(self):
        text = 'T TERM6 core.lock: GUARDED INSTALL now: Foundation\n'
        self.assertIsNotNone(gate.pending_hold(text+'U CODEX expect TERM6 core.lock released: Foundation INSTALLED\n'))
        self.assertIsNotNone(gate.pending_hold(text+'U TERM6 in 2min: core.lock released: Foundation INSTALLED\n'))
        self.assertIsNone(gate.pending_hold(text+'U TERM6 core.lock released: Foundation INSTALLED, new probe ok\n'))

    def test_exact_owner_core_lock_release_clears_only_its_hold(self):
        text = 'T TERM7 core.lock: GUARDED INSTALL Foundation now\n'
        self.assertIsNotNone(gate.pending_hold(text+'U CODEX core.lock released\n'))
        self.assertIsNotNone(gate.pending_hold(text+'U TERM7 expect core.lock released\n'))
        self.assertIsNone(gate.pending_hold(text+'U TERM7 core.lock released\n'))
        self.assertIsNotNone(gate.pending_hold(text+'U TERM7 core.lock released\nV GFX2 M1 REBOOT now\n'))

    def test_actual_owner_parenthesized_release_clears_core_hold(self):
        text = 'T TERM8 core.lock: GUARDED INSTALL libFoundation now\n'
        release = 'core.lock released (guarded install rc=0)\n'
        self.assertIsNone(gate.pending_hold(text+'U TERM8 '+release))
        self.assertIsNotNone(gate.pending_hold(text+'U CODEX '+release))
        self.assertIsNotNone(gate.pending_hold(text+'U TERM8 expect '+release))
        self.assertIsNotNone(gate.pending_hold(text+'U TERM8 in 2min: '+release))
        self.assertIsNotNone(gate.pending_hold(text+'U TERM8 '+release+'V GFX2 M1 REBOOT now\n'))

    def test_idle_measurement_survives_gui_release_with_time_margin(self):
        from datetime import datetime, timezone
        text = '2026-11-15T06:23:41Z TERM6 gui.lock: start my own Activity Monitor for an idle CPU/RSS measurement (no input), ~2 min\n'
        text += '2026-11-15T06:23:42Z TERM6 gui.lock released (measurement continues without input)\n'
        self.assertIsNotNone(gate.pending_hold(text, now=datetime(2026,11,15,6,25,50,tzinfo=timezone.utc)))
        self.assertIsNone(gate.pending_hold(text, now=datetime(2026,11,15,6,26,12,tzinfo=timezone.utc)))

    def test_recovered_kernel_owner_event_clears_only_its_reboot(self):
        text = 'T CODEX-TB cio02 clean reboot to Linux\n'
        self.assertIsNotNone(gate.pending_hold(text+'U CODEX expect CIO02 recovered USB04; M1 back in SiliconForest\n'))
        self.assertIsNotNone(gate.pending_hold(text+'U CODEX-TB waiting for CIO02 recovered USB04; M1 back in SiliconForest\n'))
        self.assertIsNone(gate.pending_hold(text+'U CODEX-TB CIO02 recovered USB04; M1 back in SiliconForest\n'))
        self.assertIsNotNone(gate.pending_hold(text+'U PERF core.lock: GUARDED INSTALL now\nV CODEX-TB CIO02 recovered USB04; M1 back in SiliconForest\n'))

    def test_desktop_install_requires_actual_same_owner_release(self):
        text = 'T DESK6 gui.lock: install desk6 + compositor relaunch (.next)\n'
        self.assertIsNotNone(gate.pending_hold(text))
        for marker in [
            'U TERM8 gui.lock released\n',
            'U DESK6 waiting for gui.lock released\n',
            'U DESK6 NOTICE gui.lock released will follow tests\n',
        ]:
            self.assertIsNotNone(gate.pending_hold(text+marker))
        self.assertIsNone(gate.pending_hold(text+'U DESK6 gui.lock released\n'))
        self.assertIsNone(gate.pending_hold(text+'U DESK6 done (runs/m1-coord/done-DESK6): M1 left on the installed daily compositor\n'))

    def test_desktop_install_explicit_correction_is_owner_scoped(self):
        text = 'T DESK6 gui.lock: install desk6 + compositor relaunch (.next)\n'
        correction = 'correction: gui.lock was released at the end of the install (TERM8 has it now)\n'
        self.assertIsNotNone(gate.pending_hold(text+'U CODEX '+correction))
        self.assertIsNone(gate.pending_hold(text+'U DESK6 '+correction))
        self.assertIsNotNone(gate.pending_hold(text+'U PERF core.lock: GUARDED INSTALL now\nV DESK6 '+correction))

    def test_verified_matching_reboot_ends_only_older_desktop_session(self):
        text = 'T DESK6 gui.lock: install desk6 + compositor relaunch\n'
        self.assertIsNotNone(gate.pending_hold(text+'U CODEX-TB M1 back in SiliconForest\n'))
        self.assertIsNotNone(gate.pending_hold(text+'U CODEX-TB will reboot the M1 in 2 minutes\nV CODEX-TB M1 back in SiliconForest\n'))
        text += 'U CODEX-TB cio35 clean reboot to Linux\n'
        self.assertIsNotNone(gate.pending_hold(text+'V CODEX M1 back in SiliconForest\n'))
        self.assertIsNotNone(gate.pending_hold(text+'V CODEX-TB waiting for M1 back in SiliconForest\n'))
        self.assertIsNone(gate.pending_hold(text+'V CODEX-TB M1 back in SiliconForest\n'))
        self.assertIsNotNone(gate.pending_hold(text+'V PERF core.lock: GUARDED INSTALL now\nW CODEX-TB M1 back in SiliconForest\n'))

    def test_recovery_preserves_newer_desktop_hold(self):
        text = 'T DESK6 gui.lock: install old + compositor relaunch\n'
        text += 'U CODEX-TB cio35 clean reboot to Linux\n'
        text += 'V DESK6 gui.lock: install new + compositor relaunch\n'
        # A repeated reboot notification must not extend the old snapshot.
        text += 'W CODEX-TB M1 reboot now\nX CODEX-TB M1 back in SiliconForest\n'
        self.assertIsNotNone(gate.pending_hold(text))
        self.assertIsNone(gate.pending_hold(text+'Y DESK6 gui.lock released\n'))

    def test_actual_compositor_relaunch_without_install_word(self):
        text = 'T DESK6 gui.lock: compositor relaunch (.next) to restore the standard state\n'
        self.assertIsNotNone(gate.pending_hold(text))
        self.assertIsNotNone(gate.pending_hold(text+'U TERM8 gui.lock released\n'))
        self.assertIsNone(gate.pending_hold(text+'U DESK6 gui.lock released\n'))

    def test_desktop_checked_release_is_actual_same_owner_event(self):
        text = 'T DESK6 gui.lock: install desk6-005 + compositor relaunch\n'
        end = 'checks done, gui.lock released; M1 standard: .next desk6-005\n'
        self.assertIsNotNone(gate.pending_hold(text+'U TERM8 '+end))
        self.assertIsNotNone(gate.pending_hold(text+'U DESK6 waiting for '+end))
        self.assertIsNone(gate.pending_hold(text+'U DESK6 '+end))
        self.assertIsNotNone(gate.pending_hold(text+'U PERF core.lock: GUARDED INSTALL now\nV DESK6 '+end))

    def test_kernel_earliest_notice_is_reservation_not_reboot_epoch(self):
        text = 'T DESK6 gui.lock: install desk6 + compositor relaunch\n'
        text += 'U CODEX-TB NOTICE CIO37NET earliest19:51:00Z AFTER current owner; coreSH->guiEX->bench\n'
        text += 'V CODEX-TB SCHEDULING CORRECTION: earliestCIO37NET19:52:30Z (supersedes19:51). No hardware action\n'
        self.assertIn('19:52:30Z', gate.pending_hold(text))
        # A release of a physical lock, another owner, or even an unrelated
        # return cannot consume this future reservation or the old desktop hold.
        self.assertIsNotNone(gate.pending_hold(text+'W CODEX-TB gui.lock released\n'))
        self.assertIsNotNone(gate.pending_hold(text+'W CODEX-TB M1 back in SiliconForest\n'))
        text += 'W CODEX-TB cio37 clean reboot to Linux\n'
        self.assertIsNotNone(gate.pending_hold(text+'X OTHER M1 back in SiliconForest\n'))
        self.assertIsNone(gate.pending_hold(text+'X CODEX-TB M1 back in SiliconForest\n'))

    def test_future_kernel_reservation_never_expires_or_quotes_complete(self):
        text = '2026-11-15T19:49:13Z CODEX-TB NOTICE CIO37NET earliest19:51:00Z AFTER other work\n'
        far_future = gate.datetime(2027, 1, 1, tzinfo=gate.timezone.utc)
        self.assertIsNotNone(gate.pending_hold(text, now=far_future))
        self.assertIsNotNone(gate.pending_hold(text+'U CODEX-TB waiting for M1 back in SiliconForest\n'))
        self.assertIsNotNone(gate.pending_hold(text+'U OTHER CANCELLED trial; no reboot\n'))
        self.assertIsNone(gate.pending_hold(text+'U CODEX-TB CANCELLED trial; no reboot\n'))

    def test_quoted_kernel_notice_is_not_a_reservation(self):
        self.assertIsNone(gate.pending_hold('T CODEX quoted: NOTICE CIO37NET earliest19:51:00Z\n'))

    def test_owner_explicitly_defers_reservation_and_offers_normal_desktop(self):
        text='T CODEX-TB NOTICE CIO38TRACE earliest20:10:00Z\n'
        event='ACK TERM8coreEX/QEMU30-60min: pending38 willWAITcoreSH BEFOREguiEX/bench; normaldesktopavailabletoothers\n'
        self.assertIsNotNone(gate.pending_hold(text+'U OTHER '+event))
        self.assertIsNotNone(gate.pending_hold(text+'U CODEX-TB '+event.replace('normaldesktopavailabletoothers','desktop still reserved')))
        self.assertIsNone(gate.pending_hold(text+'U CODEX-TB '+event))
        self.assertIsNotNone(gate.pending_hold(text+'U CODEX-TB cio38 clean reboot to Linux\nV CODEX-TB '+event))
        self.assertIsNotNone(gate.pending_hold(text+'U TERM8 core.lock: GUARDED INSTALL now\nV CODEX-TB '+event))
        self.assertIsNotNone(gate.pending_hold(text+'U CODEX-TB '+event+'V CODEX-TB NOTICE CIO38TRACE earliest21:10:00Z\n'))

if __name__ == '__main__':
    unittest.main()
