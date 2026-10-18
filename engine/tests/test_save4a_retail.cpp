// OpenBFME unit tests. GPL-3.0.
// Lane SAVE-4a on the retail game (docs/SAVES.md T4 and T2, and the measurement the brief asks for): a real skirmish's snapshot request is refused naming
// exactly the blocks the later SAVE-4 lanes must port (every section of its state hash is declared); the core blocks of a real game round-trip into a
// second, identical game byte for byte with its real teams' members linked again; the core's size and write time on a 4-player skirmish at frame 3,000.
// SKIP loudly without ROTWK_INSTALL / BFME2_INSTALL.

#include "doctest.h"

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/GameState.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace
{
int templateIndex(const PlayerTemplateStore &store, const std::string &name)
{
	for (int i = 0; i < store.getPlayerTemplateCount(); ++i)
	{
		if (store.getNthPlayerTemplate(i)->getName() == name)
		{
			return i;
		}
	}
	return -1;
}

const std::vector<MapCacheEntry> &mapCache(starttest::Shared &s)
{
	static std::vector<MapCacheEntry> cache;
	if (cache.empty())
	{
		std::string error;
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	return cache;
}

// four Medium AIs (Men, Mordor, Elves, Isengard) on the 8-player Fall Back, each on a team of its own
NewGameMessage fourPlayerMessage(const starttest::Shared &s, std::uint32_t seed)
{
	NewGameMessage m;
	m.game.mapName = "maps/map mp fall back 8p/map mp fall back 8p.map";
	m.game.seed = seed;
	m.game.startingCash = 1500;
	const char *factions[4] = { "FactionMen", "FactionMordor", "FactionElves", "FactionIsengard" };
	for (int i = 0; i < 4; ++i)
	{
		SkirmishGameSlot &sl = m.game.slots[(size_t)i];
		sl.state = SLOT_MED_AI;
		sl.name = u"Computer";
		sl.playerTemplate = templateIndex(s.world->playerTemplates(), factions[i]);
		REQUIRE(sl.playerTemplate != -1);
		sl.startPos = i;
		sl.color = i;
		sl.teamNumber = i;
	}
	return m;
}

struct Game
{
	NewGameStart start{ RandomAlgorithm::ZH_CarryChain };
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;
};

void load(starttest::Shared &s, const NewGameMessage &m, Game &out)
{
	std::string error;
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s.world->playerTemplates(), s.settings, mapCache(s), RandomAlgorithm::ZH_CarryChain, out.start, &error), error);
	out.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	out.assets = std::make_unique<WW3DAssetManager>(*out.source);
	out.game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *out.assets, s.options);
	LiveGame::Options o;
	o.start = &out.start;
	REQUIRE_MESSAGE(out.game->load(o, &error), error);
}

void runTo(Game &g, UnsignedInt frame)
{
	while (g.game->logic().getFrame() < frame)
	{
		REQUIRE(g.game->advance(0.2) == 1);
		g.game->drainFrames();
	}
	REQUIRE(g.game->logic().getFrame() == frame);
}

const std::vector<std::string> kCore = { "obfme:TeamFactory", "obfme:Players", "obfme:GameLogic", "obfme:Random" };

size_t teamMembers(const GameLogic &logic)
{
	size_t n = 0;
	for (const Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		n += o->getTeam() ? 1 : 0;
	}
	return n;
}
} // namespace

TEST_CASE("save4a retail T4: a real 4-player skirmish's snapshot is written: every block has its xfer (SAVE-4b, 4c and 4d merged by SAVE-4e); its every state "
		  "hash section is declared")
{
	OPENBFME_REQUIRE_START(s);
	Game g;
	load(*s, fourPlayerMessage(*s, 4120), g);
	runTo(g, 50);
	GameLogic &logic = g.game->logic();
	const std::vector<std::string> sections = logic.stateHashSectionNames();
	// the real game has the contributors a synthetic one lacks (the pathfinder, the shroud): all declared
	CHECK(std::find(sections.begin(), sections.end(), "pathfinder and AI movers") != sections.end());
	CHECK(std::find(sections.begin(), sections.end(), "shroud") != sections.end());
	CHECK(logic.gameState().undeclaredSections(sections).empty());
	std::vector<std::uint8_t> out;
	std::string error;
	REQUIRE_MESSAGE(logic.saveSnapshot(out, &error), error); // lane SAVE-4e: every block has its xfer (SAVE-4b, 4c, 4d merged)
	CHECK(!out.empty());
}

TEST_CASE("save4a retail T2 and the measurement: a 4-player skirmish at frame 3,000: the core blocks load into a second game run identically (its real "
		  "teams' members linked again by the post-process) without changing its state hash, save the same bytes again, and the core's size and write "
		  "time are reported")
{
	OPENBFME_REQUIRE_START(s);
	const NewGameMessage m = fourPlayerMessage(*s, 4121);
	// lane SAVE-4b (after the archive's S-3808): the second game has a world of its own; its loads and queries run with its world's stores selected
	starttest::Shared *peer = starttest::peerWorld();
	REQUIRE(peer);
	REQUIRE_MESSAGE(peer->world, peer->error);
	Game a, b;
	load(*s, m, a);
	{
		auto peerContext = peer->world->enterContext();
		load(*peer, m, b);
	}
	runTo(a, 3000);
	runTo(b, 3000);
	GameLogic &la = a.game->logic();
	GameLogic &lb = b.game->logic();
	REQUIRE(la.computeStateHash() == lb.computeStateHash());
	const size_t members = teamMembers(lb);
	REQUIRE(members == teamMembers(la));

	std::vector<std::uint8_t> stream;
	la.gameState().saveBlocks(kCore, stream);
	auto peerContext = peer->world->enterContext();
	const std::uint32_t before = lb.computeStateHash();
	lb.gameState().loadBlocks(kCore, stream);
	CHECK(lb.computeStateHash() == before);
	CHECK(teamMembers(lb) == members);
	std::vector<std::uint8_t> again;
	lb.gameState().saveBlocks(kCore, again);
	CHECK(again == stream);

	// the measurement (no threshold: SAVES.md section 4 budgets the whole snapshot, which needs SAVE-4b..d): each core block's size, and the core's write
	// time, the best of 20 writes
	std::string sizes;
	for (const std::string &t : kCore)
	{
		std::vector<std::uint8_t> one;
		la.gameState().saveBlocks({ t }, one);
		sizes += t + " " + std::to_string(one.size() - 16 - 7) + " B; "; // without the stream's 16-byte header and the 7-byte end token
	}
	double best = 1e9;
	for (int i = 0; i < 20; ++i)
	{
		const auto t0 = std::chrono::steady_clock::now();
		std::vector<std::uint8_t> w;
		la.gameState().saveBlocks(kCore, w);
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		best = ms < best ? ms : best;
	}
	MESSAGE("SAVE-4a core snapshot of a 4-player skirmish at frame 3000 (" << la.getObjectCount() << " objects, " << members << " team members): "
		<< stream.size() << " bytes (" << sizes << "), written in " << best << " ms (best of 20)");
}
