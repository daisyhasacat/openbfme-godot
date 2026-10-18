// OpenBFME unit tests. GPL-3.0.
// Lane SAVE-4c without the retail game: the five blocks in the registry (T4: their xfer, owner, lists and hash sections), the transfer helpers' container
// layouts (a map and a set in key order, a multimap's equal keys in their order, a damaged count named), and a synthetic game's blocks saved, loaded and
// saved again byte for byte (T2; the game has no map, so no AI world and no shroud: the blocks say so). The real game's tests are test_save4c_retail.cpp.

#include "doctest.h"

#include "LogicTestUtil.h"

#include "Common/GameState.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/AI/AIXfer.h"
#include "GameLogic/System/EmotionSystem.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
const std::vector<std::string> k4c = { "obfme:Partition", "obfme:Emotions", "obfme:AIWorld", "obfme:Shroud", "obfme:SkirmishAI" };
} // namespace

TEST_CASE("save4c registry (T4): the five blocks have their xfer, are SAVE-4c's, sit in retail's lists (the partition, shroud and skirmish AI also in the "
		  "deep logic CRC list) and hold their state hash sections")
{
	logictest::LogicWorld f;
	struct Want
	{
		const char *token;
		unsigned lists;
		std::vector<std::string> sections;
	};
	const std::vector<Want> want = {
		{ "obfme:Partition", 3u, { "partition manager" } },
		{ "obfme:Emotions", 1u, { "emotion system" } },
		{ "obfme:AIWorld", 1u, { "pathfinder and AI movers", "objects" } },
		{ "obfme:Shroud", 3u, { "shroud" } },
		{ "obfme:SkirmishAI", 3u, { "skirmish AI" } },
	};
	for (const Want &w : want)
	{
		INFO(w.token);
		const GameState::Block *b = f.logic->gameState().findBlock(w.token);
		REQUIRE(b != nullptr);
		CHECK(b->snapshot != nullptr);
		CHECK(b->owner == "SAVE-4c");
		CHECK(b->lists == w.lists);
		CHECK(b->sections == w.sections);
	}
	CHECK(f.logic->gameState().missingBlocks(SNAPSHOT_DEEPCRC_LOGICONLY).empty()); // lane SAVE-4e: SAVE-4b and 4d merged
}

TEST_CASE("save4c AIXfer: a map and a set travel in key order, a multimap's equal keys keep their order, and a count beyond the stream is named")
{
	std::map<std::string, int> m{ { "b", 2 }, { "a", 1 }, { "c", -3 } };
	std::multimap<int, std::uint32_t> mm;
	mm.emplace(5, 50u);
	mm.emplace(1, 10u);
	mm.emplace(5, 51u);
	mm.emplace(5, 52u);
	std::set<std::uint32_t> s{ 9, 3, 7 };
	auto kv = [](Xfer &x) {
		return [&x](std::string &k, int &v) {
			x.xferAsciiString(&k);
			x.xferInt(&v);
		};
	};
	XferSave save;
	AIXfer::map(save, m, 5, kv(save));
	AIXfer::multimap(save, mm, 8, [&](int &k, std::uint32_t &v) {
		save.xferInt(&k);
		save.xferUnsignedInt(&v);
	});
	AIXfer::idSet(save, s);
	// the map's first entry is "a" (key order), length-prefixed
	const std::vector<std::uint8_t> &bytes = save.data();
	REQUIRE(bytes.size() > 6); // tolerance: an index precondition (the bytes checked next must exist)
	CHECK(bytes[0] == 3);   // the count
	CHECK(bytes[4] == 1);   // "a"'s length
	CHECK(bytes[5] == 'a');
	XferLoad load(bytes);
	std::map<std::string, int> m2;
	std::multimap<int, std::uint32_t> mm2;
	std::set<std::uint32_t> s2;
	AIXfer::map(load, m2, 5, kv(load));
	AIXfer::multimap(load, mm2, 8, [&](int &k, std::uint32_t &v) {
		load.xferInt(&k);
		load.xferUnsignedInt(&v);
	});
	AIXfer::idSet(load, s2);
	CHECK(m2 == m);
	CHECK(s2 == s);
	std::vector<std::pair<int, std::uint32_t>> order(mm2.begin(), mm2.end());
	CHECK(order == std::vector<std::pair<int, std::uint32_t>>{ { 1, 10u }, { 5, 50u }, { 5, 51u }, { 5, 52u } });
	// a list whose count is beyond the bytes left
	std::vector<std::uint8_t> bad = { 0xFF, 0xFF, 0x00, 0x00, 1, 2 };
	XferLoad l2(bad);
	std::vector<std::uint32_t> ids;
	std::string error;
	try
	{
		AIXfer::ids(l2, ids);
	}
	catch (const XferException &e)
	{
		error = e.what();
	}
	CHECK(!error.empty());
	CHECK(ids.empty());
}

TEST_CASE("save4c T2 on a synthetic game: the five blocks of a game without a map (no AI world, no shroud: saved as absent) load into a second game of "
		  "the same START and save the same bytes")
{
	logictest::LogicWorld a, b;
	std::vector<std::uint8_t> saved;
	a.logic->gameState().saveBlocks(k4c, saved);
	b.logic->gameState().loadBlocks(k4c, saved);
	std::vector<std::uint8_t> again;
	b.logic->gameState().saveBlocks(k4c, again);
	CHECK(again == saved);
	CHECK(b.logic->emotions().scaryOrHeroes() == a.logic->emotions().scaryOrHeroes());
}

TEST_CASE("save4c AIXfer hostile inputs: a map key or a set id saved twice is refused by name and leaves the destination as it was")
{
	XferSave save;
	std::uint32_t n = 2;
	save.xferUnsignedInt(&n);
	for (int i = 0; i < 2; ++i)
	{
		std::uint32_t id = 7;
		save.xferUnsignedInt(&id);
	}
	std::set<std::uint32_t> set{ 1, 2 };
	{
		XferLoad load(save.data());
		std::string error;
		try
		{
			AIXfer::idSet(load, set);
		}
		catch (const XferException &e)
		{
			error = e.what();
		}
		CHECK(error == "a set holds the id 7 twice");
		CHECK(set == std::set<std::uint32_t>{ 1, 2 });
	}
	std::map<std::uint32_t, std::uint32_t> map{ { 4, 5 } };
	{
		XferSave twice;
		std::uint32_t count = 2;
		twice.xferUnsignedInt(&count);
		for (int i = 0; i < 2; ++i)
		{
			std::uint32_t k = 3, v = (std::uint32_t)i;
			twice.xferUnsignedInt(&k);
			twice.xferUnsignedInt(&v);
		}
		XferLoad load(twice.data());
		std::string error;
		try
		{
			AIXfer::map(load, map, 8, [&](std::uint32_t &k, std::uint32_t &v) {
				load.xferUnsignedInt(&k);
				load.xferUnsignedInt(&v);
			});
		}
		catch (const XferException &e)
		{
			error = e.what();
		}
		CHECK(error == "a map holds a key twice");
		CHECK(map == std::map<std::uint32_t, std::uint32_t>{ { 4, 5 } });
	}
}

TEST_CASE("save4c AIXfer caps: a count just below and at the bytes left is read, one above is refused by name before anything is allocated")
{
	auto stream = [](std::uint32_t count, std::uint32_t entries) {
		XferSave save;
		save.xferUnsignedInt(&count);
		for (std::uint32_t i = 0; i < entries; ++i)
		{
			std::uint32_t id = 100 + i;
			save.xferUnsignedInt(&id);
		}
		return save.data();
	};
	auto read = [](const std::vector<std::uint8_t> &bytes, std::vector<std::uint32_t> &out) {
		XferLoad load(bytes);
		try
		{
			AIXfer::ids(load, out);
		}
		catch (const XferException &e)
		{
			return std::string(e.what());
		}
		return std::string();
	};
	std::vector<std::uint32_t> ids;
	CHECK(read(stream(2, 3), ids).empty()); // below: 2 of the 3 entries' bytes
	CHECK(ids == std::vector<std::uint32_t>{ 100, 101 });
	CHECK(read(stream(3, 3), ids).empty()); // at: exactly the bytes left
	CHECK(ids == std::vector<std::uint32_t>{ 100, 101, 102 });
	std::vector<std::uint32_t> kept{ 7 };
	CHECK(read(stream(4, 3), kept) == "a list declares 4 entries of at least 4 bytes, more than the 12 bytes left"); // above
	CHECK(kept == std::vector<std::uint32_t>{ 7 });
}
