// OpenBFME. GPL-3.0.
//
// TerrainResourceManager (RotWK GameLogic + 0x170, class name string RW 0xC2BA50 "TerrainResourceManager", vtable RW 0xC2BA40; BFME-new, ZH has no such class),
// lane ECON-1: the map of who may earn from which ground. Every resource building (farm, lumber mill, mine, ...) and every other building
// (a "dead spot": MaxIncome 0, high priority) claims a disc of ground; discs that overlap divide the income. A resource building's income is its
// MaxIncome times the share of its disc it holds (TerrainResourceBehavior reads it back, RW module + 0x2C).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; read function by function, the addresses are on the code):
//   * the grid (RW 0x75BE66 init(Region3D extent, cellSize)): width = ceil((maxX - minX) / cellSize), height likewise over Y, each at least 1 (a region
//     narrower than 1.0 is widened to 1.0 first); cell size = GameData TerrainResourceCellSize (RW GlobalData + 0xD8), 20.0 in retail; the origin is the
//     region's low corner; every cell is 16 bytes {vector of {player index, object id}, ..., state}. The grid is built by GameLogic at a map's start
//     (RW 0x62FDCF) from TerrainLogic::getExtent (RW vslot 0x180), AFTER the pathfinder made its map.
//   * blocked cells (RW 0x75B7BB): for every cell, at the world point ((i + 0.5) * cellSize, (j + 0.5) * cellSize) (no origin offset: the origin is 0 in
//     every map), the pathfinder's ground-layer cell is asked (RW 0x6EAE05); a cell whose pathfinder cell type is WATER (1), CLIFF (2), 5 or DEEP_WATER (7)
//     becomes state 1: it can never be claimed.
//   * cell states (RW cell + 0xC): 0 free; 1 blocked; 2 SHARED (a list of {player index, object id}: provisional claims of buildings still under construction,
//     one per player); 3 OWNED (one entry: a completed building holds it).
//   * claim (RW 0x75BDC2 claim(object, radius, visible, highPriority)): a claimant record {object id, radius, visible, flag = 0} joins the list (the FRONT for a
//     high priority claimant, else the back), then applyClaim(object, radius, mask = highPriority ? 0x7FFFF43 : 0, flag = 0).
//   * applyClaim (RW 0x75BA1D): the object's TerrainResourceBehavior (the first module of that class) is found; cx = floor((x - originX) / cell + 0.5),
//     cy likewise (SSE sub / div / add, then floor, x87 store, fistp); r = ceil(radius / cell) (x87 fdiv at 24 bits); a Bresenham-style filled disc of radius r
//     is visited (RW 0x75B4BA, rows from the middle out); every visited cell counts toward `total` (even outside the grid), the claimable ones that the state
//     machine of RW 0x75C5D2 accepts toward `claimed`; the share claimed / total (x87 fidiv, 24 bits; 0.0 for an empty disc) is stored in the module (+ 0x2C);
//     the largest radius ever claimed is kept (+ 0x18); a high priority claim then re-applies every claimant within (largest + this radius) (RW 0x75B923).
//   * the cell state machine (RW 0x75C02D usable / 0x75C5D2 claim; `flag` = the claimant is complete): free -> SHARED (flag 0) or OWNED (flag 1) with the
//     claimant as only entry; SHARED: flag 1 takes it (OWNED, one entry), flag 0 adds an entry unless that player already has one; OWNED: only a flag 1 claimant
//     of the SAME object may re-take it (the entry is rewritten); a flag 0 claimant and every other object fail. Blocked cells never change.
//   * build complete (RW 0x75BCD9): the object's claimant record gets flag 1 and applyClaim(object, radius, mask 0, flag 1) runs: a finished building turns the
//     shared cells it can reach into owned cells.
//   * unclaim (RW 0x75BE1C, called when the object dies): the record leaves the list, the disc is released (RW 0x75BB8F with the visitor RW 0x75C461: SHARED
//     cells drop the object's entry and become free when no live entry is left (RETAIL QUIRK: the scan stops at the first matching entry, so a cell is freed when
//     that entry is the first live one); OWNED cells held by the object become free), the module's share becomes 0, and every claimant within (largest + radius)
//     is re-applied with its own flag so neighbours can grow into the freed ground.
//   * reset (RW 0x75BD62): the grid and the largest radius are cleared (the claimant list is cleared by the owner of the GameLogic).
// The client half of the class (decal alpha RW 0x75C1A9, observers) does not touch logic state and is not here. The placement preview RW 0x75B561 is
// (previewShare, community PR #37): RW 0x75C02D in its client form (a filter player >= 0) also writes - an OWNED cell without entries becomes free, a cell
// whose owner no longer exists loses its entries and becomes free -; previewShare answers the same without those writes (the cells are hashed state and
// only the local client asks), and the next logic read makes the same change retail's preview would have made.
//
// Determinism: integers, the claimants in a vector in the retail order, the cells in a vector; the floats go through NumericState (x87 at 24 bits, SSE).

#pragma once

#include "Common/StateHash.h"
#include "GameLogic/ObjectTypes.h"

#include <cstdint>
#include <memory>
#include <vector>

class GameLogic;
class Object;

// what the manager needs from the map
class TerrainResourceTerrain
{
public:
	virtual ~TerrainResourceTerrain() = default;
	// RW vslot 0x180 of TerrainLogic (getExtent: low corner x, y; high corner x, y)
	virtual void getExtent(float &loX, float &loY, float &hiX, float &hiY) const = 0;
	// RW 0x6EAE05 on the pathfinder (TheAI + 0x10), ground layer: `found` = a cell exists at the point, `type` = its cell type (the low nibble)
	virtual bool cellTypeAt(float x, float y, int &type) const = 0;
};

class TerrainResourceManager
{
public:
	static constexpr std::uint32_t kHighPriorityMask = 0x7FFFF43u; ///< RW 0x75BB0D: the mask of a high priority claim

	struct Claimant
	{
		ObjectID id = INVALID_ID;
		float radius = 0.0f;
		bool visible = true;   ///< RW node + 0x10: only the decal draw reads it
		bool complete = false; ///< RW node + 0x11
	};
	struct ClaimEntry
	{
		std::int32_t playerIndex = -1;
		ObjectID objectId = INVALID_ID;
	};
	enum CellState
	{
		CELL_FREE = 0,
		CELL_BLOCKED = 1,
		CELL_SHARED = 2,
		CELL_OWNED = 3
	};
	struct Cell
	{
		std::vector<ClaimEntry> claims;
		int state = CELL_FREE;
	};

	explicit TerrainResourceManager(GameLogic &logic);

	// RW 0x75BE66 (see the file comment); false + a reason when the arguments are not a usable grid
	void init(const TerrainResourceTerrain &terrain, float cellSize);
	// RW 0x75BD62: the grid is dropped; the claimants stay (their objects are deleted by the game's reset)
	void reset();
	// drops the claimants too (a new game)
	void clearAll();

	bool hasGrid() const { return !m_cells.empty(); }
	int width() const { return m_width; }
	int height() const { return m_height; }
	float cellSize() const { return m_cellSize; }
	float originX() const { return m_originX; }
	float originY() const { return m_originY; }
	float largestRadius() const { return m_maxRadius; }
	const Cell *cellAt(int x, int y) const { return inGrid(x, y) ? &m_cells[(size_t)y * (size_t)m_width + (size_t)x] : nullptr; }
	size_t blockedCells() const { return m_blockedCount; }
	const std::vector<Claimant> &claimants() const { return m_claimants; }

	// RW 0x75BDC2 / 0x75BE1C / 0x75BCD9
	void claim(Object &obj, float radius, bool visible, bool highPriority);
	void unclaim(Object &obj, float radius);
	void onBuildComplete(Object &obj);

	// the placement preview's share (RW 0x75B561 x, y, radius, highPriority, player; its visitor RW 0xC2BA38 / 0x75C523 counts every disc cell toward the
	// total and the cells RW 0x75C02D calls usable toward the claimed ones, with mask = highPriority ? 0x7FFFF43 : 0, object 0, complete 0 and the filter =
	// `playerIndex`): what PlaceTerrainResourceClaimantFeedback shows for the local player (BFME2 0x4E6295 passes the template's TerrainResourceBehavior
	// Radius and HighPriority). Reads only (previewUsable), so a client may ask it on an idle frame without changing a peer's state hash
	float previewShare(float x, float y, float radius, bool highPriority, int playerIndex) const;

	void crc(StateHasher &h) const;
	// lane SAVE-4d: retail's TerrainResourceManager::DoXfer (RW 0x75C7AE, window (1, 3)) in its layout, then the claimants' complete flags retail does not
	// save (S-4253); see TerrainResourceManager.cpp
	void xfer(class Xfer &x);
	// lane SAVE-4d: the economy block's post-process, after every block loaded (the objects' included): each staged claimant's object exists, or an
	// XferException naming it and the manager unchanged; then the manager takes the state xfer staged
	void finishLoad();

private:
	struct DiscCounts
	{
		int total = 0;
		int claimed = 0;
	};
	struct Disc
	{
		int cx = 0, cy = 0, r = 0;
	};
	bool inGrid(int x, int y) const { return x >= 0 && y >= 0 && x < m_width && y < m_height && !m_cells.empty(); }
	Cell &at(int x, int y) { return m_cells[(size_t)y * (size_t)m_width + (size_t)x]; }
	Disc makeDisc(float x, float y, float radius) const;
	template <class Visitor>
	void forEachInDisc(const Disc &d, Visitor &&visit) const; // RW 0x75B4BA with RW 0x75B3AE
	bool usable(int x, int y, std::uint32_t mask, ObjectID objectId, bool complete);        // RW 0x75C02D with filter -1
	bool previewUsable(int x, int y, int filter) const; // RW 0x75C02D with complete 0, object 0 and a filter player, without its two writes (previewShare)
	void visitClaim(int x, int y, std::uint32_t mask, ObjectID objectId, bool complete, DiscCounts &counts); // RW 0x75C5D2
	void visitRelease(int x, int y, ObjectID objectId);                                      // RW 0x75C461
	void applyClaim(Object &obj, float radius, bool highPriority, bool complete);            // RW 0x75BA1D
	void release(Object &obj, float radius);                                                 // RW 0x75BB8F
	void reapplyNeighbours(const Object &center, float reach);                               // RW 0x75B923
	void classifyBlockedCells(const TerrainResourceTerrain &terrain);                        // RW 0x75B7BB
	Claimant *findClaimant(ObjectID id);

	GameLogic &m_logic;
	std::vector<Claimant> m_claimants; ///< RW + 0x14 list, front = high priority
	std::vector<Cell> m_cells;         ///< RW + 0x40
	int m_width = 0, m_height = 0;     ///< RW + 0x34, + 0x38
	float m_cellSize = 0.0f;           ///< RW + 0x3C
	float m_originX = 0.0f, m_originY = 0.0f, m_originZ = 0.0f; ///< RW + 0x1C .. + 0x24
	float m_extentX = 0.0f, m_extentY = 0.0f, m_extentZ = 0.0f; ///< RW + 0x28 .. + 0x30
	float m_maxRadius = 0.0f;          ///< RW + 0x18
	size_t m_blockedCount = 0;
	struct Pending ///< lane SAVE-4d: a load's state until finishLoad (not state: null between loads)
	{
		std::vector<Claimant> claimants;
		std::vector<Cell> cells;
		float maxRadius = 0.0f;
	};
	std::unique_ptr<Pending> m_pending;
	std::uint32_t m_blockedHash = 0;   ///< the hash of the blocked cell indices (they never change after init)
};
