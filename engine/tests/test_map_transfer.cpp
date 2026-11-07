// OpenBFME unit tests. GPL-3.0.
// Map transfer (GameNetwork/MapPackage.h, LANAPI's MAP_AVAILABILITY / MAP_REQUEST / MAP_DATA): the package is canonical and named by its contents, an
// untrusted package breaking the limits is refused, and in a LAN lobby on localhost a joiner without the host's custom map receives it chunk by chunk
// (a window of requests, each asked again when it does not come), installs exactly the bytes the key names, and the host's start waits for it; a map that cannot be sent is reported, not waited on.

#include "doctest.h"

#include "GameNetwork/LANAPI.h"
#include "GameNetwork/MapPackage.h"
#include "GameNetwork/Transport.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace
{
std::vector<MapPackage::File> sampleFiles(std::size_t mapBytes)
{
	std::vector<MapPackage::File> files(3);
	files[0].name = "map mp test.map";
	for (std::size_t i = 0; i < mapBytes; ++i)
	{
		files[0].bytes.push_back((std::uint8_t)((i * 131u + 7u) >> 3));
	}
	files[1].name = "map.ini";
	files[1].bytes = { 'O', 'b', 'j', 'e', 'c', 't' };
	files[2].name = "art\\terrain\\grass.tga";
	files[2].bytes.assign(3000, 0x42);
	return files;
}

std::uint16_t portBase()
{
#ifdef _WIN32
	const int pid = 4321;
#else
	const int pid = (int)getpid();
#endif
	return (std::uint16_t)(30000 + (pid % 500) * 16);
}

ProfileIdentity profile()
{
	MountedArchive a;
	a.install = "rotwk";
	a.canonicalPath = "test.big";
	a.size = 1;
	a.md5 = "0123456789abcdef0123456789abcdef";
	return ProfileIdentity::compute({ a }, RandomAlgorithm::ZH_CarryChain);
}

SkirmishGameInfo gameOn(const std::string &mapName)
{
	SkirmishGameInfo g;
	g.mapName = mapName;
	g.slots[0].state = SLOT_PLAYER;
	g.slots[1].state = SLOT_OPEN;
	return g;
}
} // namespace

TEST_CASE("map transfer: the package is canonical, named by its contents, and an untrusted one breaking the limits is refused")
{
	std::vector<MapPackage::File> files = sampleFiles(5000);
	std::vector<std::uint8_t> a, b;
	std::string error;
	REQUIRE_MESSAGE(MapPackage::encode(files, a, &error), error);
	std::reverse(files.begin(), files.end());
	REQUIRE(MapPackage::encode(files, b, &error));
	CHECK(a == b); // the order the files were found in does not matter
	const std::string id = MapPackage::id(a);
	CHECK(id.size() == 64);
	const std::string key = MapPackage::mapKey(id);
	CHECK(key == "maps/openbfme_map_" + id.substr(0, 32) + "/openbfme_map_" + id.substr(0, 32) + ".map");
	CHECK(MapPackage::keyDigest(key) == id.substr(0, 32));
	CHECK(MapPackage::idMatchesKey(id, key));
	CHECK(MapPackage::keyDigest("maps/map mp fords of isen/map mp fords of isen.map").empty()); // a game map is never sent
	std::vector<MapPackage::File> back;
	REQUIRE_MESSAGE(MapPackage::decode(a, back, &error), error);
	CHECK(back.size() == 3);
	CHECK(MapPackage::mapFileName(back) == "map mp test.map");
	// one changed byte is another map
	std::vector<std::uint8_t> c = a;
	c.back() ^= 1;
	CHECK_FALSE(MapPackage::idMatchesKey(MapPackage::id(c), key));
	// the limits
	for (const char *bad : { "..\\evil.ini", "art\\..\\x", "Map.ini", "a:b", "con.txt", "dir\\", "x." })
	{
		std::vector<MapPackage::File> f = sampleFiles(10);
		f[1].name = bad;
		CHECK_FALSE_MESSAGE(MapPackage::encode(f, a, &error), bad);
	}
	{
		std::vector<MapPackage::File> f = sampleFiles(10);
		f[1].name = "second.map";
		CHECK_FALSE(MapPackage::encode(f, a, &error));
		CHECK(error.find(".map files") != std::string::npos);
	}
	{
		std::vector<std::uint8_t> cut(b.begin(), b.end() - 1);
		CHECK_FALSE(MapPackage::decode(cut, back, &error));
		std::vector<std::uint8_t> extra = b;
		extra.push_back(0);
		CHECK_FALSE(MapPackage::decode(extra, back, &error));
	}
}

TEST_CASE("map transfer: a LAN joiner without the host's custom map receives it, installs exactly it, and the start waits for it")
{
	std::vector<std::uint8_t> package;
	std::string error;
	REQUIRE(MapPackage::encode(sampleFiles(70000), package, &error)); // ~73 KB: 72 chunks
	const std::string key = MapPackage::mapKey(MapPackage::id(package));

	bool joinerHas = false;
	std::vector<std::uint8_t> installed;
	int packageReads = 0;
	LANAPI::Options o;
	o.playerTemplateCount = 16;
	o.colorCount = 10;
	o.lobbyPortBase = portBase();
	o.broadcast = false;
	o.extraTargets = { 0x7F000001u };
	o.resendMs = 300;
	o.mapRetryMs = 60;
	o.mapWindow = 8;
	LANAPI::Options hostOptions = o;
	hostOptions.hasMap = [](const std::string &) { return true; };
	hostOptions.mapPackage = [&](const std::string &name, std::vector<std::uint8_t> &out, std::string *why) {
		++packageReads;
		if (name != key)
		{
			*why = "no such map here";
			return false;
		}
		out = package;
		return true;
	};
	LANAPI::Options joinerOptions = o;
	joinerOptions.hasMap = [&](const std::string &name) { return name != key || joinerHas; };
	joinerOptions.installMap = [&](const std::string &name, const std::vector<std::uint8_t> &bytes, std::string *) {
		CHECK(name == key);
		installed = bytes;
		joinerHas = true;
		return true;
	};
	LANAPI host(profile(), u"Host", hostOptions);
	LANAPI joiner(profile(), u"Joiner", joinerOptions);
	REQUIRE_MESSAGE(host.open(&error), error);
	REQUIRE_MESSAGE(joiner.open(&error), error);
	std::uint64_t clock = 1000;
	std::vector<LANAPI::Event> joinerEvents;
	auto run = [&](int ticks) {
		for (int t = 0; t < ticks; ++t)
		{
			clock += 20;
			host.update(clock);
			joiner.update(clock);
			for (LANAPI::Event &e : joiner.takeEvents())
			{
				joinerEvents.push_back(e);
			}
			host.takeEvents();
			NetSleepMilliseconds(1);
		}
	};
	run(5);
	host.requestGameCreate(gameOn(key));
	joiner.requestLocations();
	run(10);
	REQUIRE(joiner.games().size() == 1);
	joiner.requestGameJoin(0);
	run(5);
	REQUIRE(joiner.currentGame());
	// until it has the map the joiner's slot says so, and the start is refused even with every accept
	joiner.requestAccept(true);
	run(2);
	std::string reason;
	if (!joinerHas)
	{
		CHECK_FALSE(host.hostStartGame(&reason));
		CHECK(reason.find("does not have the map") != std::string::npos);
		CHECK(joiner.mapTransfer().active);
	}
	for (int i = 0; i < 400 && !joinerHas; ++i)
	{
		run(1);
	}
	REQUIRE(joinerHas);
	CHECK(installed == package);
	CHECK(packageReads == 1); // built once, served from memory
	CHECK(joiner.mapTransfer().present);
	CHECK_FALSE(joiner.mapTransfer().failed);
	run(10);
	CHECK(host.currentGame()->info.slots[1].hasMap);
	CHECK(host.mapPercent(1) == 100);
	bool sawReceived = false;
	for (const LANAPI::Event &e : joinerEvents)
	{
		sawReceived = sawReceived || (e.kind == LANAPI::Event::Chat && e.text.find(u"received") != std::u16string::npos);
	}
	CHECK(sawReceived);
	joiner.requestAccept(true);
	run(5);
	CHECK_MESSAGE(host.hostStartGame(&reason), reason);

	// a map of the game itself (not an installed one) is never sent: the joiner says why instead of waiting
	LANAPI::Options j2 = joinerOptions;
	j2.lobbyPortBase = (std::uint16_t)(portBase() + 8);
	LANAPI::Options h2 = hostOptions;
	h2.lobbyPortBase = (std::uint16_t)(portBase() + 8);
	LANAPI hostB(profile(), u"HostB", h2);
	j2.hasMap = [](const std::string &) { return false; };
	LANAPI joinerB(profile(), u"JoinerB", j2);
	REQUIRE(hostB.open(&error));
	REQUIRE(joinerB.open(&error));
	auto runB = [&](int ticks) {
		for (int t = 0; t < ticks; ++t)
		{
			clock += 20;
			hostB.update(clock);
			joinerB.update(clock);
			NetSleepMilliseconds(1);
		}
	};
	hostB.requestGameCreate(gameOn("maps/map mp fords of isen/map mp fords of isen.map"));
	joinerB.requestLocations();
	runB(10);
	REQUIRE(joinerB.games().size() == 1);
	joinerB.requestGameJoin(0);
	runB(10);
	REQUIRE(joinerB.currentGame());
	CHECK(joinerB.mapTransfer().failed);
	CHECK_FALSE(joinerB.mapTransfer().active);
	CHECK_FALSE(hostB.currentGame()->info.slots[1].hasMap);
}
