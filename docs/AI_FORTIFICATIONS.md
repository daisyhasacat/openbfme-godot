# AI fortification response

The native skirmish AI checks walls, gates, fortress roots and base defenses against the current wall-damage rules and ordinary army weapons. Enemy fortifications that ordinary units cannot damage trigger an enhanced siege response. This is an OpenBFME improvement, not a completed port of the retail siege tactics.

The response selects the nearest protected fortification to the AI's base, using object IDs to break ties. Up to two available wall-capable or siege units receive direct attack orders, provided their live weapons can affect the target. They are reserved from ordinary tactical squads so other orders cannot replace the breach orders. The AI re-evaluates ownership, life state, armor and damage rules every update, attacks the next protected fortification after a breach, and releases units when the target becomes friendly, disappears or no longer needs siege. Indestructible structures are excluded.

When fewer than two capable units exist or are already requested, production prioritizes suitable members of the faction's ArmyDefinition even if their normal composition share is zero. It uses the normal production requests, respecting funds, prerequisites, command points and busy factories. If needed, a suitable factory in the faction's existing base layout is brought forward with its planned site intact. The response does not spawn free units or invent production buildings absent from that layout.

Production estimates use a template's default weapon set, damage types, target armor, projectile warheads, damage fields and target filters. Actual deployed units are checked again using their live weapon sets, including secondary weapons and armor modifiers. Upgrade-dependent weapons are conservatively excluded from production estimates. Factions or mods must offer a capable unit and a legal production chain; this response does not invent missing technology or resources.

During peace time, production may prepare siege but the response issues no combat orders. The native AI's tactical/team disable settings continue to apply.

Synthetic regression scenarios in `engine/tests/test_ai_fortifications.cpp` cover damage immunity, projectile and secondary weapons, siege-only versus wall-capable rules, actual wall damage, breach retargeting, ownership changes, peace expiry, zero-composition production, factory scheduling and bounded reinforcement demand. Live faction matches still require the user's retail archives.
