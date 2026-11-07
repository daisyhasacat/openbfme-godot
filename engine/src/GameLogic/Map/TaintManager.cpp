// OpenBFME. GPL-3.0. See GameLogic/Map/TaintManager.h.

#include "GameLogic/Map/TaintManager.h"

#include "Common/Xfer.h"

#include <cmath>
#include <string>

namespace
{
// BaseType's REAL_TO_INT_FLOOR / _CEIL as retail spells them (floor / ceil, then fistp of an integral value)
int floorToInt(float v)
{
	return (int)std::floor(v);
}
int ceilToInt(float v)
{
	return (int)std::ceil(v);
}
} // namespace

// 0x006C15E0
void TaintManager::Cell::paint(int amount, bool absolute, int mode)
{
	if (mode != -1 && amount == kNeutral && mode != owner)
	{
		return; // a neutral paint clears only the cells its own mode owns
	}
	if (!absolute)
	{
		amount += kind;
	}
	if (amount < 0)
	{
		amount = 0;
	}
	else if (amount > 0xFF)
	{
		amount = 0xFF;
	}
	kind = (std::uint8_t)amount;
	if (kind == kNeutral)
	{
		mode = -1;
		side = 0;
	}
	else
	{
		side = kind > kNeutral ? 1 : 2;
	}
	owner = mode;
}

TaintManager::TaintManager()
{
	reallocCells(0.0f, 0.0f, 0.0f, 0.0f, 1.0f); // ??0TaintManagerImpl: an empty region, size 1
}

void TaintManager::setExtent(float loX, float loY, float hiX, float hiY, float cellSize)
{
	if (cellSize <= 0.0f)
	{
		cellSize = m_cellSize;
	}
	if (!(hiX - loX < 0.0f) && !(hiY - loY < 0.0f))
	{
		reallocCells(loX, loY, hiX, hiY, cellSize);
	}
}

void TaintManager::reset()
{
	// bfmeResetGrid: Gen_008812D0::bfmeReset (an empty body), then SetExtents over a zero region at size 0 (kept)
	m_cells.clear();
	m_width = m_height = 0;
	setExtent(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	m_painted = false;
}

// _ReallocCells: the new grid takes each cell's kind from the old cell under it
void TaintManager::reallocCells(float loX, float loY, float hiX, float hiY, float cellSize)
{
	if (hiX - loX < 1.0f)
	{
		hiX = loX + 1.0f;
	}
	if (hiY - loY < 1.0f)
	{
		hiY = loY + 1.0f;
	}
	const float inv = 1.0f / cellSize;
	int width = ceilToInt((hiX - loX) * inv);
	if (width < 1)
	{
		width = 1;
	}
	int height = ceilToInt((hiY - loY) * inv);
	if (height < 1)
	{
		height = 1;
	}
	std::vector<Cell> cells((size_t)width * (size_t)height);
	for (int y = 0; y < height; ++y)
	{
		const int oldY = floorToInt(((float)y * cellSize + loY - m_loY) * m_cellSizeInv);
		if (oldY < 0 || oldY >= m_height)
		{
			continue;
		}
		for (int x = 0; x < width; ++x)
		{
			const int oldX = floorToInt(((float)x * cellSize + loX - m_loX) * m_cellSizeInv);
			if (oldX >= 0 && oldX < m_width)
			{
				cells[(size_t)y * (size_t)width + (size_t)x].kind = m_cells[(size_t)oldY * (size_t)m_width + (size_t)oldX].kind;
			}
		}
	}
	m_cells = std::move(cells);
	m_loX = loX;
	m_loY = loY;
	m_loZ = 0.0f;
	m_hiX = hiX;
	m_hiY = hiY;
	m_hiZ = 0.0f;
	m_cellSizeInv = inv;
	m_width = width;
	m_height = height;
	m_cellSize = cellSize;
	++m_revision;
}

// 0x006C0AB0
void TaintManager::applyCircleWorld(float x, float y, float radius, int amount, bool absolute, int mode)
{
	const int cellRadius = ceilToInt(radius * m_cellSizeInv);
	const int cy = floorToInt((y - m_loY) * m_cellSizeInv);
	const int cx = floorToInt((x - m_loX) * m_cellSizeInv);
	changeTaintLevel(cx, cy, cellRadius, amount, absolute, mode);
}

// 0x006C1580 -> bfmeRasterCircleFC 0x006C1150
void TaintManager::changeTaintLevel(int centerX, int centerY, int radius, int amount, bool absolute, int mode)
{
	if (radius < 0)
	{
		return;
	}
	if (amount == 0 && !absolute)
	{
		return;
	}
	int x = 0;
	int y = radius;
	int d = (1 - radius) << 1;
	int firstX = centerX;
	int lastX = centerX;
	for (;;)
	{
		if (d + y > 0)
		{
			if (y == 0 && radius == 1)
			{
				++x;
				++lastX;
				--firstX;
			}
			paintSpan(firstX, lastX, centerY + y, amount, absolute, mode);
			if (y == 0)
			{
				break;
			}
			paintSpan(firstX, lastX, centerY - y, amount, absolute, mode);
			--y;
			d -= ((y << 1) - 1);
		}
		if (x > d)
		{
			++x;
			++lastX;
			--firstX;
			d += ((x << 1) + 1);
		}
	}
	m_painted = true;
	++m_revision;
}

// BfmeRangeUpdaterFC::operator() with bfmeGetCellRange
void TaintManager::paintSpan(int firstX, int lastX, int y, int amount, bool absolute, int mode)
{
	if (lastX < 0 || firstX >= m_width || y < 0 || y >= m_height)
	{
		return;
	}
	Cell *row = m_cells.data() + (size_t)y * (size_t)m_width;
	Cell *first = firstX > 0 ? row + firstX : row;
	Cell *last = row + (lastX < m_width ? lastX + 1 : m_width);
	for (Cell *cell = first; cell < last; ++cell)
	{
		cell->paint(amount, absolute, mode);
	}
}

// 0x006C0E40
int TaintManager::sideAt(float x, float y, int *owner) const
{
	const int cx = floorToInt((x - m_loX) * m_cellSizeInv);
	if (cx < 0 || cx >= m_width)
	{
		return 0;
	}
	const int cy = floorToInt((y - m_loY) * m_cellSizeInv);
	if (cy < 0 || cy >= m_height)
	{
		return 0;
	}
	const Cell &c = m_cells[(size_t)cy * (size_t)m_width + (size_t)cx];
	if (owner)
	{
		*owner = c.owner;
	}
	return c.side;
}

// 0x006C0E70
int TaintManager::kindAt(int cx, int cy) const
{
	if (cx < 0 || cx >= m_width || cy < 0 || cy >= m_height)
	{
		return kNeutral;
	}
	return m_cells[(size_t)cy * (size_t)m_width + (size_t)cx].kind;
}

// 0x006C0F50 with the cell's 0x006C1680
void TaintManager::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	x.xferReal(&m_loX);
	x.xferReal(&m_loY);
	x.xferReal(&m_loZ);
	x.xferReal(&m_hiX);
	x.xferReal(&m_hiY);
	x.xferReal(&m_hiZ);
	x.xferReal(&m_cellSize);
	x.xferReal(&m_cellSizeInv);
	std::int32_t width = m_width, height = m_height;
	x.xferInt(&width);
	x.xferInt(&height);
	std::uint32_t checksum = 0; // retail writes 0 and ignores it on load
	x.xferUnsignedInt(&checksum);
	if (x.isLoading())
	{
		if (width < 1 || height < 1 || (long long)width * (long long)height > (1ll << 24))
		{
			throw XferException("TaintManager: a grid of " + std::to_string(width) + " x " + std::to_string(height) + " cells");
		}
		m_width = width;
		m_height = height;
		m_cells.assign((size_t)width * (size_t)height, Cell());
	}
	for (Cell &c : m_cells)
	{
		XferVersion cv{ 1, 2 };
		x.xferVersion(cv);
		x.xferUnsignedByte(&c.kind);
		x.xferInt(&c.side);
		if (x.isLoading() && (c.side < 0 || c.side > 2))
		{
			throw XferException("TaintManager: a cell side of " + std::to_string(c.side));
		}
		if (cv.current >= 2)
		{
			x.xferInt(&c.owner);
		}
	}
	x.xferBool(&m_painted); // OpenBFME trailer: whether the state hash has the section
	if (x.isLoading())
	{
		++m_revision; // the load's visitor pass (0x006C0DA0): the client redraws every cell
	}
}

void TaintManager::crc(StateHasher &h) const
{
	h.addI32(m_width);
	h.addI32(m_height);
	h.addFloat(m_cellSize);
	for (const Cell &c : m_cells)
	{
		h.addU32(c.kind);
		h.addI32(c.side);
		h.addI32(c.owner);
	}
}

const char *TaintManager::stopLine()
{
	return "[S-6110] TheTaintManager (BFME2 TaintManagerImpl 0x006C0AB0 .. 0x006C1680): the grid, the circle paint, the side / owner lookup and the "
		   "transfer are ported; INFERENCE: its hash is the port's StateHasher over each cell's kind, side and owner (retail's light CRC sums ComputeCRC of "
		   "the kind and the side), and the cell arithmetic is SSE single precision (retail's x87 floor / ceil give the same integers for map extents)";
}
