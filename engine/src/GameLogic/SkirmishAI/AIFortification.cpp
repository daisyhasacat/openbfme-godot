// OpenBFME. GPL-3.0.
// Enhanced siege response, not a claim of retail tactic parity. The BFME2 donor's
// AISimpleSiegeTactic (reverse/attempts/0x005a9f70.cpp) attacks a wall with a siege
// team before the army advances. Here the live armor / optional wall policy decides
// whether a breach is needed; existing production and attack orders do the work.
#include "GameLogic/SkirmishAI/AIFortification.h"
#include "Common/Player.h"
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

Object *AIFortification::findTarget(GameLogic &logic, const AISkirmishPlayer &ai)
{
	const Player *player = logic.players().getNthPlayer(ai.playerIndex);
	if (!player) return nullptr;
	Object *best = nullptr;
	float bestDistance = 0;
	for (Object *target = logic.getFirstObject(); target; target = target->getNextObject())
	{
		if (!fortification(*target) || !target->isInWorld() || !CombatQueries::isAttackable(*target) ||
			player->getRelationship(target->getTeam()) != ENEMIES || !target->getBodyModule() || target->getBodyModule()->isIndestructible()) continue;
		bool protectedTarget = WallBalance::requiredAttackerKind(*target) != nullptr;
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
		if (!best || distance < bestDistance || (distance == bestDistance && target->getID() < best->getID()))
		{
			best = target;
			bestDistance = distance;
		}
	}
	return best;
}

void AIFortification::update(GameLogic &logic, AISkirmishPlayer &ai)
{
	if (logic.peaceTimeActive()) return;
	Object *target = findTarget(logic, ai);
	const auto previous = ai.brain.siegeUnits;
	ai.brain.siegeUnits.clear();
	const ObjectID oldTarget = ai.brain.fortificationTarget;
	ai.brain.fortificationTarget = target ? target->getID() : INVALID_ID;
	const Player *player = logic.players().getNthPlayer(ai.playerIndex);
	std::vector<Object *> candidates;
	for (Object *unit = logic.getFirstObject(); target && unit; unit = unit->getNextObject())
		if (unit->getControllingPlayer() == player && unit->isInWorld() && CombatQueries::isAlive(*unit) &&
			!unit->isKindOfName("STRUCTURE") && !unit->isKindOfName("IGNORES_SELECT_ALL") &&
			(unit->isKindOfName("SIEGEENGINE") || unit->isKindOfName("CAN_ATTACK_WALLS"))) candidates.push_back(unit);
	std::sort(candidates.begin(), candidates.end(), [&](const Object *a, const Object *b) {
		if (previous.count(a->getID()) != previous.count(b->getID())) return previous.count(a->getID()) > previous.count(b->getID());
		const float da = CombatQueries::centerDistanceSquared2D(*a->getPosition(), *target->getPosition());
		const float db = CombatQueries::centerDistanceSquared2D(*b->getPosition(), *target->getPosition());
		return da < db || (da == db && a->getID() < b->getID());
	});
	// Two engines are enough to open a breach; keep the remainder available to the army.
	for (Object *unit : candidates)
	{
		if (ai.brain.siegeUnits.size() == 2) break;
		if (unit->getControllingPlayer() != player || !unit->isInWorld() || !CombatQueries::isAlive(*unit) ||
			unit->isKindOfName("STRUCTURE") || unit->isKindOfName("IGNORES_SELECT_ALL") || unit->testStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("IS_LEAVING_FACTORY"))) continue;
		AIUpdateInterface *a = unit->getAIUpdateInterface();
		ObjectWeapons *weapons = unit->getWeapons();
		if (!a || !weapons || !(unit->isKindOfName("SIEGEENGINE") || unit->isKindOfName("CAN_ATTACK_WALLS")) ||
			!weapons->canAttackObject(*target, CMD_FROM_AI, false)) continue;
		if (a->currentVictimId() != target->getID() || a->isIdle())
			if (!a->aiAttackObject(target, CMD_FROM_AI)) continue;
		ai.brain.siegeUnits.insert(unit->getID());
		// The normal team builder must not overwrite the breach order.
		for (auto &tactic : ai.brain.tactics)
			for (auto &team : tactic.teams)
				team.members.erase(std::remove(team.members.begin(), team.members.end(), unit->getID()), team.members.end());
	}
	for (ObjectID id : previous)
	{
		Object *unit = logic.findObjectByID(id);
		AIUpdateInterface *a = unit ? unit->getAIUpdateInterface() : nullptr;
		if (a && unit->getControllingPlayer() == player && !ai.brain.siegeUnits.count(id) && a->currentVictimId() == oldTarget)
			a->aiIdle(CMD_FROM_AI);
	}
}
