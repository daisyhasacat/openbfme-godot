// OpenBFME. GPL-3.0.
//
// TaintManager: TheTaintManager, the grid of the terrain's taint and elven wood areas (BfmeTaintManager over TaintManagerImpl). The spell book's Tainted
// Land and Elven Wood paint it, the leadership auras with RequiredConditions TAINT / ELVEN_WOOD read it, and the client draws it on the terrain
// (GameClient's TaintView, the terrain shader's taint mask).
//
// TARGET FACTS (BFME2 1.06 decompile, Open-BFME-2 Code/GameEngine/Source/Common/BfmeGridRasterCircle.cpp, Bfme5SeventySix.cpp, Rva006C0DA0.cpp; the
// RotWK addresses of the callers are SpellBookPowers.h's RW 0xAD4C10 / 0xAD49B0):
//   * the cell (12 bytes, constructor 0x006C0BA0): + 0 the kind byte (0x80 neutral), + 4 the side (0 neutral, 1 above 0x80, 2 below), + 8 the owner (the
//     painting object's id, -1 none);
//   * the paint of a cell (0x006C15E0, amount, absolute, mode): a neutral paint (0x80) with a mode other than -1 clears only a cell that mode owns; the
//     amount is added to the kind unless absolute, clamped to 0 .. 0xFF; a neutral kind gets side 0 and owner -1, any other side 1 / 2 and the mode as owner;
//   * the grid (TaintManagerImpl, 0x30 bytes): + 0 the Region3D, + 0x18 the cell size, + 0x1C its inverse, + 0x20 / + 0x24 the counts, + 0x28 the cells,
//     + 0x2C the client's per-cell visitor (x, y, kind);
//   * SetExtents (region, size): size <= 0 keeps the current one; a region of negative width or height is ignored; _ReallocCells: a width / height
//     below 1 becomes 1; counts = max(1, ceil(extent / size)); each new cell takes the kind of the old cell at floor((i * size + lo - oldLo) * oldInv)
//     (side and owner start fresh);
//   * the circle paint (BfmeTaintManager 0x006C0AB0 -> ChangeTaintLevel 0x006C1580 -> bfmeRasterCircleFC 0x006C1150): r = ceil(radius * inv),
//     centre = floor((p - lo) * inv); nothing for r < 0 or (amount 0, not absolute); the midpoint circle's spans, each row clipped to the grid
//     (bfmeGetCellRange), painted left to right;
//   * the lookups: the side and owner at a world point (0x006C0E40: 0, owner untouched, outside); the kind of a cell (0x006C0E70: 0x80 outside);
//   * the transfer (0x006C0F50): version (1, 1), the region, size, inverse, counts, a checksum word (written 0), then every cell (0x006C1680: version
//     (1, 2), the kind byte, the side's 4 raw bytes, the owner); the light CRC sums each cell's kind and side; a load ends with the visitor over every cell;
//   * the game sizes it at GameData PartitionCellSize (GlobalData + 0xD4) when the logic is made, then over TheTerrainLogic::getExtent at size 0 (kept)
//     when a map loads (GameLogicInit, 0x006C0810 / 0x006C0820).
// The client visitor is not called here: the client reads the cells from the snapshot when revision() moved (GameClient/LogicSnapshot.h TaintView).
// INFERENCE (stop S-6110): the light CRC is the port's StateHasher over each cell's kind, side and owner (retail sums ComputeCRC of the kind and side); the
// float arithmetic is SSE single precision (retail's floor / ceil go through the x87 and the CRT's double floor, the same integers for these ranges).

#pragma once

#include "Common/StateHash.h"

#include <cstdint>
#include <vector>

class Xfer;

class TaintManager
{
public:
	static constexpr int kNeutral = 0x80;

	struct Cell
	{
		std::uint8_t kind = kNeutral; ///< + 0
		std::int32_t side = 0;         ///< + 4: 1 above neutral (elven wood), 2 below (taint), 0 neutral
		std::int32_t owner = -1;       ///< + 8: the painting object's id
		void paint(int amount, bool absolute, int mode); ///< 0x006C15E0
	};

	TaintManager();

	// SetExtents (0x006C0810 / TaintManagerImpl::SetExtents): cellSize <= 0 keeps the current size
	void setExtent(float loX, float loY, float hiX, float hiY, float cellSize = 0.0f);
	// bfmeResetGrid: the grid back to one cell over an empty region, the cell size kept
	void reset();
	// bfmeApplyCircleWorld (0x006C0AB0): the cells of the circle of `radius` around (x, y) painted with (amount, absolute, mode)
	void applyCircleWorld(float x, float y, float radius, int amount, bool absolute, int mode);
	// 0x006C0E40: the side at a world point (0 outside the grid, `owner` then unchanged)
	int sideAt(float x, float y, int *owner) const;
	// 0x006C0E70: the kind of a cell (0x80 outside)
	int kindAt(int cx, int cy) const;

	int width() const { return m_width; }
	int height() const { return m_height; }
	float cellSize() const { return m_cellSize; }
	float originX() const { return m_loX; }
	float originY() const { return m_loY; }
	const std::vector<Cell> &cells() const { return m_cells; }
	// bumped by every paint, resize and load: the client copies the kinds when it moved
	std::uint64_t revision() const { return m_revision; }
	// a cell was painted since the map started (the state hash has the section only then: a game without taint keeps its hash)
	bool anyPainted() const { return m_painted; }

	void xfer(Xfer &x); ///< 0x006C0F50
	void crc(StateHasher &h) const;
	static const char *stopLine(); ///< S-6110

private:
	void reallocCells(float loX, float loY, float hiX, float hiY, float cellSize); ///< _ReallocCells
	void changeTaintLevel(int x, int y, int radius, int amount, bool absolute, int mode); ///< 0x006C1580
	void paintSpan(int firstX, int lastX, int y, int amount, bool absolute, int mode); ///< BfmeRangeUpdaterFC::operator()

	float m_loX = 0.0f, m_loY = 0.0f, m_loZ = 0.0f, m_hiX = 0.0f, m_hiY = 0.0f, m_hiZ = 0.0f; ///< + 0 the Region3D
	float m_cellSize = 1.0f;    ///< + 0x18
	float m_cellSizeInv = 1.0f; ///< + 0x1C
	int m_width = 0, m_height = 0;
	std::vector<Cell> m_cells;
	std::uint64_t m_revision = 0;
	bool m_painted = false;
};
