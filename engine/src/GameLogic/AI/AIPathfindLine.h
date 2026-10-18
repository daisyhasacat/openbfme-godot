// OpenBFME. GPL-3.0.
//
// The pathfinder's Bresenham cell walk (lane BACKLOG-8 r2 moved it out of AIPathfindSearch.cpp so the ground path code shares it). RotWK keeps fifteen
// copies of one walk, each with its own callback (BFME2 decomp PathfinderCellLineWalks.cpp: the copies differ only in the callback they call).

#pragma once

#include "GameLogic/AI/AIPathfind.h"

#include <cstdlib>

// RW Bresenham (0x6F7F7D for the shortcut, 0x6F03B1 for the passability walk): N = max(|dx|, |dy|) + 1 cells, the decision variable
// d = 2 * minor - major, incE = 2 * minor, incNE = 2 * (minor - major); d >= 0 steps both axes. `visit(from, to, x, y)` returns true to
// stop; the walk ends (returning false) at a cell outside the grid. `from` is the previous cell, null at the first.
template <class Grid, class Visit>
inline bool walkLine(Grid &grid, PathfindLayerEnum layer, const ICoord2D &start, const ICoord2D &end, Visit visit)
{
	int dx = std::abs(end.x - start.x);
	int dy = std::abs(end.y - start.y);
	const int sx = end.x >= start.x ? 1 : -1;
	const int sy = end.y >= start.y ? 1 : -1;
	int x = start.x, y = start.y;
	PathfindCell *from = nullptr;
	if (dx >= dy)
	{
		int d = 2 * dy - dx;
		const int incE = 2 * dy, incNE = 2 * (dy - dx);
		for (int k = 0; k <= dx; k++)
		{
			PathfindCell *to = grid.getCell(layer, x, y);
			if (to == nullptr) return false;
			if (visit(from, to, x, y)) return true;
			from = to;
			if (d >= 0)
			{
				x += sx;
				y += sy;
				d += incNE;
			}
			else
			{
				x += sx;
				d += incE;
			}
		}
	}
	else
	{
		int d = 2 * dx - dy;
		const int incE = 2 * dx, incNE = 2 * (dx - dy);
		for (int k = 0; k <= dy; k++)
		{
			PathfindCell *to = grid.getCell(layer, x, y);
			if (to == nullptr) return false;
			if (visit(from, to, x, y)) return true;
			from = to;
			if (d >= 0)
			{
				x += sx;
				y += sy;
				d += incNE;
			}
			else
			{
				y += sy;
				d += incE;
			}
		}
	}
	return false;
}
