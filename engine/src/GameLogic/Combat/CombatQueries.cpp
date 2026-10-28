// OpenBFME. GPL-3.0.
// See GameLogic/Combat/CombatQueries.h.

#include <algorithm>
#include <utility>
#include "GameLogic/Combat/CombatQueries.h"

#include "Common/Thing/ThingTemplate.h"
#include "Common/Player.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <stdexcept>

namespace CombatQueries
{
namespace
{
const ObjectMovementInfo &movementInfo(const Object &obj)
{
	AIWorld *world = obj.logic().aiWorld();
	if (!world)
	{
		throw std::logic_error("combat geometry: the game has no AIWorld (the geometry of an object is the pathfinder's)");
	}
	return world->movementInfo(*obj.getTemplate());
}
} // namespace

const PathfindGeometry &geometry(const Object &obj)
{
	return movementInfo(obj).geometry;
}

float boundingCircleRadius(const Object &obj)
{
	return movementInfo(obj).boundingCircle; // geometry(obj).boundingCircleRadius(), computed once per template
}

float boundingSphereRadius(const Object &obj)
{
	// RW obj + 0xBC = GeometryInfo + 0x14, computed by RW 0xAD2860 over every active shape (RW 0xAD2770; lane PATH-2, S-341)
	return movementInfo(obj).boundingSphere;
}

bool isBox(const Object &obj)
{
	return geometry(obj).type == PATHFIND_GEOMETRY_BOX;
}

float centerDistanceSquared2D(const Coord3D &a, const Coord3D &b)
{
	return SimMath::sumSquares2(SimMath::subf32(a.x, b.x), SimMath::subf32(a.y, b.y));
}

void sortByCenterDistance2D(std::vector<Object *> &objects, const Coord3D &center)
{
	std::vector<std::pair<float, Object *>> keyed;
	keyed.reserve(objects.size());
	for (Object *o : objects)
	{
		keyed.emplace_back(centerDistanceSquared2D(*o->getPosition(), center), o);
	}
	std::stable_sort(keyed.begin(), keyed.end(), [](const std::pair<float, Object *> &a, const std::pair<float, Object *> &b) { return a.first < b.first; });
	for (size_t i = 0; i < keyed.size(); ++i)
	{
		objects[i] = keyed[i].second;
	}
}

float edgeDistanceSquared2D(const Coord3D &posA, float radiusA, const Coord3D &posB, float radiusB)
{
	// RW 0x6634BF, x87 at PC24: the differences, squares and sum stay in the registers (the fstp qword of a PC24 value is exact), the CRT sqrt (RW 0xA3CF96)
	// on the double, then radius A and radius B subtracted at PC24; the fst dword is squared with mulss unless the wide distance is below 0 (fldz / fcomi / jbe)
	const double dx = SimMath::pc24SubW((double)posA.x, (double)posB.x);
	const double dy = SimMath::pc24SubW((double)posA.y, (double)posB.y);
	const double s = SimMath::pc24AddW(SimMath::pc24MulW(dy, dy), SimMath::pc24MulW(dx, dx)); // RW 0x6634E6 .. 0x6634F1
	const double d = SimMath::pc24SubW(SimMath::pc24SubW(SimMath::sqrtd(s), (double)radiusA), (double)radiusB);
	const float narrow = SimMath::fstpDword(d);
	return d < 0.0 ? 0.0f : SimMath::sseMul(narrow, narrow);
}

float edgeDistanceSquared2D(const Object &a, const Coord3D &posA, const Object &b, const Coord3D &posB)
{
	return edgeDistanceSquared2D(posA, boundingCircleRadius(a), posB, boundingCircleRadius(b));
}

bool isAlive(const Object &obj)
{
	return !obj.isDestroyed() && !obj.isEffectivelyDead();
}

// DONOR: Open-BFME-2 Common/PartitionFilterRejectBuildings_allow.cpp,
// RVA 0x261B7E, ctor PartitionFilterRejectBuildings.cpp. These are BFME2
// rules; RotWK parity is not established by this donor implementation.
bool allowsBuildingTarget(const Object &seeker, const Object &target)
{
	if (!target.isKindOfName("STRUCTURE") || target.isKindOfName("MINE")) return true;
	const Player *owner = seeker.getControllingPlayer();
	if (!owner) return false;
	const auto *contain = target.getContain();
	const Player *apparent = contain ? contain->getApparentControllingPlayer(owner) : nullptr;
	if (!apparent && !target.getControllingPlayer()) return false;
	if (owner->getPlayerType() == PLAYER_COMPUTER) return true;
	if (target.isKindOfName("FS_BASE_DEFENSE")) return true;
	return contain && target.isAbleToAttack();
}

// DONOR: AI/AIPlayerIsLocationSafe.cpp RVA 0x261246 (includes the AI
// interface exception at Object +0x258); ObjectRva0028D491.cpp 0x28D491/4C4.
// Flags CAPTURABLE and LINKED_TO_FLAG are not tested by either filter.
bool isSignificantBuildingTarget(const Object &target)
{
	if (!target.isKindOfName("STRUCTURE")) return true;
	for (const char *kind : {"FS_POWER", "FS_FACTORY", "FS_BASE_DEFENSE", "FS_TECHNOLOGY", "FS_CASH_PRODUCER"})
		if (target.isKindOfName(kind)) return true;
	if (target.getAIUpdateInterface()) return true;
	const auto *contain = target.getContain();
	return !contain || (contain->isGarrisonable() && contain->getContainCount() != 0);
}

bool isAttackable(const Object &obj)
{
	const CombatNames::Kind &k = CombatNames::kinds();
	const CombatNames::Status &st = CombatNames::statuses();
	// lane AI-2 r6: a structure under construction IS a target. ZH's AI::findClosestEnemy (AI.cpp:588) filters with PartitionFilterPossibleToAttack ->
	// WeaponSet::getAbleToAttackSpecificObject (WeaponSet.cpp:458), which rejects MASKED, UNATTACKABLE and NO_ATTACK_FROM_AI but not UNDER_CONSTRUCTION, and this port's
	// ObjectWeapons::canAttackObject agrees; the COMBAT-1 stand-in excluded it, so an unfinished structure whose builder died (83 % TrollCage) could never be acquired
	// and kept its player alive for good (S-420). TARGET (lane DECOMP-2): RW's AI::findClosestEnemy (RW 0x701443, BFME2 decomp AIClosestObjectQueries.cpp tier A) runs
	// the worker RW 0x7006D7, whose status tests are 0x17 / 0x25 / 0x26 / 0x4F (never UNDER_CONSTRUCTION = 2), and its PossibleToAttack filter (RW 0x660CA4 tier A) ->
	// Object::getAbleToAttackSpecificObject RW 0x68D6A6 -> WeaponSet RW 0x6C9147 (statuses 0x33 NOT_IN_WORLD, 0x25, 0x3C UNATTACKABLE, 0x1A NO_ATTACK_FROM_AI, 0x1B)
	// -> RW 0x6C8652 (0x25 only): an unfinished structure is attackable there too
	// lane GARRISON-1: and in the partition: a contain's rider out of the world (RW 0x68C18F) is found by no scan
	return isAlive(obj) && obj.isInWorld() && !obj.isKindOf((unsigned)k.unattackable) && !obj.isKindOf((unsigned)k.inert) &&
		!obj.testStatus((unsigned)st.unattackable);
}

bool isAbleToAttack(const Object &obj)
{
	return obj.isAbleToAttack(); // RW 0x691269 (lane DECOMP-3)
}
} // namespace CombatQueries
