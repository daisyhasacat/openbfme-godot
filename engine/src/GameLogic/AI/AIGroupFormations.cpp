// OpenBFME. GPL-3.0.
// See GameLogic/AI/AIGroupFormations.h.

#include "GameLogic/AI/AIGroupFormations.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace
{
const int kMaxSquads = 12;      // RW 0x94FCBA: the squads of one formation
const int kMaxRows = 10;        // RW 0x94FA6C
const int kMaxRowUnits = 6;     // the row record: a count and six members
const int kMaxSquadUnits = 36;  // RW 0x94F869
const int kPriorities = 13;     // locomotor FormationPriority 0 .. 12

struct Unit
{
	Object *object = nullptr;
	AIUpdateInterface *ai = nullptr;
	int width = 1, depth = 1; // FormationWidth (+ 0x578), FormationDepth (+ 0x574)
	int priority = 0;         // the current locomotor's FormationPriority (+ 0x78)
	float distance = 0.0f;
	Coord2D offset{};
};

float absf32(float a)
{
	return a < 0.0f ? SimMath::subf32(0.0f, a) : a;
}

// Coord3D::normalize of a planar vector (RW 0x403111 length: the PC24 CRT root; a zero vector stays)
void normalize2d(float &x, float &y)
{
	const float len = SimMath::fstpDword(SimMath::sqrtPC24((double)SimMath::sumSquares3(x, y, 0.0f)));
	if (len != 0.0f)
	{
		const float inv = SimMath::divf32(1.0f, len);
		x = SimMath::mulf32(x, inv);
		y = SimMath::mulf32(y, inv);
	}
}

// RW 0x94F7C6: a world offset turned into the formation's frame (x along start -> end)
void toFormationFrame(const Coord3D &from, const Coord3D &to, Coord2D &offset)
{
	float dx = SimMath::subf32(to.x, from.x), dy = SimMath::subf32(to.y, from.y);
	normalize2d(dx, dy);
	dy = SimMath::subf32(0.0f, dy);
	const float px = SimMath::subf32(0.0f, dy), py = dx;
	const float x = offset.x, y = offset.y;
	offset.x = SimMath::addf32(SimMath::mulf32(dx, x), SimMath::mulf32(px, y));
	offset.y = SimMath::addf32(SimMath::mulf32(dy, x), SimMath::mulf32(py, y));
}

struct Row
{
	int count = 0;
	Unit *units[kMaxRowUnits] = {};
	void push(Unit *u)
	{
		if (count < kMaxRowUnits)
		{
			units[count++] = u;
		}
	}
	// RW 0x94F8C8
	int maxDepth() const
	{
		int best = 0;
		for (int i = 0; i < count; ++i)
		{
			best = units[i]->depth > best ? units[i]->depth : best;
		}
		return best;
	}
	// RW 0x94F8EC
	int sumWidth() const
	{
		int total = 0;
		for (int i = 0; i < count; ++i)
		{
			total += units[i]->width;
		}
		return total;
	}
};

struct Settings
{
	float columnWidth, rowDepth, squadSpacing;
};

struct Squad
{
	int rows = 0, width = 0;
	Row row[kMaxRows];
	int unitCount = 0;
	Unit *units[kMaxSquadUnits] = {};
	int totalHeight = 0;
	bool ready = false;

	// RW 0x94F869
	void add(Unit *u)
	{
		u->ai->setFormationId(0);
		if (unitCount < kMaxSquadUnits)
		{
			units[unitCount++] = u;
		}
		ready = false;
	}
	// RW 0x94FA6C
	void buildRows()
	{
		int acc = 0;
		totalHeight = 0;
		for (int i = 0; i < rows; ++i)
		{
			row[i].count = 0;
		}
		rows = 0;
		for (int k = 0; k < kPriorities && rows < kMaxRows; ++k)
		{
			for (int j = 0; j < unitCount; ++j)
			{
				Unit *u = units[j];
				if (u->priority != k)
				{
					continue;
				}
				acc += u->width;
				if (acc > width)
				{
					if (++rows >= kMaxRows)
					{
						break;
					}
					acc = u->width;
				}
				row[rows].push(u);
				totalHeight = totalHeight < acc ? acc : totalHeight;
			}
			if (k == 0 || k == 3 || k == 6 || k == 9 || k == 12)
			{
				if (acc > 0)
				{
					++rows;
				}
				acc = 0;
			}
		}
		ready = true;
	}
	// RW 0x94FC3F: the loop reads the first row every time (the binary's own)
	Coord2D dimensions(const Settings &s)
	{
		if (!ready)
		{
			buildRows();
		}
		Coord2D d;
		d.y = SimMath::mulf32((float)totalHeight, s.columnWidth);
		int total = 0;
		for (int i = 0; i < rows; ++i)
		{
			total += row[0].maxDepth();
		}
		d.x = SimMath::mulf32((float)total, s.rowDepth);
		return d;
	}
	// RW 0x94FB5F with the row's RW 0x94F90B
	void place(const Coord2D &origin, int formationId, const Settings &s)
	{
		const float halfRow = SimMath::mulf32(s.rowDepth, 0.5f);
		const float height = SimMath::mulf32((float)totalHeight, s.columnWidth);
		int columns = 0;
		rows = rows > kMaxRows ? kMaxRows : rows;
		for (int i = 0; i < rows; ++i)
		{
			Coord2D p = origin;
			p.x = SimMath::subf32(p.x, SimMath::mulf32((float)columns, s.rowDepth));
			p.x = SimMath::addf32(p.x, halfRow);
			p.y = SimMath::addf32(p.y, SimMath::mulf32(SimMath::subf32(height, SimMath::mulf32((float)row[i].sumWidth(), s.columnWidth)), 0.5f));
			int before = 0;
			for (int n = 0; n < row[i].count; ++n)
			{
				Unit *u = row[i].units[n];
				Coord2D slot = p;
				slot.y = SimMath::addf32(slot.y, SimMath::mulf32(SimMath::addf32((float)before, SimMath::mulf32((float)u->width, 0.5f)), s.columnWidth));
				slot.x = SimMath::subf32(slot.x, SimMath::mulf32(SimMath::mulf32((float)u->depth, s.rowDepth), 0.5f));
				u->ai->setFormationId(formationId);
				if (u->priority == 0)
				{
					slot.x = SimMath::mulf32(slot.x, 0.5f);
					slot.y = SimMath::mulf32(slot.y, 0.5f);
				}
				u->offset = slot;
				before += u->width;
			}
			columns += row[i].maxDepth();
		}
	}
};
} // namespace

const char *AIGroupFormations::stopLine()
{
	return "[S-4460] AIGroup::createFormation (RW 0x94FCBA, lane BACKLOG-8) is ported and gives TEAM_FOLLOW_WAYPOINTS' formation (RW 0x771E7D) and the "
		   "formation march (RW 0x7747DE / 0x77348C, S-4462) their offsets; INFERENCE: members at equal distance keep the group's order (RW: STLport's unstable "
		   "sort); the template's FormationWidth / FormationDepth are read through the movement info's float reader (retail parseInt RW 0x42EC5E); NOT "
		   "ported: groupSetPositionInFormation (RW 0x76F466), the FormationAssistant's templates (BFME2 decomp FormationBuild.cpp, BFME2 0x824E77) and "
		   "ArmyPlacer::PlaceObjects (BFME2 0x77FAC6)";
}

std::vector<Object *> AIGroupFormations::validMembers(const std::vector<Object *> &members)
{
	std::vector<Object *> out;
	for (Object *o : members)
	{
		AIUpdateInterface *ai = o ? o->getAIUpdateInterface() : nullptr;
		if (ai && ai->curLocomotor() && (o->getDisabledMask() & (1u << DISABLED_HELD)) == 0 && !ai->isImmobile())
		{
			out.push_back(o);
		}
	}
	return out;
}

void AIGroupFormations::rotateOffset(const Coord3D &position, const Coord3D &goal, Coord2D &offset)
{
	float dx = SimMath::subf32(goal.x, position.x), dy = SimMath::subf32(goal.y, position.y);
	normalize2d(dx, dy);
	const float px = SimMath::subf32(0.0f, dy);
	const float x = offset.x, y = offset.y;
	offset.x = SimMath::addf32(SimMath::mulf32(x, dx), SimMath::mulf32(y, px));
	offset.y = SimMath::addf32(SimMath::mulf32(y, dx), SimMath::mulf32(x, dy));
}

void AIGroupFormations::createFormation(GameLogic &logic, const std::vector<Object *> &members, const Coord3D &start, const Coord3D &end)
{
	if (members.empty())
	{
		return;
	}
	AIWorld &world = members.front()->getAIUpdateInterface()->world();
	logic.noteStop(stopLine());
	const GameLogicSettings &gs = logic.settings();
	const Settings s{ gs.formationColumnWidth, gs.formationRowDepth, gs.formationSquadSpacing };
	// Hostile input (briefs/common.md): every integer the binary multiplies or sums is checked before anything changes. RW computes in 32-bit registers
	// (RW 0x94FDD5 imul FormationWidth * FormationDepth, the area sum, RW 0x94FE4C the division by 6 * 2 * FormationColumns, the row sums of RW 0x94FA6C
	// and 0x94FB5F) and divides by zero for FormationColumns 0; a value whose arithmetic leaves the int32 range, or a column count below 1, makes no
	// formation: the units keep their ids and offsets and the report names the row
	const std::int64_t kInt32Max = 0x7FFFFFFF;
	const std::int64_t kPerUnitLimit = kInt32Max / (kMaxSquadUnits + 4); // a squad's row sums add at most 36 widths (depths: 10 rows)
	if ((std::int64_t)gs.formationColumns < 1 || (std::int64_t)gs.formationColumns * 12 > kInt32Max)
	{
		logic.noteStop("[S-4460] AIData FormationColumns is " + std::to_string(gs.formationColumns) +
			": AIGroup::createFormation (RW 0x94FCBA) divides by 6 * 2 * it (retail crashes on 0, overflows above 178956970); no formation was made");
		return;
	}
	const int squadWidth = gs.formationColumns * 2;
	const int squadCapacity = squadWidth * 6;
	std::int64_t area = 0;
	for (Object *o : members)
	{
		const ObjectMovementInfo &info = world.movementInfo(*o->getTemplate());
		const std::int64_t w = info.formationWidth, d = info.formationDepth;
		const char *bad = (w > kPerUnitLimit || w < -kPerUnitLimit) ? "FormationWidth" : ((d > kPerUnitLimit || d < -kPerUnitLimit) ? "FormationDepth" : nullptr);
		if (!bad)
		{
			const std::int64_t unitArea = w * d;
			area += unitArea < 1 ? 1 : unitArea;
			if (unitArea > kInt32Max || unitArea < -kInt32Max - 1)
			{
				bad = "FormationWidth * FormationDepth";
			}
			else if (area + squadCapacity - 1 > kInt32Max)
			{
				bad = "the summed FormationWidth * FormationDepth";
			}
		}
		if (bad)
		{
			logic.noteStop(std::string("[S-4460] ") + bad + " of " + o->getTemplate()->getName() + " (" + std::to_string(w) + " x " + std::to_string(d) +
				") overflows AIGroup::createFormation's 32-bit arithmetic (RW 0x94FCBA); no formation was made");
			return;
		}
	}
	const int formationId = world.allocFormationId();
	std::vector<Squad> squads(kMaxSquads);
	for (Squad &q : squads)
	{
		q.width = squadWidth < 2 ? 2 : (squadWidth > 6 ? 6 : squadWidth); // RW 0x94FA51
	}
	std::vector<Unit> units(members.size());
	for (size_t i = 0; i < members.size(); ++i)
	{
		Unit &u = units[i];
		u.object = members[i];
		u.ai = u.object->getAIUpdateInterface();
		const ObjectMovementInfo &info = world.movementInfo(*u.object->getTemplate());
		u.width = info.formationWidth;
		u.depth = info.formationDepth;
		u.priority = u.ai->curLocomotor()->getTemplate().m_formationPriority;
		const Coord3D &p = *u.object->getPosition();
		const float dx = SimMath::subf32(p.x, end.x), dy = SimMath::subf32(p.y, end.y);
		u.distance = SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy));
		u.offset = u.ai->formationOffset(); // a member no row takes keeps the offset it had (RW writes the slot on the object)
		u.ai->setFormationId(0);
	}
	// the two SimpleObjectIterator lists hold the same members in the same order
	std::vector<Unit *> ordered;
	for (Unit &u : units)
	{
		ordered.push_back(&u);
	}
	std::stable_sort(ordered.begin(), ordered.end(), [](const Unit *a, const Unit *b) { return a->distance < b->distance; });
	int squadCount = (int)((area + squadCapacity - 1) / squadCapacity);
	squadCount = squadCount > kMaxSquads ? kMaxSquads : squadCount;
	int squadIndex = 0;
	for (int k = 0; k < kPriorities; ++k)
	{
		for (Unit *u : ordered)
		{
			if (u->priority == k)
			{
				squads[squadIndex].add(u);
				if (++squadIndex >= squadCount)
				{
					squadIndex = 0;
				}
			}
		}
	}
	float left = 0.0f, right = 0.0f;
	for (int i = 0; i < squadCount; ++i)
	{
		const Coord2D size = squads[i].dimensions(s);
		Coord2D origin;
		if (i & 1)
		{
			origin.x = right;
			origin.y = SimMath::mulf32(s.squadSpacing, 0.5f);
			right = SimMath::addf32(right, SimMath::addf32(size.x, s.squadSpacing));
		}
		else
		{
			origin.x = left;
			origin.y = SimMath::subf32(SimMath::subf32(0.0f, size.y), SimMath::mulf32(s.squadSpacing, 0.5f));
			left = SimMath::addf32(SimMath::addf32(left, size.x), s.squadSpacing);
		}
		if (squadCount == 1)
		{
			origin.y = SimMath::mulf32(size.y, -0.5f);
		}
		squads[i].place(origin, formationId, s);
	}
	// the anchor: the path start moved along the path by the first member's projection plus half a row
	const Coord3D first = *ordered.front()->object->getPosition();
	float dirX = SimMath::subf32(end.x, start.x), dirY = SimMath::subf32(end.y, start.y);
	normalize2d(dirX, dirY);
	const float half = SimMath::mulf32(s.rowDepth, 0.5f);
	const float along = SimMath::addf32(SimMath::mulf32(SimMath::subf32(first.y, start.y), dirY), SimMath::mulf32(SimMath::subf32(first.x, start.x), dirX));
	Coord3D anchor = start;
	anchor.x = SimMath::addf32(anchor.x, SimMath::addf32(SimMath::mulf32(dirX, along), SimMath::mulf32(dirX, half)));
	anchor.y = SimMath::addf32(anchor.y, SimMath::addf32(SimMath::mulf32(dirY, along), SimMath::mulf32(dirY, half)));
	auto framePosition = [&](const Unit &u) {
		Coord2D p;
		p.x = SimMath::subf32(u.object->getPosition()->x, anchor.x);
		p.y = SimMath::subf32(u.object->getPosition()->y, anchor.y);
		toFormationFrame(start, end, p);
		return p;
	};
	for (Unit *u : ordered)
	{
		int attempts = 6;
		bool swapped;
		do
		{
			swapped = false;
			--attempts;
			const Coord2D slot = u->offset;
			const Coord2D pos = framePosition(*u);
			const float oldY = absf32(SimMath::subf32(slot.y, pos.y)), oldX = absf32(SimMath::subf32(slot.x, pos.x));
			bool past = false;
			for (Unit *other : ordered)
			{
				if (other == u)
				{
					past = true;
					continue;
				}
				if (!past || other->priority != u->priority)
				{
					continue;
				}
				const Coord2D otherSlot = other->offset;
				const Coord2D otherPos = framePosition(*other);
				const float otherY = absf32(SimMath::subf32(otherSlot.y, otherPos.y)), otherX = absf32(SimMath::subf32(otherSlot.x, otherPos.x));
				const float swY = absf32(SimMath::subf32(otherSlot.y, pos.y)), swX = absf32(SimMath::subf32(otherSlot.x, pos.x));
				const float oswY = absf32(SimMath::subf32(slot.y, otherPos.y)), oswX = absf32(SimMath::subf32(slot.x, otherPos.x));
				auto cost = [](float y0, float y1, float x0, float x1) {
					float c = SimMath::addf32(y0, y1);
					c = SimMath::addf32(c, y0);
					c = SimMath::addf32(c, y1);
					c = SimMath::addf32(c, x0);
					c = SimMath::addf32(c, x1);
					return SimMath::addf32(c, SimMath::mulf32(absf32(SimMath::subf32(x0, x1)), 0.25f));
				};
				const float oldCost = cost(oldY, otherY, oldX, otherX);
				const float newCost = cost(swY, oswY, swX, oswX);
				const float gain = SimMath::subf32(SimMath::addf32(oldY, oldX), SimMath::addf32(swY, swX));
				if (gain < 0.0f)
				{
					continue;
				}
				if (newCost < SimMath::addf32(SimMath::mulf32(gain, 0.3f), oldCost))
				{
					std::swap(u->offset, other->offset);
					swapped = true;
					break;
				}
			}
		} while (swapped && attempts > 0);
	}
	// the front member and the shift
	const Unit *front = nullptr;
	float nearestY = 1.0e9f, farthestX = -1.0e9f;
	for (const Unit *u : ordered)
	{
		const Coord2D o = u->offset;
		if (o.y < 0.0f || o.x < farthestX || (o.x == farthestX && o.y > nearestY))
		{
			continue;
		}
		nearestY = o.y;
		farthestX = o.x;
		front = u;
	}
	float shiftY = 0.0f, shiftX = 0.0f;
	if (front)
	{
		if (front->width > 1)
		{
			shiftY = SimMath::subf32(SimMath::mulf32(s.columnWidth, 0.25f), front->offset.y);
			if (front->offset.y == 0.0f)
			{
				shiftY = 0.0f;
			}
			shiftX = SimMath::subf32(SimMath::subf32(0.0f, front->offset.x), SimMath::mulf32(s.rowDepth, 0.25f));
		}
		else
		{
			shiftY = SimMath::subf32(0.0f, front->offset.y);
		}
	}
	for (Unit *u : ordered)
	{
		Coord2D o = u->offset;
		o.y = SimMath::addf32(o.y, shiftY);
		o.x = SimMath::addf32(o.x, shiftX);
		u->ai->setFormationOffset(o);
	}
}
