// OpenBFME. GPL-3.0. Large owned-army target acquisition: synthetic data only.
#include "doctest.h"

#include "CombatTestUtil.h"
#include "PerfBenchUtil.h"
#include "GameLogic/Combat/TargetFinder.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/SkirmishAI/AIFortification.h"

namespace
{
const char kArmyUnit[] = "ChildObject ArmySoldier Slasher\n CommandPoints = 5\nEnd\n";

std::vector<Object *> army(combattest::CombatWorld &w, int count)
{
	std::vector<Object *> out;
	for (int i = 0; i < count; ++i)
		out.push_back(w.unit("ArmySoldier", 'A', 200.f + (i % 30) * 4.f, 200.f + (i / 30) * 4.f));
	return out;
}

// The pre-optimization path: public radius query, then the original enemy/weapon filters in cell/list order.
Object *legacyClosest(TargetFinder &finder, const Object &seeker, const Coord3D &center, float range, float maxRadius, unsigned flags)
{
	std::vector<Object *> near;
	finder.collectInRadius(center, SimMath::addf32(range, maxRadius), near);
	const auto &k = CombatNames::kinds();
	const auto &st = CombatNames::statuses();
	Object *best = nullptr;
	float bestD = 0;
	for (Object *o : near)
	{
		if (o == &seeker || seeker.getRelationship(*o) != ENEMIES)
			continue;
		if ((flags & TargetFinder::HORDES_ONLY) && o->testStatus((unsigned)st.hordeMember))
			continue;
		if ((flags & TargetFinder::MEMBERS_ONLY) && o->isKindOf((unsigned)k.horde))
			continue;
		if (!(flags & TargetFinder::ALLOW_STRUCTURES) && !CombatQueries::allowsBuildingTarget(seeker, *o))
			continue;
		if ((flags & TargetFinder::IGNORE_INSIGNIFICANT_BUILDINGS) && !CombatQueries::isSignificantBuildingTarget(*o))
			continue;
		if (o->isKindOf((unsigned)k.projectile))
			continue;
		float edge = SimMath::subf32(SimMath::length2d(SimMath::subf32(o->getPosition()->x, center.x), SimMath::subf32(o->getPosition()->y, center.y)),
									 CombatQueries::boundingCircleRadius(*o));
		if (edge > range || !AIFortification::allowAutoTarget(seeker, *o) || !seeker.getWeapons()->canAttackObject(*o, CMD_FROM_AI, false))
			continue;
		if (!best || edge < bestD || (edge == bestD && o->getID() < best->getID()))
		{
			best = o;
			bestD = edge;
		}
	}
	return best;
}
} // namespace

TEST_CASE("large army targets bench: 3000 and 6000 CP owned, idle acquisition and enemy contact" * doctest::skip())
{
	for (int units : {600, 1200})
	{
		combattest::CombatWorld w(kArmyUnit);
		w.combat().setAutoAcquireEnabled(false);
		const auto soldiers = army(w, units);
		CHECK(w.logic->economy().commandPointUsageOf(*soldiers[0]) == 5);
		TargetFinder &finder = w.combat().targets();
		Object *enemy = nullptr;
		for (bool contact : {false, true})
		{
			if (contact)
				enemy = w.unit("Dummy", 'B', 340, 260);
			finder.findClosestEnemy(*soldiers[0], 250, TargetFinder::MEMBERS_ONLY, CMD_FROM_AI); // warm index
			const auto hashBefore = w.logic->computeStateHash();
			float maxRadius = 0;
			for (Object *o = w.logic->getFirstObject(); o; o = o->getNextObject())
				if (CombatQueries::isAttackable(*o))
					maxRadius = std::max(maxRadius, CombatQueries::boundingCircleRadius(*o));
			std::uint32_t referenceDigest = 0;
			for (bool reference : {true, false, false, true})
			{
				const double start = perfbench::nowMs();
				std::uint32_t digest = 0;
				for (int pass = 0; pass < 10; ++pass)
				{
					finder.reset(); // include one index rebuild per acquisition pass, as at each new logic frame
					for (Object *s : soldiers)
					{
						Object *found = reference ? legacyClosest(finder, *s, *s->getPosition(), 250, maxRadius, TargetFinder::MEMBERS_ONLY)
												  : finder.findClosestEnemy(*s, 250, TargetFinder::MEMBERS_ONLY, CMD_FROM_AI);
						digest = (digest * 33u) ^ (found ? found->getID() : 0);
					}
				}
				const double elapsed = perfbench::nowMs() - start;
				if (reference)
					referenceDigest = digest;
				CHECK(digest == referenceDigest);
				CHECK(w.logic->computeStateHash() == hashBefore);
				CHECK(finder.findClosestEnemy(*soldiers[0], 250, TargetFinder::MEMBERS_ONLY, CMD_FROM_AI) == enemy);
				std::printf("large army targets: %d CP, %zu units, contact %d, %s, %zu queries, %.3f ms CPU, digest %08x, state %08x\n", units * 5,
							soldiers.size(), contact ? 1 : 0, reference ? "legacy" : "optimized", soldiers.size() * 10, elapsed, digest, hashBefore);
			}
		}
	}
}

TEST_CASE("large army targets: target choices and public radius order equal the original scan")
{
	combattest::CombatWorld w(kArmyUnit);
	w.combat().setAutoAcquireEnabled(false);
	const auto soldiers = army(w, 120);
	for (int i = 0; i < 30; ++i)
		w.unit(i % 2 ? "Dummy" : "Slasher", i % 3 ? 'B' : 'A', 180.f + (i % 10) * 17.f, 180.f + (i / 10) * 31.f);
	auto &finder = w.combat().targets();
	float maxRadius = 0;
	for (Object *o = w.logic->getFirstObject(); o; o = o->getNextObject())
		if (CombatQueries::isAttackable(*o))
			maxRadius = std::max(maxRadius, CombatQueries::boundingCircleRadius(*o));
	const auto hash = w.logic->computeStateHash();
	for (unsigned flags : {0u, 1u, 2u, 4u, 5u, 7u})
		for (Object *s : soldiers)
			CHECK(finder.findClosestEnemy(*s, 150, flags, CMD_FROM_AI) == legacyClosest(finder, *s, *s->getPosition(), 150, maxRadius, flags));
	CHECK(w.logic->computeStateHash() == hash);
}

TEST_CASE("large army targets: same-frame diplomacy and ownership changes are visible to every finder")
{
	combattest::CombatWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *seeker = w.unit("Slasher", 'A', 200, 200);
	Object *other = w.unit("Dummy", 'B', 220, 200);
	TargetFinder standalone(*w.logic);
	for (TargetFinder *finder : {&w.combat().targets(), &standalone})
		REQUIRE(finder->findClosestEnemy(*seeker, 100, 0, CMD_FROM_AI) == other);
	const auto frame = w.logic->getFrame();
	w.teamA()->setTeamRelationship(w.teamB(), ALLIES);
	for (TargetFinder *finder : {&w.combat().targets(), &standalone})
		CHECK(finder->findClosestEnemy(*seeker, 100, 0, CMD_FROM_AI) == nullptr);
	w.teamA()->setTeamRelationship(w.teamB(), ENEMIES);
	for (TargetFinder *finder : {&w.combat().targets(), &standalone})
		CHECK(finder->findClosestEnemy(*seeker, 100, 0, CMD_FROM_AI) == other);
	other->setTeam(w.teamA());
	for (TargetFinder *finder : {&w.combat().targets(), &standalone})
		CHECK(finder->findClosestEnemy(*seeker, 100, 0, CMD_FROM_AI) == nullptr);
	other->setTeam(w.teamB());
	for (TargetFinder *finder : {&w.combat().targets(), &standalone})
		CHECK(finder->findClosestEnemy(*seeker, 100, 0, CMD_FROM_AI) == other);
	other->setTeam(nullptr);
	for (TargetFinder *finder : {&w.combat().targets(), &standalone})
		CHECK(finder->findClosestEnemy(*seeker, 100, 0, CMD_FROM_AI) == nullptr);
	other->setTeam(w.teamB());
	seeker->setTeam(w.teamB());
	for (TargetFinder *finder : {&w.combat().targets(), &standalone})
		CHECK(finder->findClosestEnemy(*seeker, 100, 0, CMD_FROM_AI) == nullptr);
	seeker->setTeam(w.teamA());
	w.teamA()->removeAllRelationshipOverrides();
	w.teamA()->getControllingPlayer()->setPlayerRelationship(w.teamB()->getControllingPlayer(), ALLIES);
	for (TargetFinder *finder : {&w.combat().targets(), &standalone})
		CHECK(finder->findClosestEnemy(*seeker, 100, 0, CMD_FROM_AI) == nullptr);
	w.teamA()->getControllingPlayer()->setPlayerRelationship(w.teamB()->getControllingPlayer(), ENEMIES);
	for (TargetFinder *finder : {&w.combat().targets(), &standalone})
		CHECK(finder->findClosestEnemy(*seeker, 100, 0, CMD_FROM_AI) == other);
	CHECK(w.logic->getFrame() == frame);
}

TEST_CASE("large army targets: team refresh preserves the existing frame's spatial index")
{
	combattest::CombatWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *seeker = w.unit("Slasher", 'A', 500, 500);
	Object *other = w.unit("Dummy", 'A', 200, 200);
	auto &finder = w.combat().targets();
	REQUIRE(finder.findClosestEnemy(*seeker, 50, 0, CMD_FROM_AI) == nullptr);
	Coord3D moved{520, 500, 0};
	other->setPosition(&moved);
	other->setTeam(w.teamB());
	// Existing behavior freezes the cell assignment until the next frame, while distances read live positions.
	// A team-cache refresh must not silently move this entry from cell (3,3) to (8,8).
	CHECK(finder.findClosestEnemy(*seeker, 50, 0, CMD_FROM_AI) == nullptr);
	CHECK(legacyClosest(finder, *seeker, *seeker->getPosition(), 50, 8, 0) == nullptr);
	w.logic->runLogicFrame();
	CHECK(finder.findClosestEnemy(*seeker, 50, 0, CMD_FROM_AI) == other);
}

TEST_CASE("large army targets: creation, death, containment and reset preserve public radius results and enemy choices")
{
	combattest::CombatWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *seeker = w.unit("Slasher", 'A', 200, 200);
	auto &finder = w.combat().targets();
	REQUIRE(finder.findClosestEnemy(*seeker, 100, 0, CMD_FROM_AI) == nullptr);
	Object *enemy = w.unit("Dummy", 'B', 220, 200);
	REQUIRE(finder.findClosestEnemy(*seeker, 100, 0, CMD_FROM_AI) == enemy);
	std::vector<Object *> before;
	finder.collectInRadius({200, 200, 0}, 100, before);
	CHECK(before == std::vector<Object *>{seeker, enemy});
	w.logic->friend_containLeaveWorld(*enemy);
	CHECK(finder.findClosestEnemy(*seeker, 100, 0, CMD_FROM_AI) == nullptr);
	w.logic->friend_containEnterWorld(*enemy);
	CHECK(finder.findClosestEnemy(*seeker, 100, 0, CMD_FROM_AI) == enemy);
	finder.reset();
	CHECK(finder.findClosestEnemy(*seeker, 100, 0, CMD_FROM_AI) == enemy);
	std::vector<Object *> after;
	finder.collectInRadius({200, 200, 0}, 100, after);
	CHECK(after == before);
	w.logic->destroyObject(enemy);
	CHECK(finder.findClosestEnemy(*seeker, 100, 0, CMD_FROM_AI) == nullptr);
}

TEST_CASE("BFME2 building filters use scan flags and player type, not capturable flags")
{
	combattest::CombatWorld w(
		"ChildObject CaptureBuilding Dummy\n KindOf = STRUCTURE CAPTURABLE\nEnd\n"
		"ChildObject LinkedBuilding Dummy\n KindOf = STRUCTURE LINKED_TO_FLAG\nEnd\n"
		"ChildObject DefenseBuilding Dummy\n KindOf = STRUCTURE FS_BASE_DEFENSE\nEnd\n"
		"ChildObject ArmedContainer Slasher\n KindOf = STRUCTURE CAN_ATTACK\n"
		" Behavior = GarrisonContain ModuleTag_Contain\n ContainMax = 4\n End\nEnd\n"
		"ChildObject MineBuilding Dummy\n KindOf = STRUCTURE MINE\nEnd\n");
	w.combat().setAutoAcquireEnabled(false);
	auto *unit = w.unit("Slasher", 'A', 200, 200);
	auto *capture = w.unit("CaptureBuilding", 'B', 220, 200);
	auto *linked = w.unit("LinkedBuilding", 'B', 240, 200);
	auto *defense = w.unit("DefenseBuilding", 'B', 260, 200);
	auto &finder = w.combat().targets();
	w.playerOf('A')->setPlayerType(PLAYER_HUMAN, true);
	CHECK(finder.findClosestEnemy(*unit, 200, 0, CMD_FROM_AI) == defense);
	CHECK(finder.findClosestEnemy(*unit, 200, TargetFinder::ALLOW_STRUCTURES, CMD_FROM_AI) == capture);
	CHECK_FALSE(CombatQueries::allowsBuildingTarget(*unit, *linked));
	auto *armed = w.unit("ArmedContainer", 'B', 280, 200);
	auto *mine = w.unit("MineBuilding", 'B', 300, 200);
	CHECK(CombatQueries::allowsBuildingTarget(*unit, *armed));
	CHECK(CombatQueries::allowsBuildingTarget(*unit, *mine));
	w.playerOf('A')->setPlayerType(PLAYER_COMPUTER, true);
	CHECK(finder.findClosestEnemy(*unit, 200, 0, CMD_FROM_AI) == capture);
	CHECK(CombatQueries::allowsBuildingTarget(*unit, *linked));
	CHECK(unit->getWeapons()->canAttackObject(*capture, CMD_FROM_PLAYER, false));
}

TEST_CASE("BFME2 insignificant building filter keeps occupied garrisons and faction structures")
{
	combattest::CombatWorld w(
		"ChildObject EmptyInn Dummy\n KindOf = STRUCTURE CAPTURABLE\n RemoveModule ModuleTag_AI\n"
		" Behavior = GarrisonContain ModuleTag_Contain\n ContainMax = 4\n End\nEnd\n"
		"ChildObject FactionInn EmptyInn\n KindOf = STRUCTURE CAPTURABLE FS_FACTORY\nEnd\n"
		"ChildObject ActiveInn EmptyInn\n Behavior = AIUpdateInterface ModuleTag_AI\n End\nEnd\n"
		"ChildObject BareBuilding Dummy\n KindOf = STRUCTURE LINKED_TO_FLAG\n RemoveModule ModuleTag_AI\nEnd\n");
	w.combat().setAutoAcquireEnabled(false);
	auto *unit = w.unit("Slasher", 'A', 200, 200);
	auto *inn = w.unit("EmptyInn", 'B', 220, 200);
	auto *faction = w.unit("FactionInn", 'B', 240, 200);
	auto *active = w.unit("ActiveInn", 'B', 260, 200);
	auto *bare = w.unit("BareBuilding", 'B', 280, 200);
	REQUIRE(inn->getContain());
	CHECK(inn->getAIUpdateInterface() == nullptr);
	CHECK_FALSE(CombatQueries::isSignificantBuildingTarget(*inn));
	CHECK(CombatQueries::isSignificantBuildingTarget(*faction));
	CHECK(CombatQueries::isSignificantBuildingTarget(*active));
	CHECK(CombatQueries::isSignificantBuildingTarget(*bare));
	auto &finder = w.combat().targets();
	CHECK(finder.findClosestEnemy(*unit, 200, TargetFinder::ALLOW_STRUCTURES, CMD_FROM_AI) == inn);
	CHECK(unit->getAIUpdateInterface()->getNextMoodTarget(false, false) == nullptr); // Acquisition is disabled in this fixture.
	w.logic->settings().attackIgnoreInsignificantBuildings = true;
	const unsigned filtered = TargetFinder::ALLOW_STRUCTURES | TargetFinder::IGNORE_INSIGNIFICANT_BUILDINGS;
	for (auto owner : {PLAYER_HUMAN, PLAYER_COMPUTER})
	{
		w.playerOf('A')->setPlayerType(owner, true);
		CHECK(finder.findClosestEnemy(*unit, 200, filtered, CMD_FROM_AI) == faction);
		CHECK(unit->getAIUpdateInterface()->getNextMoodTarget(false, false) == nullptr);
	}
	auto *rider = w.unit("Slasher", 'B', 300, 200);
	REQUIRE(inn->getContain()->addToContain(rider));
	CHECK(CombatQueries::isSignificantBuildingTarget(*inn));
	CHECK(finder.findClosestEnemy(*unit, 200, filtered, CMD_FROM_AI) == inn);
}
