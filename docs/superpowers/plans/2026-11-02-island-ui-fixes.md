# Island UI fixes

Requirements: native hover outline matching the module; panel anchored at
module center; retained text layout during full-panel transitions; 32 mirrored
bars below elapsed/remaining time and progress, attached to progress at the top.
Persist both Noctalia settings and the user's explicitly selected Codex `/`
writable root, preserving unrelated config and backups.

Evidence: installed Noctalia log repeatedly rejects `surface_container_high`;
manifest enables `open_near_click`; full panel reconstructs all lyric labels on
every custom animation frame; widget rebuilds its tree for position-only events.

- [x] Reproduce with widget, player, full-panel, frequency and config tests.
- [x] Delegate capsule geometry to native Noctalia, deduplicate widget paints.
- [x] Disable near-click placement and persist attached top-center settings.
- [x] Keep long text layout during native panel animation; coalesce player paints.
- [x] Compute 16 frequency bands and mirror into 32 downward-growing UI bars.
- [x] Add opt-in persistent Codex settings, backups and dry-run diff.
- [x] Build, full CTest, plugin lint and staged installation/config validation.
- [x] Independent review; report limits of live installation and UI checks.

Installed version 1.7.1 into the user's runtime with backups and successful
disable/enable/config-reload responses. Confirmed installed Lua and executable
bytes match the project/build; parsed both persistent configs to verify values.
After the permission configuration was applied, one full CTest invocation passed
all 11 checks, including Unix socket and D-Bus integration. The final repeated
invocation was restricted by the active tool sandbox: 9 passed and the two socket/
D-Bus checks failed with permission errors. Do not claim the final run was green.
A real Noctalia-validator regression caught
this installed version rejecting an empty capsule_border on an enabled plugin;
changed to a valid outline role with zero border width and verified the fix.
Independent review found no additional actionable regressions.
Runtime archives were also moved out of the scanned plugins directory into
plugin-backups, preserving all previous installer backups. A regression test
verifies future archives cannot create duplicate plugin registrations.
Separate live IPC/screenshot access from the tool environment remains limited;
visual placement and perceived animation smoothness need a desktop check.
