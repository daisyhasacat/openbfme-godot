#include "GameLogic/SkirmishAI/AIFortification.h"
// OpenBFME. GPL-3.0.
// See GameLogic/Combat/TargetFinder.h.

#include "GameLogic/Combat/TargetFinder.h"

#include "Common/Thing/ThingTemplate.h"
#include "Common/Team.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <algorithm>

namespace
{
const float kCell = 60.0f;
int cellOf(float v)
{
	return SimMath::floorToInt(SimMath::divf32(v, kCell));
}
} // namespace

void TargetFinder::reset()
{
	m_cells.clear();
	m_builtFrame = 0xFFFFFFFFu;
	m_builtCount = 0;
	m_count = 0;
	m_maxRadius = 0.0f;
}

void TargetFinder::rebuild()
{
	for (auto &entry : m_cells)
	{
		entry.second.objects.clear();
		entry.second.teams.clear();
	}
	m_count = 0;
	m_maxRadius = 0.0f;
	for (Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (!CombatQueries::isAttackable(*o))
		{
			continue;
		}
		const Coord3D &p = *o->getPosition();
		Cell &cell = m_cells[{cellOf(p.x), cellOf(p.y)}];
		cell.objects.push_back(o->getID());
		// the team runs as refreshTeams makes them, from the same objects at the same moment
		const Team *team = o->getTeam();
		if (cell.teams.empty() || cell.teams.back().team != team)
		{
			cell.teams.push_back({team, cell.objects.size()});
		}
		else
		{
			cell.teams.back().end = cell.objects.size();
		}
		m_maxRadius = std::max(m_maxRadius, CombatQueries::boundingCircleRadius(*o));
		++m_count;
	}
	m_builtFrame = m_logic.getFrame();
	m_builtCount = m_logic.getObjectCount();
	m_builtTransitions = m_logic.containWorldTransitions();
	m_builtTeams = m_logic.combat().teamMembershipVersion();
}

void TargetFinder::collectInRadius(const Coord3D &center, float radius, std::vector<Object *> &out)
{
	collectInRadius(center, radius, out, nullptr);
}

void TargetFinder::refreshTeams()
{
	// Reclassify the EXISTING index after captures/team changes. Rebuilding its positions
	// here would change the original frame-snapshot behavior for objects that already moved.
	for (auto &entry : m_cells)
	{
		Cell &cell = entry.second;
		cell.teams.clear();
		for (size_t i = 0; i < cell.objects.size(); ++i)
		{
			const Object *object = m_logic.findObjectByID(cell.objects[i]);
			const Team *team = object ? object->getTeam() : nullptr;
			if (cell.teams.empty() || cell.teams.back().team != team)
			{
				cell.teams.push_back({team, i + 1});
			}
			else
			{
				cell.teams.back().end = i + 1;
			}
		}
	}
	m_builtTeams = m_logic.combat().teamMembershipVersion();
}

void TargetFinder::collectInRadius(const Coord3D &center, float radius, std::vector<Object *> &out, const Object *enemyOf)
{
	// a frame's first query builds the index; objects created later in the same frame make it stale (their count differs), so does an object a contain moved
	// into or out of the world (lane GARRISON-1)
	if (m_builtFrame != m_logic.getFrame() || m_builtCount != m_logic.getObjectCount() || m_builtTransitions != m_logic.containWorldTransitions())
	{
		rebuild();
	}
	if (enemyOf && m_builtTeams != m_logic.combat().teamMembershipVersion())
	{
		refreshTeams();
	}
	const int x0 = cellOf(SimMath::subf32(center.x, radius)), x1 = cellOf(SimMath::addf32(center.x, radius));
	const int y0 = cellOf(SimMath::subf32(center.y, radius)), y1 = cellOf(SimMath::addf32(center.y, radius));
	for (int cx = x0; cx <= x1; ++cx)
	{
		for (int cy = y0; cy <= y1; ++cy)
		{
			auto it = m_cells.find({cx, cy});
			if (it == m_cells.end())
			{
				continue;
			}
			const Cell &cell = it->second;
			auto collect = [&](size_t begin, size_t end) {
				for (size_t i = begin; i < end; ++i)
				{
					const ObjectID id = cell.objects[i];
					Object *o = m_logic.findObjectByID(id);
					if (!o || !CombatQueries::isAlive(*o) || !o->isInWorld())
					{
						continue;
					}
					const float d2 = CombatQueries::centerDistanceSquared2D(*o->getPosition(), center);
					if (d2 <= SimMath::mulf32(radius, radius))
					{
						out.push_back(o);
					}
				}
			};
			if (!enemyOf)
			{
				collect(0, cell.objects.size());
				continue;
			}
			// Object::getRelationship delegates to these same Team::getRelationship
			// calls. Read relationships now, rather than caching diplomacy per frame.
			size_t begin = 0;
			for (const TeamRun &run : cell.teams)
			{
				if (enemyOf->getTeam() && run.team && enemyOf->getTeam()->getRelationship(run.team) == ENEMIES)
				{
					collect(begin, run.end);
				}
				begin = run.end;
			}
		}
	}
}

Object *TargetFinder::findClosestEnemy(const Object &seeker, float range, unsigned flags, CommandSourceType source)
{
	return findClosestEnemyNear(seeker, *seeker.getPosition(), range, flags, source);
}

Object *TargetFinder::findClosestEnemyNear(const Object &seeker, const Coord3D &center, float range, unsigned flags, CommandSourceType source)
{
	const CombatNames::Kind &k = CombatNames::kinds();
	const CombatNames::Status &st = CombatNames::statuses();
	ObjectWeapons *weapons = seeker.getWeapons();
	if (!weapons)
	{
		return nullptr;
	}
	// Range is measured to the target's edge (BFME2 AIClosestEnemySearch.cpp,
	// partition distance mode 1). Build before reading the margin: a fortress or
	// modded structure may be much larger than the old fixed 80-unit allowance.
	if (m_builtFrame != m_logic.getFrame() || m_builtCount != m_logic.getObjectCount() || m_builtTransitions != m_logic.containWorldTransitions())
	{
		rebuild();
	}
	std::vector<Object *> near;
	collectInRadius(center, SimMath::addf32(range, m_maxRadius), near, &seeker);
	Object *best = nullptr;
	float bestD = 0.0f;
	for (Object *o : near)
	{
		if (o == &seeker || seeker.getRelationship(*o) != ENEMIES || !CombatQueries::isAutoAttackTarget(*o))
		{
			continue;
		}
		const bool isMember = o->testStatus((unsigned)st.hordeMember);
		const bool isHorde = o->isKindOf((unsigned)k.horde);
		if ((flags & HORDES_ONLY) && isMember)
		{
			continue;
		}
		if ((flags & MEMBERS_ONLY) && isHorde)
		{
			continue;
		}
		if (o->isKindOf((unsigned)k.structure) && !(flags & ALLOW_STRUCTURES))
		{
			continue;
		}
		if (o->isKindOf((unsigned)k.projectile))
		{
			continue;
		}
		const float edge = SimMath::subf32(SimMath::length2d(SimMath::subf32(o->getPosition()->x, center.x), SimMath::subf32(o->getPosition()->y, center.y)),
										   CombatQueries::boundingCircleRadius(*o));
		if (edge > range)
		{
			continue;
		}
		if (!AIFortification::allowAutoTarget(seeker, *o) || !weapons->canAttackObject(*o, source, false))
		{
			continue;
		}
		if (!best || edge < bestD || (edge == bestD && o->getID() < best->getID()))
		{
			best = o;
			bestD = edge;
		}
	}
	return best;
}
