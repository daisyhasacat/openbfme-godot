// OpenBFME. GPL-3.0.
// See GameLogic/Module/UnportedModule.h.

#include "GameLogic/Module/UnportedModule.h"
#include "GameLogic/Module/SpawnUnitBehavior.h"
#include "Common/Thing/RawModuleData.h"

#include <stdexcept>
#include "GameLogic/SnapshotXfer.h"

UnportedUpdateModule::UnportedUpdateModule(Thing *thing, const ModuleData *data, int interfaceMask)
	: UpdateModule(thing, data)
	, m_mask(interfaceMask)
{
	// the module is not ported: it has nothing to run, so it starts asleep (the way ObjectHelper's constructor starts: ZH ObjectHelper.h)
	friend_setNextCallFrame((UnsignedInt)UPDATE_SLEEP_FOREVER);
}

UpdateSleepTime UnportedUpdateModule::update()
{
	++m_calls;
	return UPDATE_SLEEP_FOREVER;
}

std::unique_ptr<Module> makeUnportedModule(Thing *thing, const ModuleData *data, const std::string &className, ModuleType type, int interfaceMask)
{
	if (type == MODULETYPE_BEHAVIOR)
	{
		// A raw-data-only factory still reports an entirely unported module.
		if (className == "SpawnUnitBehavior" && !dynamic_cast<const RawModuleData *>(data))
		{
			const auto *typed = dynamic_cast<const SpawnUnitBehaviorModuleData *>(data);
			if (!typed)
			{
				throw std::logic_error("SpawnUnitBehavior: missing typed module data");
			}
			return std::make_unique<SpawnUnitBehavior>(thing, typed, interfaceMask);
		}
		if (interfaceMask & MODULEINTERFACE_UPDATE)
		{
			return std::make_unique<UnportedUpdateModule>(thing, data, interfaceMask);
		}
		return std::make_unique<UnportedBehaviorModule>(thing, data, interfaceMask);
	}
	return std::make_unique<UnportedDrawableModule>(thing, data, type, interfaceMask);
}

// ---- lane SAVE-4b: the snapshots of the unported classes' shells (S-140: the class's state is not ported; the shell's interface mask is its data's, its
// call count the report's) ------------------------------------------------------------------------------------------------------------------------------
void UnportedBehaviorModule::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferBehaviorModule(x);
}

void UnportedUpdateModule::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	x.xferUnsignedInt(&m_calls);
}
