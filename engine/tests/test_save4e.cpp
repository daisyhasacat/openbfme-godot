// OpenBFME unit tests. GPL-3.0.
// Lane SAVE-4e review r1 (Sol), on the retail game: every closed-set check a real snapshot load runs through refuses an out-of-set value at its place
// (Common/SnapshotProbe.h arms the n-th check of the load), named, and the game the snapshot was loaded into is left exactly as it was (its state hash and
// its own snapshot's bytes; the load is put back, LiveGame::loadSnapshot); a snapshot the game refuses fails its save (CommandLogLoad::takeSnapshot).
// SKIP loudly without ROTWK_INSTALL / BFME2_INSTALL.

#include "doctest.h"

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/GameState.h"
#include "Common/PlayerTemplate.h"
#include "Common/SnapshotProbe.h"
#include "GameClient/CommandLogLoad.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/Map/TerrainTrees.h"
#include "GameLogic/NewGame/NewGame.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <map>
#include <memory>
#include <set>
#include <sstream>
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

// SAVE-4a's game: four Medium AIs (Men, Mordor, Elves, Isengard) on the 8-player Fall Back, each on a team of its own
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
}

// the probe of this thread, reset when the scope ends (a failed REQUIRE must not leave it armed)
struct ProbeScope
{
	ProbeScope(long long armed, std::vector<std::string> *log)
	{
		SnapshotProbe::State &s = SnapshotProbe::state();
		s.armed = armed;
		s.calls = 0;
		s.log = log;
	}
	~ProbeScope()
	{
		SnapshotProbe::State &s = SnapshotProbe::state();
		s.armed = -1;
		s.calls = 0;
		s.log = nullptr;
	}
};

// the snapshot of a 4-AI game at `frame`, with a standing tree of the map toppled (its trees are not the START's)
std::vector<std::uint8_t> snapshotAt(starttest::Shared &s, const NewGameMessage &m, UnsignedInt frame)
{
	Game a;
	load(s, m, a);
	runTo(a, frame);
	TerrainTrees &trees = a.game->logic().trees();
	for (const TerrainTrees::Record &r : trees.records())
	{
		if (r.clientId && !r.toppled)
		{
			TerrainTrees::Record *t = trees.findClosestTree(r.position, 1.0f, true, 0);
			REQUIRE(t);
			trees.toppleTree(*t, Coord3D{ t->position.x - 10.0f, t->position.y, t->position.z }, nullptr);
			break;
		}
	}
	REQUIRE(trees.anyChanged());
	REQUIRE(a.game->advance(0.2) == 1);
	a.game->drainFrames();
	std::vector<std::uint8_t> snap;
	std::string error;
	REQUIRE_MESSAGE(a.game->logic().saveSnapshot(snap, &error), error);
	return snap;
}
} // namespace

TEST_CASE("save4e retail review r1: every closed-set check a real 4-AI game's snapshot load runs through refuses an out-of-set value there, by its name, "
		  "and the game it was loaded into (at its START) is left exactly as it was: its state hash and its snapshot bytes, not closed")
{
	OPENBFME_REQUIRE_START(s);
	const NewGameMessage m = fourPlayerMessage(*s, 5001);
	const std::vector<std::uint8_t> snapshot = snapshotAt(*s, m, 1500);
	std::string error;
	// the inventory: the checks a successful load of this snapshot runs, in call order (a game of the second world loads it)
	std::vector<std::string> calls;
	{
		starttest::Shared *peer = starttest::peerWorld();
		REQUIRE(peer);
		REQUIRE_MESSAGE(peer->world, peer->error);
		Game c;
		{
			auto peerContext = peer->world->enterContext();
			load(*peer, m, c);
		}
		ProbeScope probe(-1, &calls);
		REQUIRE_MESSAGE(c.game->loadSnapshot(snapshot, &error), error);
	}
	std::map<std::string, long long> firstCall; // each check's name and the index of its first call
	for (size_t i = 0; i < calls.size(); ++i)
	{
		firstCall.emplace(calls[i], (long long)i);
	}
	std::string kinds;
	for (const auto &kv : firstCall)
	{
		kinds += " '" + kv.first + "'";
	}
	MESSAGE("the load runs " << calls.size() << " closed-set checks of " << firstCall.size() << " kinds:" << kinds);
	// the checks SAVE-4e added that a real game's load runs through must be among them (by name)
	for (const char *name : { "an object's status", "an object's model conditions", "an object's placement conditions", "an object's upgrades",
			 "an object's armor set flags", "an object's weapon bonus conditions", "an object's script status", "an object's experience flags",
			 "an object's weapon set flags", "an object table entry's status", "a contain's players entered", "a body's last damager players",
			 "the game's Legendary Heroes factor", "pathfinder: the obstacle it ignores" })
	{
		CHECK_MESSAGE(firstCall.count(name) == 1u, std::string(name));
	}
	// each kind, its first call armed: the load is refused naming it, and the START's game is as it was
	Game b;
	load(*s, m, b);
	std::vector<std::uint8_t> before;
	REQUIRE_MESSAGE(b.game->logic().saveSnapshot(before, &error), error);
	const std::uint32_t hash = b.game->logic().computeStateHash();
	int refused = 0;
	std::ostringstream notRefused;
	for (const auto &kv : firstCall)
	{
		INFO("the check '" << kv.first << "' (call " << kv.second << ")");
		std::string why;
		bool loaded = false;
		{
			ProbeScope probe(kv.second, nullptr);
			loaded = b.game->loadSnapshot(snapshot, &why);
		}
		if (loaded || why.find(kv.first) == std::string::npos)
		{
			notRefused << " '" << kv.first << "' (" << (loaded ? "loaded" : why) << ");";
			REQUIRE_MESSAGE(!b.game->closed(), why);
			if (loaded)
			{
				REQUIRE_MESSAGE(b.game->loadSnapshot(before, &error), error); // back to the START for the next probe
			}
			continue;
		}
		++refused;
		REQUIRE_MESSAGE(!b.game->closed(), why);
		CHECK_MESSAGE(why.find("put back") == std::string::npos, why);
		CHECK(b.game->logic().computeStateHash() == hash);
		std::vector<std::uint8_t> after;
		REQUIRE_MESSAGE(b.game->logic().saveSnapshot(after, &error), error);
		CHECK(after == before);
	}
	CHECK_MESSAGE(notRefused.str().empty(), "checks that did not refuse their out-of-set value:" << notRefused.str());
	CHECK(refused == (int)firstCall.size());
	// unarmed, the same snapshot loads into the same game
	REQUIRE_MESSAGE(b.game->loadSnapshot(snapshot, &error), error);
}

TEST_CASE("save4e retail review r1: a snapshot the game refuses fails the save, named (CommandLogLoad::takeSnapshot: never a log-only fallback)")
{
	OPENBFME_REQUIRE_START(s);
	Game g;
	load(*s, fourPlayerMessage(*s, 5002), g);
	runTo(g, 20);
	std::vector<std::uint8_t> out;
	std::string error;
	REQUIRE_MESSAGE(CommandLogLoad::takeSnapshot(*g.game, out, &error), error);
	CHECK(!out.empty());
	// a block without an xfer (a subsystem a later build adds before its xfer): the save is refused by its name, nothing is written
	g.game->logic().gameState().declareBlockWithoutXfer("test:Unsaved", 1u << SNAPSHOT_SAVELOAD, "SAVE-4e test", { "test" });
	CHECK_FALSE(CommandLogLoad::takeSnapshot(*g.game, out, &error));
	CHECK(error == "the game cannot be saved: the snapshot is refused: 1 simulation blocks have no xfer yet: test:Unsaved (SAVE-4e test)");
	CHECK(out.empty());
}
