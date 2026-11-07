# Outpost rebuild command

Editions: both BFME2 and RotWK. BFME1 retains its existing compatibility mode
(S-5017).

A destroyed outpost remains as a KeepObjectDie object with GettingBuiltBehavior.
Its START_SELF_REPAIR command was visible but had no HUD press case or simulation
message handler. Click selection also rejected dead objects before honoring
KeepSelectableWhenDead; that gate now respects the same flag as object picking.
The button now sends argument-free MSG_START_SELF_REPAIR, and
the logic uses the first owned selected object, checks rebuild eligibility and
price, and calls the existing non-forced startConstruction path. That path
charges once and rebuilds the existing object through its construction update.
Affordability and ownership are checked by the simulation even for stale input.

Sources: RotWK processCommandUI RW 0x940AF0, getCommandAvailability RW 0x942D83,
and dispatch RW 0x77C3CF..0x77C423 (subject to S-001). The matching BFME2 decomp
sources are ControlBarCommandProcessing.cpp case 0x33,
ControlBarRappellerCount.cpp case 0x33, and GameLogicDispatch.cpp case 1117.
The shared eligibility, cost and rebuild implementation remains
GettingBuiltBehavior (RW 0x856501 / 0x85657E / 0x8566DF); its existing limitations,
including S-653, still apply. Script-issued self repair through
Object::doCommandButton and neighborhood repair remain outside this change.

`test_build_core.cpp` covers HUD-to-message-to-simulation execution, one payment,
completion, insufficient funds and foreign ownership with synthetic data.
`test_factc_sweep.cpp` exercises the actual retail Outpost, rubble selection,
button press, payment and completion when the retail installs are mounted.

The optional GameData repair delay uses the native ten-second constructor
default when omitted (RW 0x642F8D; BFME2 GlobalData.cpp ctor RVA 0x23631C).
BFME1 GlobalDataConstructor.cpp also initializes this value to 0xa. Explicit
INI overrides, including zero, and later partial blocks retain their values.
The loader previously marked the omitted field unavailable, making the rebuild
eligibility check throw on retail Outpost data.
