// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Lane PHYS-1: Pathfinder::findAttackPath, the search for a cell from which the attacker's weapon reaches its victim (ZH AIPathfind.cpp findAttackPath; RotWK body
// RW 0x6FC18E, pathfinder vtable 0xC1B8D8 slot +8, the ship variant RW 0x6FDE38 at +0xC is not ported).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; review r2 of lane PHYS-1):
//   1. without the path-through flag: a short forward probe. dir = normalize2(target - position) * 10; for i = 1 .. 9 the point position + i * dir * 0.5: its cell
//      (RW 0x6E8CE6, centred) must be inside the logical extent for a human player, a valid movement cell (RW 0x6E8200) and a free footprint (RW 0x6F3082:
//      checkDestination with no other unit's goal in it); the first point from which the weapon reaches with an extra 10 (RW 0x6CC07C(obj, point, victim, pos, 10, 1))
//      and the view is not blocked (RW 0x6F4557) ends a two-node path (position -> point);
//   2. the corridor: the victim's position moved toward the attacker by the victim's bounding circle + 10 (unless RW 0x441B59) is the goal of the hierarchical block
//      search RW 0x6F9829 (findClosestHierarchicalPath, as findPath's), the block flags cleared first and every block passable when it finds none (RW 0x6FC4B1 ..
//      0x6FC4F9);
//   3. the A* search: the start cell is the position (+5, +5 for a centred footprint), the goal cell the victim's own; the search pops cells in heap order: with a
//      corridor, a cell further along it (RW 0x93AD93) than the closest so far, or as far and nearer the goal cell (squared, binary32), becomes the closest (the
//      first bar: position 0 and 0.9 times the start's squared distance, RW 0xBDE198); a cell from which the weapon reaches (RW 0x6CC07C, extra 0) and whose
//      footprint checkDestination accepts (RW 0x6F1584, its occupancy count) is an endpoint unless it lies within 5 (25 squared) of the start; an endpoint with a
//      count below 2 ends the search at once, else the lowest count is kept; every popped cell is expanded (RW 0x6F9850, given the corridor) while fewer than
//      MaxCellsFindAttackPath (GameData +0x1214) cells were examined; with no endpoint the closest cell is used and `fallback` is set; without a corridor there is no
//      closest cell and no endpoint means no path (the mover then takes an ordinary path, RW 0x668F2F);
//      OpenBFME correction (kAttackCorridor): a position attack takes no corridor and falls back to the cell nearest the goal cell, as before the port;
//   4. the path is built by RW 0x6F1A31 (buildActualPath).
// INFERENCE (stop S-784): the view test RW 0x6F4557 is not ported (S-325: never blocked); RW 0x441B59's condition on the goal shift and RW 0x68C9AC's KindOf 150 goal
// are not read (the shift is always made); RW 0x6FC7B0's test of the popped record's + 0xC (it skips the endpoint tests) is not read; RW 0x6F9850's range limit
// (attack distance + 30, RW 0x6CA974) is not passed (the port's expansion has none); the occupancy count is the port's checkDestination count (allied goals
// block, S-164); a popped cell is always closed (RW 0x6F6286 is skipped with the path-through flag; the port's records would not return to the pool).

#include "Common/PerformanceCounters.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIPathfindLine.h"
#include "GameLogic/AI/AIPathfindStops.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Locomotor.h"

#include <limits>

Path *Pathfinder::findAttackPath(const PathfindObject *obj, const PathfindLocomotorInfo &loco, const Coord3D *from, const PathfindObject *victim, const Coord3D *victimPos,
	float victimRadius, const PathfindAttackRange &range, bool pathThrough, bool *fallback)
{
	perf::Scope performanceScope(perf::Pathing);
	if (fallback)
	{
		*fallback = false;
	}
	if (!m_isMapReady || obj == nullptr)
	{
		return nullptr;
	}
	int radius = 0;
	bool center = false;
	getRadiusAndCenter(obj, radius, center);
	const PathfindMovement mv = makeMovement(obj, loco);
	const bool isHuman = !obj->isComputerControlled();
	const unsigned water = LOCOMOTORSURFACE_WATER | LOCOMOTORSURFACE_DEEP_WATER;
	const bool waterOnly = (loco.validSurfaces & water) && !(loco.validSurfaces & ~water);
	// Requested shoreline fix, rather than a port of the retail ship search
	// (S-784). A water cell may have only one submerged corner.
	const auto waterPositionAllowed = [&](const Coord3D &p) {
		return !waterOnly || (m_terrain && m_terrain->isUnderwater(p.x, p.y, nullptr, nullptr));
	};
	const Coord3D pos = *from;
	const Coord3D target = victim ? victim->getPosition() : *victimPos;
	// 1. the forward probe
	if (!pathThrough)
	{
		float dx = SimMath::subf32(target.x, pos.x), dy = SimMath::subf32(target.y, pos.y);
		const float len = SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares2(dx, dy)));
		if (len != 0.0f)
		{
			const float inv = SimMath::divf32(1.0f, len);
			dx = SimMath::mulf32(dx, inv);
			dy = SimMath::mulf32(dy, inv);
		}
		dx = SimMath::mulf32(dx, 10.0f);
		dy = SimMath::mulf32(dy, 10.0f);
		for (int i = 1; i <= 9; ++i)
		{
			const float fi = SimMath::sseFromInt32(i);
			const Coord3D p{ SimMath::addf32(SimMath::mulf32(SimMath::mulf32(fi, dx), 0.5f), pos.x), SimMath::addf32(SimMath::mulf32(SimMath::mulf32(fi, dy), 0.5f), pos.y),
				pos.z };
			ICoord2D c;
			worldToCell(&p, true, &c);
			if (isHuman && (c.x < m_logicalExtent.lo.x || c.y < m_logicalExtent.lo.y || c.x > m_logicalExtent.hi.x || c.y > m_logicalExtent.hi.y))
			{
				break;
			}
			const PathfindCell *cell = getCell(LAYER_GROUND, c.x, c.y);
			if (cell == nullptr || !validMovementPosition(mv, cell) || !waterPositionAllowed(p))
			{
				break;
			}
			int crowd = 0;
			if (!checkDestination(obj, c.x, c.y, LAYER_GROUND, radius, center, &crowd, false) || crowd != 0)
			{
				break;
			}
			// Shoreline fix: this short attack probe bypasses the normal expansion.
			// A water-classified cell can contain dry land; validate its route too.
			// RW 0x6FC3B5 .. 0x6FC3D2: in reach with an extra 10 and the view from the point not blocked (RW 0x6F4557, the supplied victim and position)
			if (range.inRangeFrom(p, 10.0f) && (!waterOnly || isLinePassable(obj, loco.validSurfaces, obj->getLayer(), pos, p, false)) &&
				!isAttackViewBlockedByObstacle(*obj, p, victim, target))
			{
				Path *path = new Path();
				path->prependNode(&p, LAYER_GROUND);
				path->prependNode(&pos, obj->getLayer());
				path->optimize(*this, obj, loco.validSurfaces, false, nullptr);
				return path;
			}
		}
	}
	// 2. the search
	Coord3D goal = target;
	if (victim)
	{
		float ux = SimMath::subf32(pos.x, target.x), uy = SimMath::subf32(pos.y, target.y);
		const float len = SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares2(ux, uy)));
		if (len != 0.0f)
		{
			const float inv = SimMath::divf32(1.0f, len);
			ux = SimMath::mulf32(ux, inv);
			uy = SimMath::mulf32(uy, inv);
		}
		const float k = SimMath::addf32(victimRadius, 10.0f);
		goal.y = SimMath::addf32(SimMath::mulf32(uy, k), goal.y); // the binary's order: y, x (the z term is 0)
		goal.x = SimMath::addf32(SimMath::mulf32(k, ux), goal.x);
	}
	Coord3D start = pos;
	// Shoreline fix: the generic attack search nudges its start by half a
	// cell. For a water-only mover that can seed a dry neighbouring cell
	// and manufacture a short path ashore before any expansion is checked.
	if (center && !waterOnly)
	{
		start.x = SimMath::addf32(start.x, 5.0f);
		start.y = SimMath::addf32(start.y, 5.0f);
	}
	ICoord2D startNdx;
	worldToCell(&start, true, &startNdx);
	PathfindCell *parentCell = waterOnly ? getCell(obj->getLayer(), startNdx.x, startNdx.y) : getClippedCell(obj->getLayer(), &start);
	if (parentCell == nullptr || !obj->hasAI())
	{
		return nullptr;
	}
	// RW 0x6FC4B1 .. 0x6FC4F9: the block flags cleared (RW 0x937C23), the corridor toward the shifted point (RW 0x6F9829: isHuman, the locomotor, obj, from, the
	// shifted point as goal and adjusted goal), every block passable when there is none (RW 0x937C65). Before it the search flooded every block from the attacker:
	// a long approach, or one round a wall to its gate, spent MaxCellsFindAttackPath near the start and ended at the fallback cell nearest the victim, the near
	// side of the wall, where the attack stalled out of range.
	// OpenBFME correction (deviation, kAttackCorridor): only an attack on an object takes the corridor. A position attack (an archer horde's bombard at a point
	// inside its minimum range) keeps the search without it: the corridor's estimate drives the search along the route toward the point, whose cells are out of
	// reach, and the horde walked aside before firing
	bool hierarchical = false;
	if (victim)
	{
		m_zoneManager.clearPassableFlags();
		Coord3D adjustedGoal = goal;
		hierarchical = findClosestHierarchicalPath(obj, loco, isHuman, from, &goal, &adjustedGoal);
	}
	else
	{
		note(pathstops::kAttackCorridor);
	}
	if (!hierarchical)
	{
		m_zoneManager.setAllPassable();
	}
	// review r3: the goal cell is the target's own position (RW 0x6FC661 .. 0x6FC68B: the target object's + 0x38, or the supplied position)
	ICoord2D goalNdx;
	Coord3D goalPos = target;
	worldToCell(&goalPos, true, &goalNdx);
	PathfindCell *goalCell = getCell(LAYER_GROUND, goalNdx.x, goalNdx.y);
	if (goalCell == nullptr)
	{
		return nullptr;
	}
	if (!goalCell->allocateInfo(m_pool, goalNdx))
	{
		return nullptr;
	}
	if (parentCell != goalCell && !parentCell->allocateInfo(m_pool, startNdx))
	{
		goalCell->releaseInfo(m_pool);
		return nullptr;
	}
	m_isTunneling = false;
	parentCell->startPathfind(goalCell);
	m_open.clear();
	m_closedList = nullptr;
	parentCell->setTotalCost(parentCell->costToGoal(goalCell));
	m_open.push(parentCell);
	m_shortcutCounter = 0;
	PathfindCell *best = nullptr, *closest = nullptr;
	int bestCount = 9999999;
	// RW 0x6FC6D5 .. 0x6FC714: the fallback's best corridor position (0: a cell off the corridor, -1, never qualifies) and its distance, 0.9 (RW 0xBDE198) times
	// the start cell's squared distance to the goal cell
	int closestPosition = 0;
	float closestD2 = 0.0f;
	{
		const int sx = (int)goalCell->getXIndex() - (int)parentCell->getXIndex(), sy = (int)goalCell->getYIndex() - (int)parentCell->getYIndex();
		closestD2 = SimMath::mulf32(SimMath::sseFromInt32(sy * sy + sx * sx), 0.9f);
	}
	int positionClosestD2 = std::numeric_limits<int>::max(); // the position attack's fallback: the cell nearest the goal cell (the search before the corridor)
	int examined = 0;
	const bool askView = victim == nullptr || !victim->isSignificantlyAboveTerrain(); // RW 0x6FC719 .. 0x6FC72B (RW 0x70C55D)
	Path *result = nullptr;
	while (!m_open.empty())
	{
		PathfindCell *c = m_open.pop();
		// every popped cell is closed at once (review r3): its record returns to the pool on success, failure and an exhausted budget alike. RW skips the close
		// (RW 0x6F6286) with the path-through flag (S-784)
		markClosed(c);
		// RW 0x6FC757 .. 0x6FC7AB: only with a corridor, a cell further along it (RW 0x93AD93) than the closest so far, or as far and nearer the goal cell
		if (hierarchical)
		{
			const int position = hierarchicalPositionOf(c->getXIndex(), c->getYIndex());
			const int gx = (int)goalCell->getXIndex() - (int)c->getXIndex(), gy = (int)goalCell->getYIndex() - (int)c->getYIndex();
			const float d2 = SimMath::sseFromInt32(gy * gy + gx * gx);
			if (position > closestPosition || (position == closestPosition && closestD2 > d2))
			{
				closest = c;
				closestPosition = position;
				closestD2 = d2;
			}
		}
		else if (!victim)
		{
			const int gx = (int)goalCell->getXIndex() - (int)c->getXIndex(), gy = (int)goalCell->getYIndex() - (int)c->getYIndex();
			const int d2 = gx * gx + gy * gy;
			if (d2 < positionClosestD2)
			{
				positionClosestD2 = d2;
				closest = c;
			}
		}
		Coord3D centre;
		adjustCoordToCell(c->getXIndex(), c->getYIndex(), center, centre, c->getLayer());
		int crowd = 0;
		if (range.inRangeFrom(centre, 0.0f) && checkDestination(obj, c->getXIndex(), c->getYIndex(), c->getLayer(), radius, center, &crowd, false))
		{
			// RW 0x6FC817 .. 0x6FC88E: the view from the cell blocked (RW 0x6F4557, asked unless the victim is significantly above the terrain) or the cell within 5
			// of the start: no endpoint
			const bool viewBlocked = askView && isAttackViewBlockedByObstacle(*obj, centre, victim, target);
			if (c != parentCell)
			{
				const float sx = SimMath::subf32(centre.x, start.x), sy = SimMath::subf32(centre.y, start.y);
				const bool nearStart = SimMath::addf32(SimMath::mulf32(sy, sy), SimMath::mulf32(sx, sx)) < 25.0f;
				if (!nearStart && !viewBlocked)
				{
					if (crowd < 2)
					{
						best = c;
						bestCount = -1;
						break;
					}
					if (crowd < bestCount)
					{
						best = c;
						bestCount = crowd;
					}
				}
			}
		}
		if (examined < m_config.findAttackPathLimit)
		{
			examined += examineNeighboringCells(c, goalCell, loco, isHuman, center, radius, startNdx, obj, hierarchical); // RW 0x6FC8F2: the corridor passed
		}
	}
	PathfindCell *end = best;
	if (end == nullptr && closest != nullptr)
	{
		end = closest;
		if (fallback)
		{
			*fallback = true;
		}
	}
	if (end)
	{
		result = buildActualPath(obj, loco.validSurfaces, from, end, center, false);
	}
	cleanOpenAndClosedLists();
	parentCell->releaseInfo(m_pool);
	if (goalCell != parentCell)
	{
		goalCell->releaseInfo(m_pool);
	}
	return result;
}

// RW 0x6F4557 (ZH Pathfinder::isAttackViewBlockedByObstacle; RotWK body read from the disassembly):
//   1. off (false) unless AIData AttackUsesLineOfSight (+ 0x67), with a victim, for an ATTACK_NEEDS_LINE_OF_SIGHT or CAN_SHOOT_OVER_WALLS attacker;
//   2. the current weapon's terrain line (RW 0x6CC6DD, not for an IMMOBILE attacker) and the layer pair test (a bridge layer on either side, RW 0x680A75) come
//      first: NOT PORTED (the terrain line test is not ported, every object is on the ground layer: S-161); see pathstops::kAttackView;
//   3. the cell line (RW 0x6F44BD -> the walk RW 0x6F334C, centred cells) from the attacker's position to the victim's position moved 5 (RW 0xBDAE58) toward the
//      attacker (normalize3), on the victim's layer; the first 3 cells are skipped only for an attacker on a bridge layer (RW 0x76550F), never here;
//   4. the callback RW 0x6F2D31 on each cell: only an obstacle cell (type 4) can block; not the attacker's, the victim's, the attacker's container's or its
//      container's, a transparent one (cell record + 0x2C bit 2, RW 0x6E80DF) or the obstacle of the victim's own cell (RW 0x5E2EF2); an obstacle object that is
//      gone blocks; a wall (KindOf WALK_ON_TOP_OF_WALL, WALL_SEGMENT, WALL_GATE or WALL_UPGRADE) blocks unless the attacker or the victim is off the ground layer,
//      or the attacker CAN_SHOOT_OVER_WALLS and the cell is nearer the attacker than the victim (squared, from the cell's corner x * 10, y * 10); any other
//      obstacle blocks unless the attacker CAN_SHOOT_OVER_WALLS without ATTACK_NEEDS_LINE_OF_SIGHT.
// INFERENCE (kAttackView): the obstacle ids RW 0x6E82C8 takes from the victim's and the attacker's modules (the first module answering vslot 0x68) are not read
bool Pathfinder::isAttackViewBlockedByObstacle(const PathfindObject &attacker, const Coord3D &attackerPos, const PathfindObject *victim, const Coord3D &victimPos)
{
	if (!m_config.attackUsesLineOfSight || victim == nullptr)
	{
		return false; // RW 0x6F455D .. 0x6F457C
	}
	const bool overWalls = attacker.isKindOf(PK_CAN_SHOOT_OVER_WALLS);
	const bool needsLos = attacker.isKindOf(PK_ATTACK_NEEDS_LINE_OF_SIGHT);
	if (!needsLos && !overWalls)
	{
		return false; // RW 0x6F4582 .. 0x6F4598
	}
	note(pathstops::kAttackView);
	ICoord2D victimNdx;
	{
		Coord3D v = victimPos;
		worldToCell(&v, true, &victimNdx);
	}
	const PathfindCell *victimCell = getCell(victim->getLayer(), victimNdx.x, victimNdx.y);
	const PathfindObjectID victimCellObstacle = victimCell ? victimCell->getObstacleID() : PATHFIND_INVALID_ID;
	float dx = SimMath::subf32(attackerPos.x, victimPos.x), dy = SimMath::subf32(attackerPos.y, victimPos.y), dz = SimMath::subf32(attackerPos.z, victimPos.z);
	const float len = SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares3(dx, dy, dz)));
	if (len != 0.0f)
	{
		const float inv = SimMath::divf32(1.0f, len);
		dx = SimMath::mulf32(dx, inv);
		dy = SimMath::mulf32(dy, inv);
		dz = SimMath::mulf32(dz, inv);
	}
	Coord3D end{ SimMath::addf32(victimPos.x, SimMath::mulf32(dx, 5.0f)), SimMath::addf32(victimPos.y, SimMath::mulf32(dy, 5.0f)),
		SimMath::addf32(victimPos.z, SimMath::mulf32(dz, 5.0f)) };
	ICoord2D a, b;
	{
		Coord3D s = attackerPos;
		worldToCell(&s, true, &a);
		worldToCell(&end, true, &b);
	}
	const PathfindObject *container = attacker.getContainer();
	const PathfindObject *outer = container ? container->getContainer() : nullptr;
	const bool onGround = attacker.getLayer() == LAYER_GROUND && victim->getLayer() == LAYER_GROUND;
	return walkLine(*this, victim->getLayer(), a, b, [&](PathfindCell *, PathfindCell *to, int x, int y) -> bool {
		if (to->getType() != PathfindCell::CELL_OBSTACLE)
		{
			return false;
		}
		if (to->isObstaclePresent(attacker.getID()) || to->isObstaclePresent(victim->getID()) || (container && to->isObstaclePresent(container->getID())) ||
			(outer && to->isObstaclePresent(outer->getID())) || to->isObstacleTransparent() || to->isObstaclePresent(victimCellObstacle))
		{
			return false;
		}
		const PathfindObject *obstacle = m_world ? m_world->findObjectByID(to->getObstacleID()) : nullptr;
		if (obstacle == nullptr)
		{
			return true; // RW 0x6F2E58
		}
		const bool wall = obstacle->isKindOf(PK_WALK_ON_TOP_OF_WALL) || obstacle->isKindOf(PK_WALL_SEGMENT) || obstacle->isKindOf(PK_WALL_GATE) ||
			obstacle->isKindOf(PK_WALL_UPGRADE);
		if (!wall)
		{
			return !overWalls || needsLos; // RW 0x6F2E96 .. 0x6F2EAB
		}
		if (!onGround)
		{
			return false; // RW 0x6F2EB0 .. 0x6F2ED1
		}
		if (!overWalls)
		{
			return true;
		}
		// RW 0x6F2EDB .. 0x6F2F43: shot over a wall nearer the attacker than the victim (the objects' own positions, + 0x38, not the arguments)
		const float cx = SimMath::sseFromInt32(x * 10), cy = SimMath::sseFromInt32(y * 10);
		const Coord3D &ap = attacker.getPosition(), &vp = victim->getPosition();
		const float ax = SimMath::subf32(ap.x, cx), ay = SimMath::subf32(ap.y, cy);
		const float vx = SimMath::subf32(vp.x, cx), vy = SimMath::subf32(vp.y, cy);
		const float toAttacker = SimMath::addf32(SimMath::mulf32(ay, ay), SimMath::mulf32(ax, ax));
		const float toVictim = SimMath::addf32(SimMath::mulf32(vy, vy), SimMath::mulf32(vx, vx));
		return !(toVictim > toAttacker);
	});
}
