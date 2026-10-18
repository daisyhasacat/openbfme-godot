# Multi-selection formation dispatch

Scope: selection → command button → command stream → existing formation swap.
No PORCUPINE pose, formation march, geometry, movement or Xfer implementation is
included. Base: main `645d389` (archive `b5c2095d02`).

## Recovered contract

Target facts from the supplied RotWK image (the provenance caveat in PLAN rule
9 / S-001 still applies), checked using local read-only Ghidra:

- `ControlBar::processCommandUI` (`0x940435`, command case `0x24`) distinguishes
  multi-selection context 7 from the single-source context.
- The group path counts each selected horde's **next** formation template ID,
  not its current formation and not a boolean inversion of the first horde.
  The getter is HordeContainInterface slot `0x58`, `0x86C7DE`.
- The most frequent ID wins. The ordered ID map and strict-greater comparison
  break ties in favor of the lowest template ID, independently of selection
  order. A horde with no alternate template does not vote.
- In selection order, the client emits `MSG_HORDE_SET_FORMATION` (`0x460` / 1120)
  for **each selected horde**, including ones which did not vote. Arguments:
  object ID, integer template ID. It emits nothing for single soldiers.
- The dispatcher resolves the template by its 16-bit ID and calls
  `canSetFormation` (`0x86FFD2`). Only the horde's own next template is accepted,
  with the existing nonempty-member / MinimumHordeSize checks. An already
  matching horde, incompatible formation family, or horde without an alternate
  is left alone. This is a global template vote, not one vote per unit type.
- Single selection retains `MSG_HORDE_TOGGLE_FORMATION` (`0x453` / 1107) with one
  object ID, using the existing swap worker.

The BFME2 donor corroborates message 1120's argument order and predicate in
`Code/GameEngine/Source/GameLogic/System/GameLogicDispatch.cpp`.

## Other command paths and limits

Stances already emit one `MSG_CHANGE_STANCE`; the simulation iterates the
issuing player's selection. Weapon and hold-fire toggles also already iterate
that selection: their source argument selects the desired state, not the only
recipient. Weapon toggles retain their equivalent-template / HOBBIT filter.
These paths are regression controls, not new formation implementations.

The control bar's common-command population, unsupported special-power toggles,
and the remaining formation-worker stops are not closed by this dispatch fix.
Network tests use OpenBFME's encoded lockstep transport, not retail cross-play.
Retail-data regressions are not a clean-retail executable oracle.

## Reproduction

`group command retail:*` in `test_hud_group_commands.cpp` uses actual mouse and
Shift-click selection, `ControlBar::pressButton`, and `HudInput`'s command stream.
Before the fix the formation case failed 14 assertions across three subcases:
only the source toggle was emitted, no group set messages existed, and the
other applicable hordes stayed unchanged. The stance/weapon control passed.

Additional coverage exercises shield wall, wedge, ties, standalone siege
hold-fire, and two network peers. The sending game uses `LockstepDriver`; the
receiving peer's decoded batches run in a fresh world with no local selection
or button press. Formation effects occur at the run-ahead frame and state
hashes are compared after every batch. No client-side simulation mutation or
new RNG draw is introduced by dispatch. Executing the newly delivered swaps
can change simulation state and hashes; pinned vectors remain unchanged.
