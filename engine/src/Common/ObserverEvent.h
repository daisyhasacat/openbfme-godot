// OpenBFME. GPL-3.0.
#pragma once

#include "Common/GameCommon.h"
#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectTypes.h"
#include <string>

struct ObserverEvent
{
	enum Kind { Damage, Death, Power, Capture };
	Kind kind = Damage;
	UnsignedInt frame = 0;
	ObjectID object = INVALID_ID;
	int player = -1;
	Coord3D position{};
	std::string name;
	bool hero = false, structure = false, base = false;
	float weight = 0.0f;
};
