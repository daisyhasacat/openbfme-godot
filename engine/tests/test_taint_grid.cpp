// OpenBFME. GPL-3.0.
// TheTaintManager (GameLogic/Map/TaintManager.h, S-6110): the BFME2 grid's paint, lookup and transfer.

#include "doctest.h"

#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameClient/LogicSnapshot.h"
#include "GameClient/TaintMask.h"
#include "GameLogic/Map/TaintManager.h"

#include <set>
#include <utility>

namespace
{
// the cells whose kind is not neutral, as (x, y)
std::set<std::pair<int, int>> painted(const TaintManager &t)
{
	std::set<std::pair<int, int>> out;
	for (int y = 0; y < t.height(); ++y)
	{
		for (int x = 0; x < t.width(); ++x)
		{
			if (t.kindAt(x, y) != TaintManager::kNeutral)
			{
				out.insert({ x, y });
			}
		}
	}
	return out;
}

TaintManager grid()
{
	TaintManager t;
	t.setExtent(0.0f, 0.0f, 0.0f, 0.0f, 40.0f); // GameData PartitionCellSize
	t.setExtent(0.0f, 0.0f, 400.0f, 400.0f);    // the map's extent at size 0 (kept)
	return t;
}
} // namespace

TEST_CASE("taint grid: the manager starts as one neutral cell and is sized by the cell size, then the map's extent")
{
	TaintManager fresh;
	CHECK(fresh.width() == 1);
	CHECK(fresh.height() == 1);
	CHECK(fresh.kindAt(0, 0) == 0x80);
	CHECK(fresh.kindAt(-1, 0) == 0x80); // outside: neutral (0x006C0E70)
	const TaintManager t = grid();
	CHECK(t.cellSize() == 40.0f);
	CHECK(t.width() == 10);
	CHECK(t.height() == 10);
	CHECK(painted(t).empty());
	CHECK_FALSE(t.anyPainted());
}

TEST_CASE("taint grid: the midpoint circle (bfmeRasterCircleFC 0x006C1150): radius 1 is a plus, radius 2 is 21 cells, clipped to the grid")
{
	TaintManager t = grid();
	t.applyCircleWorld(200.0f, 200.0f, 40.0f, 0, true, -1); // ceil(40 / 40) = 1 cell around (5, 5)
	CHECK(painted(t) == std::set<std::pair<int, int>>{ { 5, 4 }, { 4, 5 }, { 5, 5 }, { 6, 5 }, { 5, 6 } });
	CHECK(t.anyPainted());
	TaintManager u = grid();
	u.applyCircleWorld(200.0f, 200.0f, 80.0f, 0, true, -1); // ceil(80 / 40) = 2
	std::set<std::pair<int, int>> expect;
	for (int x = 4; x <= 6; ++x)
	{
		expect.insert({ x, 3 });
		expect.insert({ x, 7 });
	}
	for (int y = 4; y <= 6; ++y)
	{
		for (int x = 3; x <= 7; ++x)
		{
			expect.insert({ x, y });
		}
	}
	CHECK(painted(u) == expect);
	CHECK(expect.size() == 21u);
	TaintManager v = grid();
	v.applyCircleWorld(0.0f, 0.0f, 80.0f, 0, true, -1); // the corner: only the cells inside the grid
	CHECK(painted(v) == std::set<std::pair<int, int>>{
		{0, 0}, {1, 0}, {2, 0}, {0, 1}, {1, 1}, {2, 1}, {0, 2}, {1, 2}});
	TaintManager w = grid();
	w.applyCircleWorld(200.0f, 200.0f, 40.0f, 0, false, -1); // amount 0 and not absolute: nothing (0x006C1580)
	CHECK(painted(w).empty());
	CHECK_FALSE(w.anyPainted());
}

TEST_CASE("taint grid: the cell paint (0x006C15E0) and the side / owner lookup (0x006C0E40) as Tainted Land and Elven Wood use them")
{
	TaintManager t = grid();
	// TaintSpecialPower 0x004C49AE: radius * 1.25 neutral for everyone, then the radius at 0 owned by the taint object
	t.applyCircleWorld(200.0f, 200.0f, 50.0f, 0x80, true, -1);
	t.applyCircleWorld(200.0f, 200.0f, 40.0f, 0, true, 7);
	int owner = -5;
	CHECK(t.sideAt(205.0f, 205.0f, &owner) == 2);
	CHECK(owner == 7);
	owner = -5;
	CHECK(t.sideAt(5.0f, 5.0f, &owner) == 0); // a neutral cell
	CHECK(owner == -1);
	owner = -5;
	CHECK(t.sideAt(-10.0f, 5.0f, &owner) == 0); // outside: 0, the owner untouched
	CHECK(owner == -5);
	// a neutral paint by another owner leaves the cell; its own owner clears it
	t.applyCircleWorld(200.0f, 200.0f, 40.0f, 0x80, true, 8);
	CHECK(t.sideAt(205.0f, 205.0f, &owner) == 2);
	t.applyCircleWorld(200.0f, 200.0f, 40.0f, 0x80, true, 7);
	CHECK(t.sideAt(205.0f, 205.0f, &owner) == 0);
	CHECK(owner == -1);
	// ElvenWoodSpecialPower 0x004C3B17: 0xFF owned by the grove; a relative paint is added and clamped
	t.applyCircleWorld(200.0f, 200.0f, 40.0f, 0xFF, true, 9);
	CHECK(t.sideAt(205.0f, 205.0f, &owner) == 1);
	CHECK(owner == 9);
	t.applyCircleWorld(200.0f, 200.0f, 0.0f, 10, false, 9);
	CHECK(t.kindAt(5, 5) == 0xFF);
	t.applyCircleWorld(200.0f, 200.0f, 0.0f, -0x7F, false, 9);
	CHECK(t.kindAt(5, 5) == 0x80);
	CHECK(t.sideAt(205.0f, 205.0f, &owner) == 0);
}

TEST_CASE("taint grid: a new extent keeps each cell's kind under it (_ReallocCells), the revision moves and the transfer (0x006C0F50) round-trips")
{
	TaintManager t = grid();
	t.applyCircleWorld(200.0f, 200.0f, 40.0f, 0, true, 3);
	const auto before = t.revision();
	t.setExtent(0.0f, 0.0f, 800.0f, 400.0f);
	CHECK(t.revision() != before);
	CHECK(t.width() == 20);
	CHECK(t.kindAt(5, 5) == 0);
	CHECK(t.kindAt(15, 5) == 0x80);
	TaintManager a = grid();
	a.applyCircleWorld(120.0f, 280.0f, 80.0f, 0xFF, true, 11);
	XferSave save;
	a.xfer(save);
	TaintManager b;
	XferLoad load(save.data());
	b.xfer(load);
	CHECK(load.atEnd());
	CHECK(b.width() == a.width());
	CHECK(b.height() == a.height());
	CHECK(b.cellSize() == a.cellSize());
	CHECK(b.anyPainted());
	StateHasher ha, hb;
	a.crc(ha);
	b.crc(hb);
	CHECK(ha.value() == hb.value());
	int owner = 0;
	CHECK(b.sideAt(125.0f, 285.0f, &owner) == 1);
	CHECK(owner == 11);
	CHECK(TaintManager::stopLine() == std::string(TaintManager::stopLine()));
}

TEST_CASE("taint view: W3DTaint::setTaintLevel's texel (BFME2 0x00073CC0) from GameData's TaintColor / ElvenWoodColor, and the bordered mask texture")
{
	TaintColors colors;
	colors.on = true;
	const float low[3] = { 120.0f / 255.0f, 90.0f / 255.0f, 80.0f / 255.0f };   // gamedata.ini TaintColor
	const float high[3] = { 70.0f / 255.0f, 120.0f / 255.0f, 50.0f / 255.0f };   // ElvenWoodColor
	for (int k = 0; k < 3; ++k)
	{
		colors.low[k] = low[k];
		colors.high[k] = high[k];
	}
	std::uint8_t t[4];
	TaintMask::texel(3, 1, 0x80, colors, t); // neutral: white, level in alpha
	CHECK(t[0] == 255);
	CHECK(t[1] == 255);
	CHECK(t[2] == 255);
	CHECK(t[3] == 0x80);
	TaintMask::texel(0, 0, 0, colors, t); // full taint at (0, 0): red +0.03 (x % 5, y % 2), green -0.02 (x % 3, y % 5), blue -0.03 (x % 2, y % 3)
	CHECK(t[0] == 127);
	CHECK(t[1] == 84);
	CHECK(t[2] == 72);
	CHECK(t[3] == 0);
	TaintMask::texel(1, 1, 0xFF, colors, t); // full elven wood at (1, 1): red -0.02, green +0.03, blue +0.02
	CHECK(t[0] == (std::uint8_t)(unsigned)((high[0] - 0.02f) * 255.0f));
	CHECK(t[1] == (std::uint8_t)(unsigned)((high[1] + 0.03f) * 255.0f));
	CHECK(t[2] == (std::uint8_t)(unsigned)((high[2] + 0.02f) * 255.0f));
	CHECK(t[3] == 0xFF);
	TaintView view;
	view.countX = 3;
	view.countY = 2;
	view.kinds = { 0x80, 0x80, 0x80, 0x80, 0, 0x80 };
	CHECK(TaintMask::anyPainted(view));
	std::vector<std::uint8_t> rgba;
	int w = 0, h = 0;
	TaintMask::build(view, colors, rgba, w, h);
	REQUIRE(w == 5);
	REQUIRE(h == 4);
	REQUIRE(rgba.size() == 5u * 4u * 4u);
	CHECK(rgba[3] == 0x80);                      // the border is neutral
	CHECK(rgba[((2 * 5) + 2) * 4 + 3] == 0);     // cell (1, 1) at texel (2, 2)
	TaintMask::texel(1, 1, 0, colors, t);
	CHECK(rgba[((2 * 5) + 2) * 4 + 0] == t[0]);
	view.kinds[4] = 0x80;
	CHECK_FALSE(TaintMask::anyPainted(view));
}

TEST_CASE("taint fade: BFME2 W3DTaintInterpolate byte steps and reversal")
{
	std::vector<std::uint8_t> target = {0, 255, 128}, displayed;
	CHECK(TaintMask::interpolate(target, displayed, 0));
	CHECK(displayed == std::vector<std::uint8_t>{128, 128, 128});
	CHECK_FALSE(TaintMask::interpolate(target, displayed, 15));
	CHECK(TaintMask::interpolate(target, displayed, 16));
	CHECK(displayed == std::vector<std::uint8_t>{127, 129, 128});
	CHECK(TaintMask::interpolate(target, displayed, 1000));
	CHECK(displayed == std::vector<std::uint8_t>{63, 193, 128});
	target = {255, 0, 128};
	CHECK(TaintMask::interpolate(target, displayed, 10000));
	CHECK(displayed == std::vector<std::uint8_t>{191, 65, 128});
	CHECK(TaintMask::interpolate(target, displayed, 2000));
	CHECK(displayed == target);
	CHECK_FALSE(TaintMask::interpolate(target, displayed, 16));
}

TEST_CASE("taint shader mask: BFME2 mode above zero keeps texture colour untinted")
{
	TaintColors colors;
	colors.shaderTerrain = true;
	colors.low[0] = 0.2f;
	colors.high[1] = 0.3f;
	for (int level : {0, 64, 128, 192, 255})
	{
		std::uint8_t texel[4];
		TaintMask::texel(0, 0, level, colors, texel);
		CHECK(texel[0] == 255);
		CHECK(texel[1] == 255);
		CHECK(texel[2] == 255);
		CHECK(texel[3] == level);
	}
}
