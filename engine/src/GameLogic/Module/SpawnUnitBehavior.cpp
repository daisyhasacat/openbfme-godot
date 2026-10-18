// OpenBFME. GPL-3.0. See SpawnUnitBehavior.h.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/SpawnUnitBehavior.h"

#include "GameLogic/SnapshotXfer.h"

#include <cstddef>

void SpawnUnitBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	// RW 0x8BC981 -> 0xC6FEF0; defaults RW 0x65573A.
	static const FieldParse fields[] = {
		{ "UnitName", INI::parseAsciiString, nullptr, (int)offsetof(SpawnUnitBehaviorModuleData, m_unitName) },
		{ "UnitCommand", INI::parseAsciiString, nullptr, (int)offsetof(SpawnUnitBehaviorModuleData, m_unitCommand) },
		{ "SpawnOnce", INI::parseBool, nullptr, (int)offsetof(SpawnUnitBehaviorModuleData, m_spawnOnce) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	p.add(fields);
}

// ---- lane SAVE-4e (merge q20): the snapshot (the class keeps no state of its own: the sleeping UnportedUpdateModule shell, S-140) ----

void SpawnUnitBehavior::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	UnportedUpdateModule::xfer(x);
}
