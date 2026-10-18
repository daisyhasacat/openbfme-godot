// OpenBFME. GPL-3.0.
// Enhanced policy derived from the BFME2 strategy playbook: answer composition,
// preserve valuable units, protect firing time, and retain siege control.
// Thresholds are OpenBFME tuning decisions, not inferred retail constants.
#include "GameLogic/SkirmishAI/AIStrategy.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "GameLogic/SkirmishAI/AIFortification.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Thing/ThingFactory.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponNugget.h"
#include <algorithm>

namespace
{
bool kind(GameLogic &logic, const ThingTemplate &tt, const char *name)
{
 const int bit = ObjectTemplateInfoBuilder::kindOfIndex(name);
 return bit >= 0 && MaskTest(logic.templateInfo(tt.getFinalOverride()).kindOf, (unsigned)bit);
}
bool army(const Object &o)
{
 return o.isInWorld() && !o.getContainedBy() && !o.isEffectivelyDead() && !o.isUnderConstruction() &&
  !o.isKindOfName("STRUCTURE") && !o.isKindOfName("DOZER") && !o.isKindOfName("IGNORES_SELECT_ALL") &&
  (o.isKindOfName("CAN_ATTACK") || o.isKindOfName("HERO") || o.isKindOfName("SUPPORT"));
}
float distance(const Object &a, const Object &b)
{
 return CombatQueries::centerDistanceSquared2D(*a.getPosition(), *b.getPosition());
}
float range(const Object &unit)
{
 float result = 0;
 if (auto *weapons = unit.getWeapons())
  for (int i = 0; i < WEAPONSLOT_COUNT; ++i)
   if (auto *w = weapons->weaponInSlot(i))
    if (w->getTemplate() && !w->getTemplate()->m_meleeWeapon)
     result = std::max(result, w->getTemplate()->m_attackRange);
 return result;
}
void releaseFromTactics(AISkirmishPlayer &ai, ObjectID id)
{
 for (auto &tactic : ai.brain.tactics)
  for (auto &team : tactic.teams)
   team.members.erase(std::remove(team.members.begin(), team.members.end(), id), team.members.end());
 ai.brain.dispatched.erase(id);
}
}

AIStrategy::Role AIStrategy::role(GameLogic &logic, const ThingTemplate &tt)
{
 if (kind(logic, tt, "SIEGEENGINE")) return Siege;
 if (kind(logic, tt, "AIRCRAFT")) return Flyers;
 if (kind(logic, tt, "PIKE")) return Pikes;
 if (kind(logic, tt, "ARCHER")) return Archers;
 if (kind(logic, tt, "CAVALRY")) return Cavalry;
 if (kind(logic, tt, "MONSTER")) return Monsters;
 if (kind(logic, tt, "INFANTRY")) return Swords;
 return Other;
}

bool AIStrategy::visible(GameLogic &logic, const Object &enemy, int player)
{
 const Player *viewer = logic.players().getNthPlayer(player);
 if (!viewer || InvisibilityManager::isStealthedAndUndetected(enemy, viewer)) return false;
 if (!logic.shroud()) return true; // Synthetic worlds without a visibility subsystem.
 const auto state = logic.shroud()->getObjectStatus(enemy, player);
 return state == OBJECTSHROUD_INVALID || state == OBJECTSHROUD_CLEAR || state == OBJECTSHROUD_PARTIAL_CLEAR;
}

AIStrategy::Situation AIStrategy::assess(GameLogic &logic, const AISkirmishPlayer &ai)
{
 Situation result;
 const Player *player = logic.players().getNthPlayer(ai.playerIndex);
 if (!player) return result;
 for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
 {
  if (!army(*o)) continue;
  const auto r = role(logic, *o->getTemplate());
  if (o->getControllingPlayer() == player) ++result.own[r];
  else if (player->getRelationship(o->getTeam()) == ENEMIES && visible(logic, *o, ai.playerIndex))
  {
   ++result.enemy[r];
   if (CombatQueries::centerDistanceSquared2D(ai.build.startPosition, *o->getPosition()) <= 360000.0f)
    result.homeThreat = true;
  }
 }
 // Count outstanding requests, so a busy factory is not asked for the same counter indefinitely.
 for (const auto &req : ai.build.units.requests)
  if (!req->finished())
   if (const auto *tt = logic.things().findTemplate(req->templateName)) ++result.own[role(logic, *tt)];
 return result;
}

int AIStrategy::productionNeed(Role candidate, const Situation &s)
{
 int desired = 0, importance = 0;
 switch (candidate)
 {
 case Pikes:
  desired = std::min(4, s.enemy[Cavalry] + (s.enemy[Monsters] + 1) / 2);
  if (s.own[Archers] >= 2) desired = std::max(desired, 1);
  importance = 40;
  break;
 case Archers:
  desired = std::min(4, s.enemy[Flyers] * 2 + (s.enemy[Swords] + s.enemy[Pikes] + 1) / 2);
  importance = s.enemy[Flyers] ? 50 : 25;
  break;
 case Cavalry:
  desired = s.enemy[Pikes] == 0 && s.enemy[Archers] >= 2 ? 1 : 0;
  importance = 20;
  break;
 case Swords:
  desired = std::min(3, (s.enemy[Pikes] + 1) / 2);
  importance = 15;
  break;
 default: break;
 }
 return desired > s.own[candidate] ? importance + desired - s.own[candidate] : 0;
}

void AIStrategy::update(GameLogic &logic, AISkirmishPlayer &ai)
{
 if (!ai.store || ai.disabled || ai.scriptControlled || ai.store->data().disableTacticalAI ||
     ai.store->data().disableTeamBuilding || logic.peaceTimeActive()) return;
 if (logic.getFrame() % 5 != (unsigned)ai.playerIndex % 5) return;
 Player *player = logic.players().getNthPlayer(ai.playerIndex);
 if (!player || player->getPlayerType() != PLAYER_COMPUTER) return;
 auto &recovering = ai.brain.strategyRecovering;
 for (auto it = recovering.begin(); it != recovering.end();)
 {
  Object *o = logic.findObjectByID(it->first);
  auto *body = o ? o->getBodyModule() : nullptr;
  if (!o || !army(*o) || o->getControllingPlayer() != player || !body ||
      logic.getFrame() >= it->second || body->getHealth() >= SimMath::mulf32(body->getMaxHealth(), 0.75f))
  { it = recovering.erase(it); }
  else ++it;
 }
 std::vector<Object *> enemies;
 for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
  if (o->isInWorld() && !o->getContainedBy() && CombatQueries::isAttackable(*o) &&
      player->getRelationship(o->getTeam()) == ENEMIES && visible(logic, *o, ai.playerIndex) &&
      !AIFortification::passiveBarrier(*o)) enemies.push_back(o);
 for (Object *unit = logic.getFirstObject(); unit; unit = unit->getNextObject())
 {
  if (unit->getControllingPlayer() != player || !army(*unit) || !unit->getAIUpdateInterface() ||
      ai.brain.siegeUnits.count(unit->getID()) || ai.brain.siegeEscorts.count(unit->getID())) continue;
  auto *u = unit->getAIUpdateInterface();
  auto *weapons = unit->getWeapons();
  const Role r = role(logic, *unit->getTemplate());
  // Retreat ownership lasts up to 15 seconds; it cannot be stolen by recruitment.
  if (recovering.count(unit->getID()))
  {
   if (u->isIdle() && CombatQueries::centerDistanceSquared2D(*unit->getPosition(), ai.build.startPosition) > 22500.0f)
    u->aiMoveToPosition(ai.build.startPosition, CMD_FROM_AI);
   continue;
  }
  Object *nearestThreat = nullptr;
  Object *pike = nullptr;
  for (Object *enemy : enemies)
  {
   const float d = distance(*unit, *enemy);
   if (enemy->isKindOfName("CAN_ATTACK") && d <= 90000.0f &&
       (!nearestThreat || d < distance(*unit, *nearestThreat))) nearestThreat = enemy;
   if (!enemy->isKindOfName("STRUCTURE") && role(logic, *enemy->getTemplate()) == Pikes && d <= 40000.0f &&
       (!pike || d < distance(*unit, *pike))) pike = enemy;
  }
  auto *body = unit->getBodyModule();
  if (nearestThreat && body && body->getMaxHealth() > 0 && body->getHealth() <= SimMath::mulf32(body->getMaxHealth(), 0.3f) &&
      (unit->isKindOfName("HERO") || r == Cavalry || r == Flyers))
  {
   releaseFromTactics(ai, unit->getID());
   recovering[unit->getID()] = logic.getFrame() + 75;
   u->aiIdle(CMD_FROM_AI);
   u->aiMoveToPosition(ai.build.startPosition, CMD_FROM_AI);
   continue;
  }
  // Avoid feeding cavalry into pikes; move away using normal pathfinding.
  if (r == Cavalry && pike)
  {
   releaseFromTactics(ai, unit->getID());
   recovering[unit->getID()] = logic.getFrame() + 5; // Hold ownership through this recruitment pass.
   Coord3D away = *unit->getPosition();
   const auto &threat = *pike->getPosition();
   const float dx = SimMath::subf32(away.x, threat.x), dy = SimMath::subf32(away.y, threat.y);
   away.x = SimMath::addf32(away.x, dx >= 0 ? 160.0f : -160.0f);
   away.y = SimMath::addf32(away.y, dy >= 0 ? 160.0f : -160.0f);
   away.z = logic.getGroundHeight(away.x, away.y);
   u->aiIdle(CMD_FROM_AI);
   u->aiMoveToPosition(away, CMD_FROM_AI);
   continue;
  }
  if (!weapons || r == Siege || r == Other || r == Flyers || r == Monsters) continue;
  Object *best = nullptr;
  int bestRank = 100;
  const float reach = r == Archers ? range(*unit) : 220.0f;
  for (Object *enemy : enemies)
  {
   const float d = distance(*unit, *enemy);
   if (d > SimMath::mulf32(reach, reach) || !weapons->canAttackObject(*enemy, CMD_FROM_AI, false)) continue;
   const Role er = role(logic, *enemy->getTemplate());
   int rank = 10;
   if (!enemy->isKindOfName("STRUCTURE"))
   {
    rank = 5;
    if ((r == Pikes && (er == Cavalry || er == Monsters)) ||
        (r == Archers && (er == Swords || er == Pikes || er == Flyers)) ||
        (r == Swords && er == Pikes) || (r == Cavalry && er == Archers)) rank = 0;
   }
   else if (enemy->isKindOfName("ECONOMY_STRUCTURE")) rank = 8;
   if (!best || rank < bestRank || (rank == bestRank && (d < distance(*unit, *best) ||
       (d == distance(*unit, *best) && enemy->getID() < best->getID()))))
   { best = enemy; bestRank = rank; }
  }
  if (best && u->currentVictimId() != best->getID()) u->aiAttackObject(best, CMD_FROM_AI);
 }
}
