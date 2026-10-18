// OpenBFME. GPL-3.0. Client-only wall expansion spacing preview.
#pragma once
#include "GameLogic/ObjectTypes.h"
#include <vector>
class GameLogic;
class Object;
class CommandButton;
namespace WallUpgradePreview {
// false for commands that do not replace the selected wall; gate is a regular three-tile gate.
bool expansion(GameLogic &logic, const Object &source, const CommandButton &button, bool &gate);
// Recorded span slots retain parity across gaps and existing expansions; never follows a hub into another span.
std::vector<ObjectID> objects(GameLogic &logic, const Object &source, bool gate);
}
