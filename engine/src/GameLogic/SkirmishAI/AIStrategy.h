// OpenBFME. GPL-3.0. User-requested strategy policy, not retail parity.
#pragma once
#include "GameLogic/ObjectTypes.h"
#include <array>
class GameLogic;
class Object;
class ThingTemplate;
struct AISkirmishPlayer;
namespace AIStrategy
{
enum Role { Swords, Pikes, Archers, Cavalry, Monsters, Flyers, Siege, Other, RoleCount };
struct Situation
{
 std::array<int, RoleCount> own{};
 std::array<int, RoleCount> enemy{};
 bool homeThreat = false;
};
// Uses KindOf roles, never faction or retail unit names.
Role role(GameLogic &logic, const ThingTemplate &unit);
bool visible(GameLogic &logic, const Object &enemy, int player);
Situation assess(GameLogic &logic, const AISkirmishPlayer &ai);
int productionNeed(Role candidate, const Situation &s);
// One local command pass per second. Reserved breach units keep their orders.
void update(GameLogic &logic, AISkirmishPlayer &ai);
// Native capture intent is already serialized by SpecialAbilityUpdate.
bool capturing(const Object &unit);
}
