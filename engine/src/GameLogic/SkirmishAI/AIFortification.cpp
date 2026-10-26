// OpenBFME. GPL-3.0.
// Enhanced siege response, not a claim of retail tactic parity. The BFME2 donor's
// AISimpleSiegeTactic (reverse/attempts/0x005a9f70.cpp) attacks a wall with a siege
// team before the army advances. Here the live armor / optional wall policy decides
// whether a breach is needed; existing production and attack orders do the work.
#include "GameLogic/SkirmishAI/AIFortification.h"
#include "Common/Player.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "Common/PlayerList.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Thing/ThingFactory.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "GameLogic/SkirmishAI/AIStrategy.h"
#include "GameLogic/WallBalance.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponNugget.h"
#include "GameLogic/WeaponStores.h"
#include <algorithm>


namespace
{
bool kind(GameLogic &logic, const ThingTemplate &tt, const char *name)
{
	const int bit = ObjectTemplateInfoBuilder::kindOfIndex(name);
	return bit >= 0 && MaskTest(logic.templateInfo(&tt).kindOf, (unsigned)bit);
}

// A production estimate, not a simulated shot: no objects are spawned, no ammo or
// random values consumed. Live canAttackObject validates each deployed unit again.
bool weaponCanBreach(GameLogic &logic, const WeaponTemplate &weapon, Object &target, const Player *owner, int depth)
{
	if (depth > 4 || !(weapon.m_antiMask & (WEAPON_ANTI_GROUND | WEAPON_ANTI_STRUCTURE))) return false;
	for (const auto &np : weapon.m_nuggets)
	{
		const WeaponNugget &n = *np;
		// Upgrade-dependent damage is not available on a newly trained unit.
		if (!n.m_requiredUpgradeNames.empty()) continue;
		if (ObjectFilterMatch::isValid(&n.m_specialObjectFilter) &&
			!ObjectFilterMatch::allows(logic, n.m_specialObjectFilter, target, owner)) continue;
		if (n.kind() == NUGGET_DAMAGE || n.kind() == NUGGET_DOT)
		{
			const auto &d = static_cast<const DamageNugget &>(n);
			DamageInfoInput in;
			in.m_amount = d.m_damage;
			in.m_damageType = d.m_damageType;
			for (const auto &scalar : d.m_damageScalar)
				if (ObjectFilterMatch::allows(logic, scalar.filter, target, owner))
				{
					in.m_amount = SimMath::mulf32(in.m_amount, scalar.scalar);
					break; // FillDamageInfo uses the first matching scalar.
				}
			if (target.getBodyModule()->estimateDamage(in) > 0.0f) return true;
		}
		else if (n.kind() == NUGGET_PROJECTILE || n.kind() == NUGGET_DAMAGE_FIELD)
		{
			const std::string &name = n.kind() == NUGGET_PROJECTILE ?
				static_cast<const ProjectileNugget &>(n).m_warheadTemplateName :
				static_cast<const DamageFieldNugget &>(n).m_weaponTemplateName;
			const WeaponTemplate *warhead = TheWeaponStore ? TheWeaponStore->findWeaponTemplate(name) : nullptr;
			if (warhead && weaponCanBreach(logic, *warhead, target, owner, depth + 1)) return true;
		}
	}
	return false;
}

bool fortification(const Object &o)
{
	return WallBalance::wall(o) || (o.isKindOfName("STRUCTURE") &&
		(o.isKindOfName("COMMANDCENTER") || o.isKindOfName("DOZER_FACTORY") || o.isKindOfName("FS_BASE_DEFENSE")));
}
}

namespace {
bool siegeDamage(const WeaponTemplate &weapon,int depth=0)
{
 if(depth>4)return false;
 for(const auto &n:weapon.m_nuggets)
 {
  if(n->kind()==NUGGET_DAMAGE || n->kind()==NUGGET_DOT)
  {
   const auto &damage=static_cast<const DamageNugget &>(*n);
   if(damage.m_damage>0 && damage.m_damageType==DAMAGE_SIEGE)return true;
  }
  else if(n->kind()==NUGGET_PROJECTILE || n->kind()==NUGGET_DAMAGE_FIELD)
  {
   const auto &name=n->kind()==NUGGET_PROJECTILE ? static_cast<const ProjectileNugget &>(*n).m_warheadTemplateName : static_cast<const DamageFieldNugget &>(*n).m_weaponTemplateName;
   const auto *warhead=TheWeaponStore ? TheWeaponStore->findWeaponTemplate(name) : nullptr;
   if(warhead && siegeDamage(*warhead,depth+1))return true;
  }
 }
 return false;
}
}

float AIFortification::siegeRange(const ThingTemplate &unit)
{
 const auto *set=FindWeaponTemplateSet(unit.weaponTemplateSets(),WeaponSetFlags{});
 float range=0;
 for(int i=0;set && i<WEAPONSLOT_COUNT;++i)
  if(const auto *weapon=set->m_template[i])
   if(!weapon->m_meleeWeapon && siegeDamage(*weapon) && (weapon->m_antiMask & (WEAPON_ANTI_GROUND|WEAPON_ANTI_STRUCTURE))) range=std::max(range,weapon->m_attackRange);
 return range;
}

bool AIFortification::canBreach(GameLogic &logic, const ThingTemplate &tt, Object &target, const Player *owner)
{
	if (!target.getBodyModule() || target.getBodyModule()->isIndestructible()) return false;
	if (!kind(logic, tt, "SIEGEENGINE") && !kind(logic, tt, "CAN_ATTACK_WALLS")) return false;
	const char *required = WallBalance::requiredAttackerKind(target);
	if (required && !kind(logic, tt, required)) return false;
	const WeaponTemplateSet *set = FindWeaponTemplateSet(tt.weaponTemplateSets(), WeaponSetFlags{});
	if (!set) return false;
	for (int i = 0; i < WEAPONSLOT_COUNT; ++i)
	{
		bool restricted = false, matches = false, conditional = false;
		for (size_t w = 0; w < target.getKindOf().size(); ++w)
		{
			restricted |= set->m_onlyAgainst[i][w] != 0;
			matches |= (set->m_onlyAgainst[i][w] & target.getKindOf()[w]) != 0;
		}
		for (auto word : set->m_onlyInCondition[i]) conditional |= word != 0;
		if ((restricted && !matches) || conditional || !(set->m_autoChooseMask[i] & (1u << CMD_FROM_AI))) continue;
		if (set->m_template[i] && !set->m_template[i]->m_bombardType && weaponCanBreach(logic, *set->m_template[i], target, owner, 0)) return true;
	}
	return false;
}

bool AIFortification::canBreach(const Object &unit, Object &target)
{
	ObjectWeapons *weapons = unit.getWeapons();
	if (!weapons || !(unit.isKindOfName("SIEGEENGINE") || unit.isKindOfName("CAN_ATTACK_WALLS"))) return false;
	for (int i = 0; i < WEAPONSLOT_COUNT; ++i)
		if (Weapon *w = weapons->weaponInSlot(i))
			if (w->getTemplate() && (w->getTemplate()->m_antiMask & (WEAPON_ANTI_GROUND | WEAPON_ANTI_STRUCTURE)) && weapons->canDamage(*w, target)) return true;
	return false;
}

bool AIFortification::factoryCanBreach(GameLogic &logic, const ThingTemplate &factory, Object &target, const Player *owner)
{
	const FieldValue *v = factory.findField("CommandSet");
	const auto *name = v ? std::get_if<std::string>(v) : nullptr;
	const CommandSet *set = name && TheCommandStore ? TheCommandStore->findCommandSet(*name) : nullptr;
	for (int i = 0; set && i < CommandSet::MAX_BUTTONS; ++i)
	{
		const CommandButton *b = set->getCommandButton(i);
		if (!b || b->m_command != GUI_COMMAND_UNIT_BUILD) continue;
		const ThingTemplate *tt = logic.things().findTemplate(b->m_objectName);
		if (tt && canBreach(logic, *tt->getFinalOverride(), target, owner)) return true;
	}
	return false;
}

// User-requested OpenBFME policy, not retail parity: inert fortifications are
// obstacles to an objective, not objectives in their own right. Armed wall
// upgrades (towers/emplacements) remain combat targets.
bool AIFortification::passiveBarrier(const Object &o)
{
 return WallBalance::wall(o) && !o.isKindOfName("CAN_ATTACK");
}

bool AIFortification::allowAutoTarget(const Object &unit, const Object &target)
{
 const Player *owner=unit.getControllingPlayer();
 if(!owner || owner->getPlayerType()!=PLAYER_COMPUTER)return true;
 const auto *ai=unit.logic().skirmishAI().findAI(owner->getPlayerIndex());
 // Escorts defend the siege line; acquiring an armed wall tower would walk them
 // back into the wall even after the tactic's attack-move was removed.
 if (ai && ai->brain.siegeEscorts.count(unit.getID()) && WallBalance::wall(target)) return false;
 if (!passiveBarrier(target)) return true;
 return ai && ai->brain.siegeLaunched && ai->brain.fortificationTarget==target.getID() && ai->brain.siegeUnits.count(unit.getID());
}

namespace {
struct BreachRoute { const Object *unit=nullptr;const Object *objective=nullptr;bool open=false; };
BreachRoute breachRoute(GameLogic &logic,const Player &player,const Coord3D &base)
{
 BreachRoute route;
 bool barriers=false;
 for(Object *o=logic.getFirstObject();o;o=o->getNextObject())
  if(o->isInWorld() && CombatQueries::isAlive(*o) && player.getRelationship(o->getTeam())==ENEMIES && AIFortification::passiveBarrier(*o)) {barriers=true;break;}
 if(!barriers)return route;
 float best=0;
 for(Object *o=logic.getFirstObject();o;o=o->getNextObject()) {
  if(!o->isInWorld() || !CombatQueries::isAttackable(*o) || player.getRelationship(o->getTeam())!=ENEMIES ||
   !o->isKindOfName("STRUCTURE") || AIFortification::passiveBarrier(*o) || o->isKindOfName("IGNORE_FOR_VICTORY"))continue;
  float d=CombatQueries::centerDistanceSquared2D(base,*o->getPosition());
  if(!route.objective || d<best || (d==best && o->getID()<route.objective->getID())) {route.objective=o;best=d;}
 }
 if(!route.objective)return route;
 for(Object *o=logic.getFirstObject();o;o=o->getNextObject()) {
  // not a unit the AI leaves alone (GameLogic::aiReserved: a wave spawner's): it is no part of the army, and one already fighting at the walls
  // makes every approach search below fail at full cost, twice a logic frame
  if(o->getControllingPlayer()!=&player || !o->isInWorld() || !CombatQueries::isAlive(*o) || !o->getAIUpdateInterface() ||
   o->isKindOfName("STRUCTURE") || o->isKindOfName("AIRCRAFT") || o->isKindOfName("DOZER") || !o->isKindOfName("CAN_ATTACK") || logic.aiReserved(*o))continue;
  float d=CombatQueries::centerDistanceSquared2D(*o->getPosition(),*route.objective->getPosition());
  if(!route.unit || d<best || (d==best && o->getID()<route.unit->getID())) {route.unit=o;best=d;}
 }
 if(!route.unit || !logic.aiWorld())return route;
 // Test walkable approach points outside the objective footprint, rather than
 // its blocked centre. Full paths include live structures and change after a breach.
 auto loco=route.unit->getAIUpdateInterface()->locomotorInfo();
 float radius=CombatQueries::boundingCircleRadius(*route.objective)+40.0f;
 for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)if(x || y) {
  Coord3D to=*route.objective->getPosition();to.x+=x*radius;to.y+=y*radius;
  bool partial=false;
  auto *unitAI=route.unit->getAIUpdateInterface();
  std::unique_ptr<Path> path(logic.aiWorld()->pathfinder().findPath(&unitAI->pathfindObject(),loco,route.unit->getPosition(),&to,&partial));
  if(path && !partial) {route.open=true;return route;}
 }
 return route;
}
bool onBreachRoute(const Object &barrier,const BreachRoute &route)
{
 if(!route.unit || !route.objective || route.open)return false;
 const auto &a=*route.unit->getPosition(),&b=*route.objective->getPosition(),&p=*barrier.getPosition();
 float dx=b.x-a.x,dy=b.y-a.y,len=dx*dx+dy*dy;if(len<=0)return false;
 float t=((p.x-a.x)*dx+(p.y-a.y)*dy)/len;if(t<0 || t>1)return false;
 float x=p.x-a.x-t*dx,y=p.y-a.y-t*dy;
 float width=CombatQueries::boundingCircleRadius(barrier)+60.0f;
 return x*x+y*y<=width*width;
}
}

Object *AIFortification::findTarget(GameLogic &logic, const AISkirmishPlayer &ai)
{
	const Player *player = logic.players().getNthPlayer(ai.playerIndex);
	if (!player) return nullptr;
	Object *best = nullptr;
	float bestDistance = 0;
 const auto route=breachRoute(logic,*player,ai.build.startPosition);
 int bestRank=99;
	for (Object *target = logic.getFirstObject(); target; target = target->getNextObject())
	{
		if (!fortification(*target) || !target->isInWorld() || !CombatQueries::isAttackable(*target) ||
			player->getRelationship(target->getTeam()) != ENEMIES || !target->getBodyModule() || target->getBodyModule()->isIndestructible()) continue;
		if(passiveBarrier(*target) && !onBreachRoute(*target,route))continue;
		bool protectedTarget = passiveBarrier(*target) || WallBalance::requiredAttackerKind(*target) != nullptr;
		if (!protectedTarget)
		{
			bool ordinary = false, vulnerable = false;
			for (Object *unit = logic.getFirstObject(); unit; unit = unit->getNextObject())
			{
				if (unit->getControllingPlayer() != player || !unit->isInWorld() || !CombatQueries::isAlive(*unit) ||
					unit->isKindOfName("STRUCTURE") || unit->isKindOfName("SIEGEENGINE") || unit->isKindOfName("CAN_ATTACK_WALLS")) continue;
				ObjectWeapons *weapons = unit->getWeapons();
				if (!weapons || !weapons->hasAnyDamageWeapon()) continue;
				ordinary = true;
				for (int i = 0; i < WEAPONSLOT_COUNT && !vulnerable; ++i)
					if (Weapon *w = weapons->weaponInSlot(i))
						vulnerable = w->getTemplate() && (w->getTemplate()->m_antiMask & (WEAPON_ANTI_GROUND | WEAPON_ANTI_STRUCTURE)) && weapons->canDamage(*w, *target);
				if (vulnerable) break;
			}
			protectedTarget = ordinary && !vulnerable;
		}
		if (!protectedTarget) continue;
		const float distance = CombatQueries::centerDistanceSquared2D(ai.build.startPosition, *target->getPosition());
		int rank=passiveBarrier(*target) ? (target->isKindOfName("WALL_HUB") ? 1 : 0) : 2;
		if (!best || rank<bestRank || (rank==bestRank && (distance < bestDistance || (distance == bestDistance && target->getID() < best->getID()))))
		{
			best = target;
			bestDistance = distance;bestRank=rank;
		}
	}
	return best;
}

void AIFortification::update(GameLogic &logic, AISkirmishPlayer &ai)
{
 if (logic.peaceTimeActive()) return;
 Object *target = findTarget(logic, ai);
 auto &b = ai.brain;
 const auto previous = b.siegeUnits;
 const auto previousEscorts = b.siegeEscorts;
 const ObjectID oldTarget = b.fortificationTarget;
 b.siegeUnits.clear(); b.siegeEscorts.clear();
 b.fortificationTarget = target ? target->getID() : INVALID_ID;
 const auto *oldObject = logic.findObjectByID(oldTarget);
 if (oldTarget == INVALID_ID || !target || (target && passiveBarrier(*target) && oldObject && !passiveBarrier(*oldObject)))
 {
  b.siegeGatherFrame = logic.getFrame();
  b.siegeLaunched = false;
 }
 const Player *player = logic.players().getNthPlayer(ai.playerIndex);
 std::vector<Object *> candidates;
 for (Object *unit = logic.getFirstObject(); target && unit; unit = unit->getNextObject())
  if (unit->getControllingPlayer() == player && unit->isInWorld() && CombatQueries::isAlive(*unit) &&
      !unit->getContainedBy() && !b.strategyRecovering.count(unit->getID()) && !unit->isKindOfName("STRUCTURE") && !unit->isKindOfName("IGNORES_SELECT_ALL") &&
      (unit->isKindOfName("SIEGEENGINE") || unit->isKindOfName("CAN_ATTACK_WALLS"))) candidates.push_back(unit);
 std::sort(candidates.begin(), candidates.end(), [&](const Object *a, const Object *c) {
  const float ar=siegeRange(*a->getTemplate()),cr=siegeRange(*c->getTemplate());
  if(ar!=cr)return ar>cr;
  if (previous.count(a->getID()) != previous.count(c->getID())) return previous.count(a->getID()) > previous.count(c->getID());
  const float da = CombatQueries::centerDistanceSquared2D(*a->getPosition(), *target->getPosition());
  const float dc = CombatQueries::centerDistanceSquared2D(*c->getPosition(), *target->getPosition());
  return da < dc || (da == dc && a->getID() < c->getID());
 });
 for (Object *unit : candidates)
 {
  if (b.siegeUnits.size() == 5) break;
  auto *a = unit->getAIUpdateInterface();
  auto *weapons = unit->getWeapons();
  if (!a || !weapons || unit->testStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("IS_LEAVING_FACTORY")) ||
      !weapons->canAttackObject(*target, CMD_FROM_AI, false)) continue;
  b.siegeUnits.insert(unit->getID());
 }
 const bool staging = target && passiveBarrier(*target);
 if (staging && b.siegeUnits.empty()) b.siegeLaunched = false;
 if (staging || (target && !previousEscorts.empty()))
 {
  for (Object *unit = logic.getFirstObject(); unit; unit = unit->getNextObject())
  {
   // An intact breach blocks the whole army, not just six escorts. Surplus
   // infantry and previously dispatched squads must not march in without siege.
   if (!staging && b.siegeEscorts.size() == 6) break;
   auto *a = unit->getAIUpdateInterface();
   if (unit->getControllingPlayer() != player || !unit->isInWorld() || !CombatQueries::isAlive(*unit) || !a ||
       unit->getContainedBy() || AIStrategy::capturing(*unit) || b.strategyRecovering.count(unit->getID()) || unit->isUnderConstruction() || !unit->isKindOfName("CAN_ATTACK") ||
       unit->isKindOfName("STRUCTURE") || unit->isKindOfName("DOZER") || unit->isKindOfName("IGNORES_SELECT_ALL") ||
       unit->isKindOfName("SIEGEENGINE") || b.siegeUnits.count(unit->getID())) continue;
   bool assigned = false;
   for (const auto &tactic : b.tactics) for (const auto &team : tactic.teams)
    assigned = assigned || (std::find(team.members.begin(), team.members.end(), unit->getID()) != team.members.end() &&
                            !(team.type == 0 && team.wantsMembers && !team.handedOver));
   if (!staging && !previousEscorts.count(unit->getID()) && (assigned || a->currentVictim())) continue;
   b.siegeEscorts.insert(unit->getID());
  }
 }
 Coord3D rally = ai.build.startPosition;
 // Gather on the home side. A time limit prevents an incomplete army waiting forever.
 bool assembled = b.siegeUnits.size() >= 3 && b.siegeEscorts.size() >= 6;
 for (ObjectID id : b.siegeUnits)
  if (const Object *unit = logic.findObjectByID(id)) assembled = assembled && CombatQueries::centerDistanceSquared2D(*unit->getPosition(), rally) <= 202500.0f;
 for (ObjectID id : b.siegeEscorts)
  if (const Object *unit = logic.findObjectByID(id)) assembled = assembled && CombatQueries::centerDistanceSquared2D(*unit->getPosition(), rally) <= 202500.0f;
 if (!staging || assembled || (!b.siegeUnits.empty() && logic.getFrame() - b.siegeGatherFrame >= 225u /* 45 seconds at 5 Hz */))
  b.siegeLaunched = target != nullptr;
 auto orders = [&](ObjectID id, bool siege) {
  Object *unit = logic.findObjectByID(id);
  auto *a = unit ? unit->getAIUpdateInterface() : nullptr;
  if (!a || !target) return;
  if (!b.siegeLaunched)
  {
   // A policy takeover must cancel the previous attack-move. CMD_FROM_AI
   // idle intentionally preserves it (temporary combat diversion semantics).
   if (!(siege ? previous : previousEscorts).count(id)) a->aiIdle(CMD_FROM_PLAYER);
   if (CombatQueries::centerDistanceSquared2D(*unit->getPosition(), rally) > 40000.0f && a->isIdle()) a->aiMoveToPosition(rally, CMD_FROM_AI);
  }
  else if (siege)
  {
   if (a->currentVictimId() != target->getID() || a->isIdle() || (logic.getFrame()%50==id%50 && a->mover().isBlocked())) a->aiAttackObject(target, CMD_FROM_AI);
  }
  else if (staging)
  {
   // Ordinary troops must not attack-move into an intact wall. Hold on the
   // home side behind the siege firing line until findTarget sees an open route.
   float standOff = 450.0f;
   for (ObjectID engine : b.siegeUnits)
    if (const Object *artillery = logic.findObjectByID(engine))
     standOff = std::max(standOff, SimMath::addf32(siegeRange(*artillery->getTemplate()), 200.0f));
   Coord3D hold = rally;
   const Coord3D &wall = *target->getPosition();
   const float dx = SimMath::subf32(rally.x, wall.x), dy = SimMath::subf32(rally.y, wall.y);
   const float length = (float)SimMath::sqrtd(SimMath::addf32(SimMath::mulf32(dx,dx),SimMath::mulf32(dy,dy)));
   if (length > standOff)
   {
    hold.x = SimMath::addf32(wall.x,SimMath::mulf32(SimMath::divf32(dx,length),standOff));
    hold.y = SimMath::addf32(wall.y,SimMath::mulf32(SimMath::divf32(dy,length),standOff));
    hold.z = logic.getGroundHeight(hold.x,hold.y);
   }
   const bool tooClose = CombatQueries::centerDistanceSquared2D(*unit->getPosition(),wall) < SimMath::mulf32(standOff,standOff);
   const bool wrongGoal = CombatQueries::centerDistanceSquared2D(a->stateMachine().goalPosition(),hold) > 2500.0f;
   if (!previousEscorts.count(id) || (tooClose && wrongGoal) || a->currentVictim()) a->aiIdle(CMD_FROM_PLAYER);
   if (a->isIdle() && CombatQueries::centerDistanceSquared2D(*unit->getPosition(),hold) > 2500.0f)
    a->aiMoveToPosition(hold,CMD_FROM_AI);
  }
  else if (a->isIdle())
  {
   a->aiMoveToPosition(*target->getPosition(), CMD_FROM_AI);
   a->armAttackMove(*target->getPosition(), CMD_FROM_AI);
  }
  for (auto &tactic : b.tactics) for (auto &team : tactic.teams)
   team.members.erase(std::remove(team.members.begin(), team.members.end(), id), team.members.end());
 };
 for (ObjectID id : b.siegeUnits) orders(id, true);
 for (ObjectID id : b.siegeEscorts) orders(id, false);
 auto release = [&](ObjectID id) {
  Object *unit = logic.findObjectByID(id);
  auto *a = unit ? unit->getAIUpdateInterface() : nullptr;
  if (a && unit->getControllingPlayer() == player && !b.siegeUnits.count(id) && !b.siegeEscorts.count(id)) a->aiIdle(CMD_FROM_AI);
 };
 for (ObjectID id : previous) release(id);
 for (ObjectID id : previousEscorts) release(id);
}
