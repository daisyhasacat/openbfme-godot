// OpenBFME tests, lane OWNERFIX-3: the skirmish map list's battle honors (GUI/Skirmish/RealTimeStats.h): the profile's RealTimeStats.ini, the conquest
// state of a map (RW 0x8460B5), the record after a skirmish (RW 0x9CE8F3 / 0x9CE6FE). GPL-3.0.
// The expected values are retail's rules written out case by case (the decomp's AptMpGameSetupRefreshMapList.cpp and StatsReporterChallenge.cpp, tier A /
// B for RotWK), not read back from the code under test.

#include "doctest.h"

#include "GameClient/GUI/Skirmish/RealTimeStats.h"
#include "GameClient/GUI/Skirmish/SkirmishPreferences.h"
#include "GameClient/MapCache.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

namespace
{
struct TempFolder
{
	std::filesystem::path dir;
	TempFolder()
	{
		dir = std::filesystem::temp_directory_path() / ("openbfme-ownerfix3-" + std::to_string(std::random_device{}()) + "-" + std::to_string(std::random_device{}()));
		std::filesystem::remove_all(dir);
		std::filesystem::create_directories(dir);
	}
	~TempFolder() { std::filesystem::remove_all(dir); }
	std::string folder() const { return dir.string() + "/"; }
};

std::string readFile(const std::string &path)
{
	std::ifstream in(path, std::ios::binary);
	std::stringstream s;
	s << in.rdbuf();
	return s.str();
}

MapCacheEntry mapEntry(const std::string &name, bool official, bool multiplayer, int players)
{
	MapCacheEntry m;
	m.name = name;
	m.isOfficial = official;
	m.isMultiplayer = multiplayer;
	m.numPlayers = players;
	return m;
}

// a skirmish of the local human in slot 0 (team `localTeam`) against the given slots
SkirmishHonorsGame game(std::initializer_list<SkirmishHonorsGame::Slot> others, int localTeam = -1, int mapPlayers = 4)
{
	SkirmishHonorsGame g;
	g.valid = true;
	g.localSlot = 0;
	g.slots[0] = { 6, localTeam };
	int i = 1;
	for (const SkirmishHonorsGame::Slot &s : others)
	{
		g.slots[(size_t)i++] = s;
	}
	g.mapKey = "maps/map mp fall back 4p/map mp fall back 4p.map";
	g.mapPlayers = mapPlayers;
	return g;
}
const char *kKey = "maps\\map mp fall back 4p\\map mp fall back 4p.map";
} // namespace

TEST_CASE("ownerfix3 honors file: <user data folder><profile>RealTimeStats.ini, \"%s_%d\" counters, UseMapListTooltips, refusals")
{
	TempFolder t;
	RealTimeStatsPreferences s;
	std::string error;
	// a profile without a file yet: no counters, the constructor's UseMapListTooltips
	REQUIRE_MESSAGE(s.load(t.folder(), u"Ancalgon", &error), error);
	CHECK(s.path() == t.folder() + "AncalgonRealTimeStats.ini");
	CHECK(s.mapCounter(kKey, 4) == 0);
	CHECK(s.values().get("UseMapListTooltips") == "1");
	s.setMapCounter(kKey, 4, 2);
	REQUIRE_MESSAGE(s.write(&error), error);
	const std::string text = readFile(s.path());
	CHECK(text.find(std::string(kKey) + "_4 = 2") != std::string::npos);
	CHECK(text.find("UseMapListTooltips = 1") != std::string::npos);
	// a retail-written file is read back: other keys stay, counters parse with atoi
	{
		std::ofstream out(s.path(), std::ios::binary | std::ios::trunc);
		out << "ProfileCreatedDate = 10/17/2026\r\n" << kKey << "_6 = 3\r\n" << kKey << "_2 = x\r\nLoyalGames = 5\r\n";
	}
	RealTimeStatsPreferences r;
	REQUIRE_MESSAGE(r.load(t.folder(), u"Ancalgon", &error), error);
	CHECK(r.mapCounter(kKey, 6) == 3);
	CHECK(r.mapCounter(kKey, 2) == 0); // atoi("x")
	CHECK(r.getInt("LoyalGames", -1) == 5);
	REQUIRE(r.write(&error));
	CHECK(readFile(r.path()).find("ProfileCreatedDate = 10/17/2026") != std::string::npos);
	// no profile: no file, nothing to write
	RealTimeStatsPreferences none;
	CHECK(none.load(t.folder(), u"", &error));
	CHECK(none.path().empty());
	CHECK_FALSE(none.write(&error));
	CHECK(error.find("no profile") != std::string::npos);
	// a store without a folder (in memory): refused by name, no file in the working directory
	RealTimeStatsPreferences memory;
	CHECK_FALSE(memory.load("", u"Gimli", &error));
	CHECK(error.find("no user data folder") != std::string::npos);
	CHECK_FALSE(std::filesystem::exists("GimliRealTimeStats.ini"));
	// a path that exists but cannot be read as a file
	std::filesystem::create_directories(t.dir / "BadRealTimeStats.ini");
	RealTimeStatsPreferences bad;
	error.clear();
	CHECK_FALSE(bad.load(t.folder(), u"Bad", &error));
	CHECK(error.find("is not a file") != std::string::npos);
	CHECK(bad.mapCounter(kKey, 4) == 0);
	// SkirmishPreferences gives the folder Skirmish.ini is in
	SkirmishPreferences prefs((t.dir / "Skirmish.ini").string());
	CHECK(std::filesystem::equivalent(std::filesystem::u8path(prefs.folder()), t.dir));
}

TEST_CASE("ownerfix3 honors map state (RW 0x8460B5): the highest conquered difficulty, multiplayer maps only, + 0x8000 for a user map, and the images")
{
	TempFolder t;
	RealTimeStatsPreferences s;
	std::string error;
	REQUIRE(s.load(t.folder(), u"Legolas", &error));
	const MapCacheEntry official = mapEntry("maps/map mp fall back 4p/map mp fall back 4p.map", true, true, 4);
	const MapCacheEntry user = mapEntry("maps/my map/my map.map", false, true, 2);
	const MapCacheEntry campaign = mapEntry("maps/map evil gondor/map evil gondor.map", true, false, 2);
	// no stats (the flag clear): 0, a user map 0x8000
	CHECK(RealTimeStatsPreferences::conquestState(nullptr, official) == 0);
	CHECK(RealTimeStatsPreferences::conquestState(nullptr, user) == 0x8000);
	// stats without counters: 1; a map that is not multiplayer stays 0
	CHECK(RealTimeStatsPreferences::conquestState(&s, official) == 1);
	CHECK(RealTimeStatsPreferences::conquestState(&s, user) == 0x8001);
	CHECK(RealTimeStatsPreferences::conquestState(&s, campaign) == 0);
	s.setMapCounter(kKey, 2, 1);
	CHECK(RealTimeStatsPreferences::conquestState(&s, official) == 2);
	s.setMapCounter(kKey, 5, 1);
	CHECK(RealTimeStatsPreferences::conquestState(&s, official) == 5); // the highest wins
	s.setMapCounter(kKey, 6, 1);
	CHECK(RealTimeStatsPreferences::conquestState(&s, official) == 6);
	s.setMapCounter(kKey, 6, 0);
	CHECK(RealTimeStatsPreferences::conquestState(&s, official) == 5); // a zero counter is no conquest
	s.setMapCounter("maps\\my map\\my map.map", 3, 2);
	CHECK(RealTimeStatsPreferences::conquestState(&s, user) == 0x8003);
	// the image table (RW 0x846674 ..), the user-map medium state with the official image as retail has it
	CHECK(std::string(RealTimeStatsPreferences::conquestImage(0)) == "AptDifficultyNotConquered");
	CHECK(std::string(RealTimeStatsPreferences::conquestImage(1)) == "AptDifficultyNotConquered");
	CHECK(std::string(RealTimeStatsPreferences::conquestImage(2)) == "AptDifficultyEasyConquered");
	CHECK(std::string(RealTimeStatsPreferences::conquestImage(3)) == "AptDifficultyMedConquered");
	CHECK(std::string(RealTimeStatsPreferences::conquestImage(4)) == "AptDifficultyHardConquered");
	CHECK(std::string(RealTimeStatsPreferences::conquestImage(5)) == "AptDifficultyBrutalConquered");
	CHECK(std::string(RealTimeStatsPreferences::conquestImage(6)) == "AptDifficultyMaxConquered");
	CHECK(std::string(RealTimeStatsPreferences::conquestImage(0x8000)) == "AptUserMapNotConquered");
	CHECK(std::string(RealTimeStatsPreferences::conquestImage(0x8001)) == "AptUserMapNotConquered");
	CHECK(std::string(RealTimeStatsPreferences::conquestImage(0x8002)) == "AptUserMapEasyConquered");
	CHECK(std::string(RealTimeStatsPreferences::conquestImage(0x8003)) == "AptDifficultyMedConquered");
	CHECK(std::string(RealTimeStatsPreferences::conquestImage(0x8004)) == "AptUserMapHardConquered");
	CHECK(std::string(RealTimeStatsPreferences::conquestImage(0x8005)) == "AptUserMapBrutalConquered");
	CHECK(std::string(RealTimeStatsPreferences::conquestImage(0x8006)) == "AptUserMapMaxConquered");
}

TEST_CASE("ownerfix3 honors record (RW 0x9CE8F3 / 0x9CE6FE): the highest AI difficulty beaten, max(old, count), the all-Brutal 6, and every refusal")
{
	TempFolder t;
	std::string error, what;
	auto fresh = [&](RealTimeStatsPreferences &s) { REQUIRE(s.load(t.folder(), u"Gimli", &error)); };
	{
		// one Easy and one Hard opponent (teams none): difficulty 4, value max(0, 1) = 1
		RealTimeStatsPreferences s;
		fresh(s);
		CHECK(recordSkirmishHonors(s, game({ { 2, -1 }, { 4, -1 } }), true, &what));
		CHECK(s.mapCounter(kKey, 4) == 1);
		CHECK(s.mapCounter(kKey, 2) == 0);
		CHECK(what == std::string(kKey) + "_4 = 1");
		// the old counter is kept when larger
		s.setMapCounter(kKey, 4, 3);
		CHECK(recordSkirmishHonors(s, game({ { 4, -1 }, { 4, -1 } }), true, &what));
		CHECK(s.mapCounter(kKey, 4) == 3);
	}
	{
		// three Brutal opponents on a 4-player map: every opponent Brutal -> 6 with max(old Brutal counter, 3)
		RealTimeStatsPreferences s;
		fresh(s);
		s.setMapCounter(kKey, 5, 7);
		CHECK(recordSkirmishHonors(s, game({ { 5, -1 }, { 5, -1 }, { 5, -1 } }), true, &what));
		CHECK(s.mapCounter(kKey, 6) == 7); // retail compares with the Brutal counter
		CHECK(s.mapCounter(kKey, 5) == 7);
	}
	{
		// two Brutal opponents on a 4-player map: 5
		RealTimeStatsPreferences s;
		fresh(s);
		CHECK(recordSkirmishHonors(s, game({ { 5, -1 }, { 5, -1 } }), true, &what));
		CHECK(s.mapCounter(kKey, 5) == 2);
		CHECK(s.mapCounter(kKey, 6) == 0);
		// a 3-player map: 2 Brutal opponents are every opponent
		RealTimeStatsPreferences m;
		fresh(m);
		CHECK(recordSkirmishHonors(m, game({ { 5, -1 }, { 5, -1 } }, -1, 3), true, &what));
		CHECK(m.mapCounter(kKey, 6) == 2);
	}
	{
		// Medium opponents on teams against the local team 0, a human ally on team 0: 3
		RealTimeStatsPreferences s;
		fresh(s);
		CHECK(recordSkirmishHonors(s, game({ { 3, 1 }, { 3, 1 }, { 6, 0 } }, 0), true, &what));
		CHECK(s.mapCounter(kKey, 3) == 2);
	}
	// the refusals, each with nothing written
	auto refused = [&](const SkirmishHonorsGame &g, bool victory, const char *why) {
		RealTimeStatsPreferences s;
		fresh(s);
		const auto before = s.values().values();
		CHECK_FALSE(recordSkirmishHonors(s, g, victory, &what));
		CHECK_MESSAGE(what.find(why) != std::string::npos, what);
		CHECK(s.values().values() == before);
	};
	refused(game({ { 4, -1 } }), false, "not the local side's victory");
	refused(game({ { 4, 0 }, { 6, 0 } }, 0), true, "sandbox");          // every other slot on the local team
	refused(game({ { 4, 1 }, { 2, 0 } }, 0), true, "an AI ally");       // an Easy AI on the local team voids the record
	refused(game({ { 6, -1 } }), true, "no AI opponent");               // a human opponent only
	SkirmishHonorsGame noLocal = game({ { 4, -1 } });
	noLocal.localSlot = -1;                                              // RW 0x9CE6C5: no local slot -> every slot an ally
	refused(noLocal, true, "an AI ally");
	SkirmishHonorsGame invalid;
	refused(invalid, true, "no skirmish game");
}
