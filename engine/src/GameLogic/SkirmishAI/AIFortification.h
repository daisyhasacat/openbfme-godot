// OpenBFME. GPL-3.0. Enhanced response to fortifications immune to the ordinary army.
#pragma once
class GameLogic;
class Object;
class ThingTemplate;
class Player;
struct AISkirmishPlayer;
namespace AIFortification
{
// These queries read current ownership, armor and wall policy; no remembered immunity can become stale.
Object *findTarget(GameLogic &logic, const AISkirmishPlayer &ai);
bool canBreach(GameLogic &logic, const ThingTemplate &unit, Object &target, const Player *owner = nullptr);
bool canBreach(const Object &unit, Object &target);
bool factoryCanBreach(GameLogic &logic, const ThingTemplate &factory, Object &target, const Player *owner = nullptr);
void update(GameLogic &logic, AISkirmishPlayer &ai);
}
