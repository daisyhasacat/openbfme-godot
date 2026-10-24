// OpenBFME unit tests. GPL-3.0.
// The wall rules in the game setup (GameNetwork/WallBalanceSetup.h): the canonical line round-trips every field bit for bit, off is "", a line this build
// does not read is refused, and SkirmishGameInfo carries the line through the network / replay encoding (flag 0x200) while a setup without the rules
// keeps its old bytes. The players' starts stamp the starter's rules.

#include "doctest.h"

#include "GameNetwork/LANLobby.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/Transport.h"
#include "GameNetwork/WallBalanceSetup.h"

#include <cstring>
#include <string>
#include <vector>

namespace
{
WallBalance customRules()
{
	WallBalance w;
	w.enabled = true;
	w.intermediateHubs = false;
	w.shareArrows = true;
	w.trebuchetWallTargets = true;
	w.startingWalls = true;
	w.requireEnclosed = false;
	w.gateGap = false;
	w.disableUpgradeSeparation = true;
	w.enclosedIncome = 2.0f;
	w.startingRadius = 400.0f;
	w.startingSpacing = 300.0f;
	w.wallDamage = 1;
	w.fortressDamage = 2;
	w.health[0] = 6.0f;
	w.health[1] = 30.0f;
	w.health[2] = 0.1f; // not exact in decimal: the bits must survive
	w.cost[0] = 0.5f;
	w.cost[1] = 0.7f;
	w.cost[2] = 0.3f;
	w.cost[3] = 0.8f;
	w.fortressRange = 2.0f;
	w.hubRange = 1.25f;
	w.startingHubs = { { "Men", "GondorWallHub" }, { "Mordor", "MordorWallHub" } };
	w.startingGates = { { "Men", "GondorGate" } };
	return w;
}

bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }

SkirmishGameInfo twoPlayers()
{
	SkirmishGameInfo g;
	g.mapName = "maps/map mp fords of isen";
	g.seed = 7;
	g.slots[0].state = SLOT_PLAYER;
	g.slots[0].name = u"Aragorn";
	g.slots[1].state = SLOT_PLAYER;
	g.slots[1].name = u"Saruman";
	return g;
}
} // namespace

TEST_CASE("wall balance setup: the canonical line round-trips every rule bit for bit; off is \"\"; an unknown line is refused")
{
	CHECK(WallBalanceSetup::toText(WallBalance()) == "");
	WallBalance off;
	off.startingWalls = true; // a rule without `enabled` changes nothing in play: off has no fields
	CHECK(WallBalanceSetup::toText(off) == "");
	WallBalance back;
	REQUIRE(WallBalanceSetup::fromText("", back, nullptr));
	CHECK_FALSE(back.enabled);

	const WallBalance w = customRules();
	const std::string line = WallBalanceSetup::toText(w);
	MESSAGE(line);
	CHECK(line.rfind("wallbalance 1;", 0) == 0);
	std::string error;
	REQUIRE_MESSAGE(WallBalanceSetup::fromText(line, back, &error), error);
	CHECK(back.enabled);
	CHECK(back.intermediateHubs == w.intermediateHubs);
	CHECK(back.shareArrows == w.shareArrows);
	CHECK(back.trebuchetWallTargets == w.trebuchetWallTargets);
	CHECK(back.startingWalls == w.startingWalls);
	CHECK(back.requireEnclosed == w.requireEnclosed);
	CHECK(back.gateGap == w.gateGap);
	CHECK(back.disableUpgradeSeparation == w.disableUpgradeSeparation);
	CHECK(back.wallDamage == 1);
	CHECK(back.fortressDamage == 2);
	CHECK(sameBits(back.health[2], 0.1f));
	CHECK(sameBits(back.cost[1], 0.7f));
	CHECK(sameBits(back.hubRange, 1.25f));
	CHECK(sameBits(back.startingRadius, 400.0f));
	CHECK(back.startingHubs == w.startingHubs);
	CHECK(back.startingGates == w.startingGates);
	CHECK(WallBalanceSetup::toText(back) == line); // canonical: the same rules, the same line

	CHECK_FALSE(WallBalanceSetup::fromText("wallbalance 2;x=1", back, &error));
	CHECK(error.find("unknown version") != std::string::npos);
	std::string missing = line.substr(0, line.rfind(';'));
	CHECK_FALSE(WallBalanceSetup::fromText(missing, back, &error));
	std::string bad = line;
	bad.replace(bad.find("wall_damage=1"), 13, "wall_damage=7");
	CHECK_FALSE(WallBalanceSetup::fromText(bad, back, &error));
	CHECK(error.find("wall_damage") != std::string::npos);
	CHECK(WallBalanceSetup::describe(w) == "on, starting walls, towers share arrow upgrades");
	CHECK(WallBalanceSetup::describe(WallBalance()) == "off");
}

TEST_CASE("wall balance setup: the game setup carries the rules (flag 0x200) and keeps its old bytes without them; a LAN START stamps the host's")
{
	SkirmishGameInfo g = twoPlayers();
	NetByteWriter plain;
	NetPacket::writeGameInfo(plain, g);
	g.wallBalance = WallBalanceSetup::toText(customRules());
	NetByteWriter walled;
	NetPacket::writeGameInfo(walled, g);
	// without the rules: the bytes of the builds before (the rules only append, behind the flag)
	REQUIRE(walled.data().size() == plain.data().size() + 4 + g.wallBalance.size()); // the u32 length and the line
	{
		SkirmishGameInfo back;
		NetByteReader r(plain.data());
		std::string error;
		REQUIRE_MESSAGE(NetPacket::readGameInfo(r, back, &error), error);
		CHECK(back.wallBalance.empty());
		CHECK(r.atEnd());
	}
	{
		SkirmishGameInfo back;
		NetByteReader r(walled.data());
		std::string error;
		REQUIRE_MESSAGE(NetPacket::readGameInfo(r, back, &error), error);
		CHECK(back.wallBalance == g.wallBalance);
		CHECK(back == g);
		CHECK(r.atEnd());
	}
	{
		// a flagged setup whose line this build cannot read is refused (never played with other rules)
		SkirmishGameInfo broken = g;
		broken.wallBalance = "wallbalance 9;future=1";
		NetByteWriter w;
		NetPacket::writeGameInfo(w, broken);
		SkirmishGameInfo back;
		NetByteReader r(w.data());
		std::string error;
		CHECK_FALSE(NetPacket::readGameInfo(r, back, &error));
		CHECK(error.find("wall balance") != std::string::npos);
	}

	// the LAN host's START carries the host's rules (WallBalanceSetup::local), and the joiner decodes them
	WallBalanceSetup::setLocal(customRules());
	UDP socket;
	std::string error;
	REQUIRE_MESSAGE(socket.bind(0, 0, &error), error);
	NewGameMessage m;
	m.game = twoPlayers();
	ProfileIdentity profile = ProfileIdentity::fromText("openbfme-profile 1\nprofile enhanced\n");
	LANLobbyHost host(socket, m, 0, 1, 100, profile);
	CHECK(host.start().game.game.wallBalance == WallBalanceSetup::toText(customRules()));
	LobbyStart start = host.start();
	start.localSlot = 1;
	LobbyStart back;
	REQUIRE_MESSAGE(LANLobby::decodeStart(LANLobby::encodeStart(start), back, &error), error);
	CHECK(back.game.game.wallBalance == WallBalanceSetup::toText(customRules()));
	WallBalanceSetup::setLocal(WallBalance());
}
