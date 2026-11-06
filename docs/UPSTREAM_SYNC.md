# Upstream integration, 2026-11-06

Merged upstream main `c8aa460379` into personal main. This includes upstream
simulation changes for BFME2 and RotWK and presentation changes shared with
BFME1 compatibility mode. Edition-specific source citations remain in the
imported implementations.

The integration preserves fork Lua, controller/touch, wall/placement and shell
features, including conditional instance-buffer uploads. Overlapping ports use
upstream's position sampler, defensive tactics, structure/citadel/tree decisions,
Brutal AI arithmetic, placement feedback, shader sharing and draw culling. The
fork Army Breaker decision and its independent timer remain.

Older native AI snapshots require a new game: SkirmishAI now uses version 7 and
AIStatCollector version 4. Fork and upstream reused earlier version numbers for
different layouts, so the merged reader refuses them explicitly. TerrainTrees
keeps version 1 for an unused index, writes version 3 after a cluster is used,
and refuses ambiguous pre-merge version 2. These are OpenBFME native formats;
retail save formats are unchanged.

The fork's AI battle-statistics stop is now S-6200, avoiding the S-5902 crate
input stop that upstream also uses. Duplicate S-5901 documentation is consolidated.

Validation is recorded in the integration commit. No packaged app is replaced.

## Validation

- Release extension and native test executable build successfully.
- Mounted BFME2/RotWK AI, shader, banner and upgrade checks: 101 cases,
  51,389 assertions passed. Binary-oracle checks requiring RW_GAME_DAT were
  skipped; no pristine RotWK binary parity claim is made.
- Broader save/shroud/script run: 192 of 195 cases passed (83,508 of 83,513
  assertions). The initial failures were the Angmar campaign damage-composition pin,
  script-client audio counts, and the known ambient rank-store issue in script4.
  Applied the existing test-only rank-store isolation fix; the combined AIPOW
  and script4 rerun passed 28 cases and 698 assertions. Campaign/audio expectations
  remain unchanged rather than being re-recorded merely to make the fork green.
- Focused saves/shroud/command-button/BFME1-isolation run: 114 cases and
  50,328 assertions passed, with unavailable mounted BFME1 checks skipped.
- Headless controller-input and macOS-trackpad checks passed in normal mode.
- Controller gameplay type-selection also fails against the existing packaged
  pre-merge fb2df6a76d build. Tutorial construction fails on main; its pre-merge
  ObjectDoCommandButton.cpp still explicitly reports DOZER_CONSTRUCT as
  unported. The fixes in the separate tutorial contribution branch are not
  silently imported by this upstream sync.
- Python non-game checks: 12 passed. The inventory check still finds two
  existing unregistered launcher scripts, aotr_selection_test.gd and
  bfme2_selection_test.gd (neither was registered on pre-merge main).
- Commit gate and whitespace checks pass. BFME1 mounted data was not available
  in the test installation; BFME1 isolation uses synthetic data instead.

BFME2-only Godot startup was also checked and fails at Upgrade.ini's reference
to ANGMAR_TECH_BASIC_TRAINING_BUILDCOST, followed by unresolved upgrade names.
The existing pre-merge app also cannot finish BFME2-only startup, reporting
S-1321 for Living World's AddPlayer field; those are different failure reports.
Normal RotWK-on-BFME2 mounted startup passes the camera/input checks above.

The BFME2 native data/map checks passed 5 of 6 cases. The remaining assertion
expects 4,238 templates, while this fork loads 4,268; the 120-map object sweep
resolved 123,461 placed objects. The named S-1321 Living World gap is reported.
The 3,000-frame BFME2 skirmish was excluded from this focused check.
