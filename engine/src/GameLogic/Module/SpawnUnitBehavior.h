// OpenBFME. GPL-3.0.
// S-207: SpawnUnitBehavior's template offer, recovered from the supplied
// modified RotWK image (S-001), not independently verified clean-retail parity.
// RW 0x6506C0 -> 0x8BC924 installs behavior vtable 0xC6FDE8;
// slot 0x2C returns the +0x20 interface, whose getter RW 0x8BC992
// copies ModuleData +0x08 (UnitName). The query has no runtime-state gates.
// The separate automatic production update RW 0x8BC9B0 is NOT ported:
// retain the sleeping UnportedUpdateModule and its S-140 reporting.

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/UnportedModule.h"

class SpawnUnitBehaviorModuleData : public ModuleData
{
public:
	std::string m_unitName;    ///< RW data +0x08, default empty
	std::string m_unitCommand; ///< RW data +0x0C, default empty
	bool m_spawnOnce = false; ///< RW data +0x1C
	static void buildFieldParse(MultiIniFieldParse &p);
};

class SpawnUnitBehavior : public UnportedUpdateModule, public SpawnUnitBehaviorInterface
{
public:
	SpawnUnitBehavior(Thing *thing, const SpawnUnitBehaviorModuleData *data, int interfaceMask)
		: UnportedUpdateModule(thing, data, interfaceMask)
	{
	}
	OBFME_MODULE_XFER(SpawnUnitBehavior) // lane SAVE-4e (merge q20): the class keeps no state of its own; the shell's snapshot is UnportedUpdateModule's
	SpawnUnitBehaviorInterface *getSpawnUnitBehaviorInterface() override { return this; }
	const std::string &getUnitName() const override
	{
		return static_cast<const SpawnUnitBehaviorModuleData *>(getModuleData())->m_unitName;
	}
};
