// OpenBFME unit tests (lane PHYS-1, review r3): the pathfinder configuration the attack path, the allied clearing and the melee approach read enters the state hash, and the
// attack and move-away searches give every cell-info record back to the pool (successful, failed and budget-exhausted searches, repeated). Synthetic map, no retail data.
#include "doctest.h"

#include "PathfindTestUtil.h"

#include "Common/StateHash.h"

using namespace pathtest;

namespace
{
std::uint32_t configHash(const PathfindConfig &c)
{
	TestWorld world;
	Pathfinder pf(c, &world);
	StateHasher h;
	pf.crc(h);
	return h.value();
}

// a weapon test answering a fixed reach: `reach` around `centre` (extra enters squared like RW 0x6CC07C), or never
class FixedRange : public PathfindAttackRange
{
public:
	FixedRange(Coord3D centre, float reach) : m_centre(centre), m_reach(reach) {}
	bool inRangeFrom(const Coord3D &from, float extra) const override
	{
		if (m_reach <= 0.0f)
		{
			return false;
		}
		const float dx = from.x - m_centre.x, dy = from.y - m_centre.y;
		return dx * dx + dy * dy + extra * extra <= m_reach * m_reach;
	}
	bool inRangeFromTo(const Coord3D &from, const Coord3D &victimPos, float extra) const override
	{
		if (m_reach <= 0.0f)
		{
			return false;
		}
		const float dx = from.x - victimPos.x, dy = from.y - victimPos.y;
		return dx * dx + dy * dy + extra * extra <= m_reach * m_reach;
	}

private:
	Coord3D m_centre;
	float m_reach;
};
} // namespace

TEST_CASE("phys1 pathfind: MaxCellsFindAttackPath, HordesWaitForHordes and MeleeApproachDist are in the pathfinder's hash (review r3)")
{
	const PathfindConfig base = testConfig();
	const std::uint32_t h0 = configHash(base);
	PathfindConfig c = base;
	c.findAttackPathLimit += 1;
	CHECK(configHash(c) != h0);
	c = base;
	c.hordesWaitForHordes = !c.hordesWaitForHordes;
	CHECK(configHash(c) != h0);
	c = base;
	c.meleeApproachDist += 1.0f;
	CHECK(configHash(c) != h0);
	c = base;
	c.adjustToMeleeLimit += 1;
	CHECK(configHash(c) != h0);
	c = base;
	c.findMeleeEngagementLimit += 1;
	CHECK(configHash(c) != h0);
	c = base;
	c.meleeApproachTolerance += 1.0f;
	CHECK(configHash(c) != h0);
	c = base;
	c.castleSiegeStandBackDistance += 1.0f; // round 6: RW 0x7463E8's margin
	CHECK(configHash(c) != h0);
	CHECK(configHash(base) == h0);
}

TEST_CASE("phys1 pathfind: findAttackPath and getMoveAwayFromPath give every cell record back (budget exhausted, failed, successful, repeated)")
{
	SyntheticTerrain terrain(20, 20);
	TestWorld world;
	PathfindConfig cfg = testConfig();
	TestObject unit;
	unit.id = 3;
	unit.pos = Coord3D{ 55.0f, 55.0f, 0.0f };
	unit.mobile = true; // hasAI
	world.objects[unit.id] = &unit;
	const Coord3D target{ 155.0f, 155.0f, 0.0f };
	auto freeRecords = [](Pathfinder &pf) { return pf.pool().freeCount(); };
	for (int limit : { 1, 2500 })
	{
		cfg.findAttackPathLimit = limit;
		Pathfinder pf(cfg, &world);
		pf.newMap(terrain);
		const int before = freeRecords(pf);
		for (int round = 0; round < 3; ++round)
		{
			bool fallback = false;
			// never in reach: the budget runs out (limit 1) or the whole map is searched; the closest cell is the fallback
			Path *p = pf.findAttackPath(&unit, groundLoco(), &unit.pos, nullptr, &target, 0.0f, FixedRange(target, 0.0f), false, &fallback);
			delete p;
			CHECK(freeRecords(pf) == before);
			// a reach of 30 around the target: a successful endpoint
			p = pf.findAttackPath(&unit, groundLoco(), &unit.pos, nullptr, &target, 0.0f, FixedRange(target, 30.0f), true, &fallback);
			CHECK(p != nullptr);
			delete p;
			CHECK(freeRecords(pf) == before);
			// a successful move-away search
			p = pf.getMoveAwayFromPath(&unit, groundLoco(), nullptr, nullptr, nullptr, nullptr);
			CHECK(p != nullptr);
			delete p;
			CHECK(freeRecords(pf) == before);
		}
	}
}

// RW 0x6EEBC1 (Pathfinder::adjustToMeleeDestination) on an open 30 x 30 map, a one-cell unit at (55, 155) and the destination (205.3, 155.7)
TEST_CASE("phys1 pathfind: the melee destination check keeps a free destination, spirals only to closer cells and refuses enemies' positions and others' goals")
{
	SyntheticTerrain terrain(30, 30);
	TestWorld world;
	PathfindConfig cfg = testConfig();
	TestObject unit;
	unit.id = 3;
	unit.pos = Coord3D{ 55.0f, 155.0f, 0.0f };
	unit.mobile = true;
	world.objects[unit.id] = &unit;
	TestObject ally;
	ally.id = 4;
	ally.mobile = true;
	ally.team = 0;
	world.objects[ally.id] = &ally;
	TestObject enemy;
	enemy.id = 5;
	enemy.mobile = true;
	enemy.team = 1;
	world.objects[enemy.id] = &enemy;
	Pathfinder pf(cfg, &world);
	pf.newMap(terrain);
	const Coord3D original{ 205.3f, 155.7f, 0.0f };
	auto dist2 = [&](const Coord3D &c) { return (c.x - unit.pos.x) * (c.x - unit.pos.x) + (c.y - unit.pos.y) * (c.y - unit.pos.y); };
	// a free destination in the unit's zone: true, the coordinate unchanged (not snapped to its cell)
	Coord3D d = original;
	CHECK(pf.adjustToMeleeDestination(unit, groundLoco(), &d));
	CHECK(d.x == original.x);
	CHECK(d.y == original.y);
	// an ally's goal on the destination (RW 0x9344DE: no other unit's goal): the spiral's first acceptable cell, strictly closer to the unit
	ally.pos = original;
	pf.updateGoal(ally, &ally.pos, LAYER_GROUND);
	d = original;
	CHECK(pf.adjustToMeleeDestination(unit, groundLoco(), &d));
	CHECK((d.x != original.x || d.y != original.y));
	CHECK(dist2(d) < dist2(original));
	pf.removeGoal(ally);
	// an enemy standing on the destination (its position, RW 0x6EBE89): refused there too
	enemy.pos = original;
	pf.updatePos(enemy);
	d = original;
	CHECK(pf.adjustToMeleeDestination(unit, groundLoco(), &d));
	CHECK(dist2(d) < dist2(original));
	// an ally standing there is no blocker (only enemies' positions are)
	pf.removePos(enemy);
	ally.pos = original;
	pf.updatePos(ally);
	d = original;
	CHECK(pf.adjustToMeleeDestination(unit, groundLoco(), &d));
	CHECK(d.x == original.x);
	pf.removePos(ally);
	// no budget: a blocked destination is refused
	PathfindConfig none = cfg;
	none.adjustToMeleeLimit = 0;
	Pathfinder pf0(none, &world);
	pf0.newMap(terrain);
	pf0.updateGoal(ally, &ally.pos, LAYER_GROUND);
	d = original;
	CHECK_FALSE(pf0.adjustToMeleeDestination(unit, groundLoco(), &d));
}

// review r5: RW 0x6E8DD5 gives x = -1 off the map and RW 0x6EEBC1 refuses before searching; the port's converter clips, so the overflow is honoured
TEST_CASE("phys1 pathfind: the melee destination check refuses a destination off the map on each side, the coordinate unchanged")
{
	SyntheticTerrain terrain(30, 30);
	TestWorld world;
	TestObject unit;
	unit.id = 3;
	unit.pos = Coord3D{ 155.0f, 155.0f, 0.0f };
	unit.mobile = true;
	world.objects[unit.id] = &unit;
	Pathfinder pf(testConfig(), &world);
	pf.newMap(terrain);
	for (const Coord3D &probe : { Coord3D{ -5.0f, 155.0f, 0.0f }, Coord3D{ 305.0f, 155.0f, 0.0f }, Coord3D{ 155.0f, -5.0f, 0.0f }, Coord3D{ 155.0f, 305.0f, 0.0f } })
	{
		Coord3D d = probe;
		CHECK_FALSE(pf.adjustToMeleeDestination(unit, groundLoco(), &d));
		CHECK(d.x == probe.x);
		CHECK(d.y == probe.y);
	}
	Coord3D inside{ 155.0f, 205.0f, 0.0f };
	CHECK(pf.adjustToMeleeDestination(unit, groundLoco(), &inside));
}

// round 6: RW 0x6F37B3 (Pathfinder::findMeleeEngagementLocation) on an open 30 x 30 map: a one-cell unit at (55, 155) facing +x, a target at (205, 155) with a reach of 15
TEST_CASE("phys1 pathfind: the member engagement destination stays put in reach, else takes the reachable free cell nearest the unit, and refuses with no reach")
{
	SyntheticTerrain terrain(30, 30);
	TestWorld world;
	TestObject unit;
	unit.id = 3;
	unit.pos = Coord3D{ 55.0f, 155.0f, 0.0f };
	unit.mobile = true;
	world.objects[unit.id] = &unit;
	TestObject target;
	target.id = 5;
	target.mobile = true;
	target.team = 1;
	target.pos = Coord3D{ 205.0f, 155.0f, 0.0f };
	world.objects[target.id] = &target;
	TestObject ally;
	ally.id = 4;
	ally.mobile = true;
	world.objects[ally.id] = &ally;
	Pathfinder pf(testConfig(), &world);
	pf.newMap(terrain);
	const FixedRange reach(target.pos, 15.0f);
	auto d2 = [](const Coord3D &a, const Coord3D &b) { return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y); };
	// far: the destination is a cell from which the target is in reach, on the unit's side
	Coord3D dest = target.pos;
	REQUIRE(pf.findMeleeEngagementLocation(unit, groundLoco(), target, reach, &dest));
	CHECK(d2(dest, target.pos) <= 15.0f * 15.0f);
	CHECK(dest.x < target.pos.x);
	const Coord3D first = dest;
	// deterministic: the same answer again
	Coord3D again = target.pos;
	REQUIRE(pf.findMeleeEngagementLocation(unit, groundLoco(), target, reach, &again));
	CHECK(again.x == first.x);
	CHECK(again.y == first.y);
	// another unit's goal on that cell: the next best cell
	ally.pos = first;
	pf.updateGoal(ally, &ally.pos, LAYER_GROUND);
	Coord3D other = target.pos;
	REQUIRE(pf.findMeleeEngagementLocation(unit, groundLoco(), target, reach, &other));
	CHECK((other.x != first.x || other.y != first.y));
	CHECK(d2(other, target.pos) <= 15.0f * 15.0f);
	pf.removeGoal(ally);
	// in reach with a free own footprint (RW 0x6EE6DD): the unit's own position
	unit.pos = Coord3D{ 195.0f, 155.0f, 0.0f };
	CHECK(pf.meleeOwnFootprintFree(unit));
	Coord3D stay = target.pos;
	REQUIRE(pf.findMeleeEngagementLocation(unit, groundLoco(), target, reach, &stay));
	CHECK(stay.x == unit.pos.x);
	CHECK(stay.y == unit.pos.y);
	// another unit's goal under the unit: RW 0x6EE6DD fails and the search runs instead
	ally.pos = unit.pos;
	pf.updateGoal(ally, &ally.pos, LAYER_GROUND);
	CHECK_FALSE(pf.meleeOwnFootprintFree(unit));
	Coord3D moved = target.pos;
	REQUIRE(pf.findMeleeEngagementLocation(unit, groundLoco(), target, reach, &moved));
	CHECK((moved.x != unit.pos.x || moved.y != unit.pos.y));
	pf.removeGoal(ally);
	// no reach anywhere: false
	unit.pos = Coord3D{ 55.0f, 155.0f, 0.0f };
	Coord3D none = target.pos;
	CHECK_FALSE(pf.findMeleeEngagementLocation(unit, groundLoco(), target, FixedRange(target.pos, 0.0f), &none));
}

// round 6: RW 0x7463E8 (Pathfinder::meleeBackOff): an open target area with a path keeps the destination
TEST_CASE("phys1 pathfind: the contact back-off keeps a reachable destination in the open")
{
	SyntheticTerrain terrain(30, 30);
	TestWorld world;
	TestObject unit;
	unit.id = 3;
	unit.pos = Coord3D{ 55.0f, 155.0f, 0.0f };
	unit.mobile = true;
	world.objects[unit.id] = &unit;
	Pathfinder pf(testConfig(), &world);
	pf.newMap(terrain);
	Coord3D dest{ 205.0f, 155.0f, 0.0f };
	CHECK(pf.meleeBackOff(unit, groundLoco(), 20.0f, &dest));
	CHECK(dest.x == 205.0f);
	CHECK(dest.y == 155.0f);
}

TEST_CASE("pathfind donor movement: transient probes check moving enemies as well as allies")
{
	SyntheticTerrain terrain(20, 20);
	TestWorld world;
	TestObject mover, occupant;
	mover.id = 1;
	mover.mobile = true;
	mover.diameter = 10.0f;
	mover.pos = {25.0f, 25.0f, 0.0f};
	occupant.id = 2;
	occupant.mobile = true;
	occupant.diameter = 10.0f;
	occupant.stationary = true;
	occupant.team = 1;
	occupant.pos = {85.0f, 85.0f, 0.0f};
	world.objects = {{mover.id, &mover}, {occupant.id, &occupant}};
	Pathfinder pf(testConfig(), &world);
	pf.newMap(terrain);
	pf.updatePos(occupant);
	// A transient reservation remains until its next updatePos; it is not a parked goal.
	occupant.stationary = false;
	PathfindCheckMovement info;
	info.cell = {8, 8};
	info.flags = 1;
	info.mv = pf.makeMovement(&mover, groundLoco());
	// BFME2 checkCellUnits + RotWK 0x6EB8FF: transient is independent of relationship.
	CHECK(pf.checkForMovement(&mover, info, nullptr));
	info.transient = true;
	CHECK_FALSE(pf.checkForMovement(&mover, info, nullptr));
	const ICoord2D previous{2, 2};
	CHECK_FALSE(pf.checkForMovement(&mover, info, &previous));
	occupant.team = 0;
	info.flags = 2;
	CHECK_FALSE(pf.checkForMovement(&mover, info, nullptr));
	CHECK(info.allyPresent);
	info.ignoreId = occupant.id;
	CHECK(pf.checkForMovement(&mover, info, nullptr));
	pf.removePos(occupant);
}

TEST_CASE("pathfind donor movement: submarines ignore unit occupancy without ignoring terrain")
{
	SyntheticTerrain terrain(20, 20);
	TestWorld world;
	TestObject mover, occupant;
	mover.id = 1;
	mover.mobile = true;
	mover.diameter = 10.0f;
	mover.pos = {25.0f, 25.0f, 0.0f};
	occupant.id = 2;
	occupant.mobile = true;
	occupant.diameter = 10.0f;
	occupant.pos = {85.0f, 85.0f, 0.0f};
	world.objects = {{mover.id, &mover}, {occupant.id, &occupant}};
	Pathfinder pf(testConfig(), &world);
	pf.newMap(terrain);
	pf.updatePos(occupant);
	pf.updateGoal(occupant, &occupant.pos, LAYER_GROUND);
	PathfindCheckMovement info;
	info.cell = {8, 8};
	info.flags = 2;
	info.transient = true;
	info.mv = pf.makeMovement(&mover, groundLoco());
	CHECK_FALSE(pf.checkForMovement(&mover, info, nullptr));
	mover.kinds.insert(PK_SUBMARINE);
	CHECK(pf.checkForMovement(&mover, info, nullptr));
	CHECK_FALSE(info.goalListNonEmpty);
	CHECK_FALSE(info.allyFixed);
	occupant.team = 1;
	info.flags = 0x11;
	CHECK(pf.checkForMovement(&mover, info, nullptr));
	// The unit exception follows terrain checks; an impassable cell still counts.
	pf.getCell(LAYER_GROUND, info.cell.x, info.cell.y)->setType(PathfindCell::CELL_BRIDGE_IMPASSABLE);
	CHECK(pf.checkForMovement(&mover, info, nullptr));
	CHECK(info.terrainPenalty == 1);
	pf.removeGoal(occupant);
	pf.removePos(occupant);
}

TEST_CASE("phys1 pathfind: findAttackPath follows the hierarchical corridor round a wall to its gate (RW 0x6FC4E4)")
{
	// a 128 x 128 cell map split by a cliff wall filling the block column x = 64..79 below y = 112, the top block row open (the gate). The attacker stands
	// just before the wall, its victim just behind it: the pocket before the wall (7168 cells) is larger than MaxCellsFindAttackPath (2500), so a search that
	// floods every block runs out there and falls back to the cell nearest the victim, against the wall
	SyntheticTerrain terrain(128, 128);
	for (int x = 64; x < 80; ++x)
	{
		for (int y = 0; y < 112; ++y)
		{
			terrain.cliff.insert({ x, y });
		}
	}
	TestWorld world;
	const PathfindConfig cfg = testConfig();
	REQUIRE(cfg.findAttackPathLimit == 2500);
	Pathfinder pf(cfg, &world);
	pf.newMap(terrain);
	TestObject unit;
	unit.id = 3;
	unit.pos = Coord3D{ 605.0f, 105.0f, 0.0f }; // cell (60, 10)
	unit.mobile = true;
	world.objects[unit.id] = &unit;
	// the victim object (a position attack takes no corridor: kAttackCorridor)
	TestObject victim;
	victim.id = 4;
	victim.pos = Coord3D{ 845.0f, 105.0f, 0.0f }; // cell (84, 10)
	world.objects[victim.id] = &victim;
	const Coord3D target = victim.pos;
	const FixedRange range(target, 30.0f);
	bool fallback = true;
	std::unique_ptr<Path> p(pf.findAttackPath(&unit, groundLoco(), &unit.pos, &victim, &target, 0.0f, range, false, &fallback));
	REQUIRE(p != nullptr);
	CHECK_FALSE(fallback);
	const Coord3D end = *p->getLastNode()->getPosition();
	CHECK(range.inRangeFrom(end, 0.0f));
	CHECK(end.x > 800.0f); // tolerance: x=800 is the far wall boundary; any reachable endpoint beyond it satisfies the visibility regression.
}

namespace
{
// a 64 x 64 cell map with a WALL_SEGMENT box at x = 300 (half-width 5) from y = 200 to 400, a building to shoot at behind it at (400, 300) and an
// ATTACK_NEEDS_LINE_OF_SIGHT archer in front of it at (200, 300); the objects are the world's (`objects` owns them)
struct WallScene
{
	SyntheticTerrain terrain{ 64, 64 };
	TestWorld world;
	TestObject wall, building, archer;
	std::unique_ptr<Pathfinder> pf;
	explicit WallScene(PathfindConfig cfg = testConfig())
	{
		wall.id = 10;
		wall.pos = Coord3D{ 300.0f, 300.0f, 0.0f };
		wall.structure = true;
		wall.kinds.insert(PK_WALL_SEGMENT);
		wall.geometry.type = PATHFIND_GEOMETRY_BOX;
		wall.geometry.majorRadius = 5.0f;
		wall.geometry.minorRadius = 100.0f;
		building.id = 11;
		building.pos = Coord3D{ 400.0f, 300.0f, 0.0f };
		building.structure = true;
		building.geometry.type = PATHFIND_GEOMETRY_BOX;
		building.geometry.majorRadius = 20.0f;
		building.geometry.minorRadius = 20.0f;
		archer.id = 12;
		archer.pos = Coord3D{ 200.0f, 300.0f, 0.0f };
		archer.mobile = true;
		archer.kinds.insert(PK_ATTACK_NEEDS_LINE_OF_SIGHT);
		archer.geometry.type = PATHFIND_GEOMETRY_SPHERE;
		archer.geometry.majorRadius = 5.0f;
		for (TestObject *o : { &wall, &building, &archer })
		{
			world.objects[o->id] = o;
		}
		pf = std::make_unique<Pathfinder>(cfg, &world);
		pf->newMap(terrain);
		pf->addObjectToPathfindMap(wall);
		pf->addObjectToPathfindMap(building);
	}
	bool blockedFrom(const Coord3D &from) { return pf->isAttackViewBlockedByObstacle(archer, from, &building, building.pos); }
};
} // namespace

TEST_CASE("phys1 pathfind: the attack view test (RW 0x6F4557) is blocked by a wall between an archer and its target, not round the wall's end")
{
	WallScene s;
	CHECK(s.blockedFrom(s.archer.pos));
	CHECK_FALSE(s.blockedFrom(Coord3D{ 250.0f, 600.0f, 0.0f })); // the line passes the wall's end (x 300 at y 500)
	CHECK_FALSE(s.blockedFrom(Coord3D{ 350.0f, 300.0f, 0.0f })); // behind the wall: only the building's own cells, which never block
	CHECK_FALSE(s.pf->isAttackViewBlockedByObstacle(s.archer, s.archer.pos, nullptr, s.building.pos)); // no victim: never blocked
	// an attacker without ATTACK_NEEDS_LINE_OF_SIGHT / CAN_SHOOT_OVER_WALLS is never blocked
	s.archer.kinds.clear();
	CHECK_FALSE(s.blockedFrom(s.archer.pos));
	// CAN_SHOOT_OVER_WALLS alone: over a wall nearer the attacker than the victim (the objects' own positions), not one nearer the victim
	s.archer.kinds.insert(PK_CAN_SHOOT_OVER_WALLS);
	s.archer.pos = Coord3D{ 280.0f, 300.0f, 0.0f };
	CHECK_FALSE(s.blockedFrom(s.archer.pos));
	s.archer.pos = Coord3D{ 100.0f, 300.0f, 0.0f };
	CHECK(s.blockedFrom(s.archer.pos));
	// AIData AttackUsesLineOfSight No: off
	PathfindConfig off = testConfig();
	off.attackUsesLineOfSight = false;
	WallScene o(off);
	CHECK_FALSE(o.blockedFrom(o.archer.pos));
}

TEST_CASE("phys1 pathfind: findAttackPath from behind a wall ends where the view to the target is clear (RW 0x6FC3CB / 0x6FC82A)")
{
	WallScene s;
	const FixedRange range(s.building.pos, 250.0f); // in reach from the archer's own position: only the wall stands in the way
	REQUIRE(range.inRangeFrom(s.archer.pos, 0.0f));
	bool fallback = true;
	std::unique_ptr<Path> p(s.pf->findAttackPath(&s.archer, groundLoco(), &s.archer.pos, &s.building, &s.building.pos, 20.0f, range, false, &fallback));
	REQUIRE(p != nullptr);
	CHECK_FALSE(fallback);
	const Coord3D end = *p->getLastNode()->getPosition();
	CHECK(range.inRangeFrom(end, 0.0f));
	CHECK_FALSE(s.blockedFrom(end));
}
