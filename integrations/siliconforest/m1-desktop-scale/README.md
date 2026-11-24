# Desktop scaling and the settings menu (issue #38)

Lab-side files for the desktop usability work: a guest script and a boot wrapper for the
desktop with display scaling, themes and the Wi-Fi and Sound rows of the desktop menu.
Status, proofs and what is left: `docs/roadmap/issues/SF-O-desktop.md`.

| File | What |
|---|---|
| `tools/ds_desktop_boot.sh <name> [limit]` | One boot (names `ds01`, `ds02`, ...). Unattended by default: the compositor drives itself, including its `scale` and `settings` phases, then a second short session starts from a persisted scale and theme. `DS_AUTOPILOT=0 DS_SECONDS=<n>` gives the same desktop to a person. Queues for the bench itself. |
| `tools/ds_summary.sh <name>` | The lines of that boot's console that say what happened. |
| `guest/darwin-desktop-scale.in` | The guest side: Wi-Fi joined and kept joined in the background, coreaudiod, the desktop. |

The code is elsewhere:

- Compositor and shell: Chonkstep, `sources/chonkstep-darwin`, local branch `wm-darwin-scale` (worktree
  `runs/m1-desktop-010/wm-darwin-scale-worktree`; never pushed). Crates `wm-darwin` (`src/scale.rs`,
  `src/settings.rs`, the `scale` and `settings` autopilot phases), `chonk-shell`, `wm-theme`.
  Cross builds: `runs/m1-desktop-010/wm-darwin-scale-desktop-build-NNN` (`build.sh` there is the recipe).
- Terminal: `integrations/siliconforest/chonkstep-darwin/client-terminal-application` (`src/scale.rs`);
  cross build `runs/m1-desktop-010/terminal-application-build-006`.

Environment of the desktop session (set by the guest script from the wrapper's `DS_*`):

- `CHONKSTEP_SCALE`: `1`, `1.25`, `1.5`, `1.75`, `2`, `2.5`, `3` (or `150%`). Unset: what the menu last
  persisted, else by the panel (2 on the J293).
- `CHONKSTEP_THEME`: a built-in theme id (`nextstep-classic`, `graphite`, ...). Unset: what the menu
  last persisted, else the classic one.
- `XDG_STATE_HOME`: where the menu's choices are kept (`chonkstep/scale`, `chonkstep/theme`). The RAM
  root forgets it at power-off; nothing is written to the M1's internal storage.
- `CHONK_TERMINAL_FONT_PX`: the terminal's font in logical pixels (default 18).
- `CHONKSTEP_DARWIN_AUTOPILOT=1`, `CHONKSTEP_DARWIN_AUTOPILOT_ONLY=<phases>`,
  `CHONKSTEP_DARWIN_AUTOPILOT_WIFI_TOGGLE=0`: the unattended script, a selection of its phases, and
  leaving the radio alone.

Host tests write nothing outside their own directories when run with `XDG_STATE_HOME` pointing at a
scratch directory; run them that way (`host-tests.log` in the build directories was).
