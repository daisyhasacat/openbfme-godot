// OpenBFME unit tests. GPL-3.0.
// The network game's incident report (GameNetwork/NetIncidentReport.h): the facts of a desync followed by a drop worded for the player, and the facts a
// real pair of Networks collects after a desync and a leave.

#include "doctest.h"

#include "GameNetwork/NetIncidentReport.h"
#include "GameNetwork/Network.h"

#include <cstdio>
#include <deque>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
// two peers, every datagram delivered on the next pump
struct Loop
{
	struct End : NetTransport
	{
		Loop *loop = nullptr;
		int slot = 0;
		std::deque<std::pair<int, std::vector<std::uint8_t>>> inbox;
		bool sendTo(int to, const std::vector<std::uint8_t> &bytes, std::string *) override
		{
			loop->ends[(size_t)to].inbox.push_back({ slot, bytes });
			return true;
		}
		bool receive(int &from, std::vector<std::uint8_t> &bytes) override
		{
			if (inbox.empty())
			{
				return false;
			}
			from = inbox.front().first;
			bytes = inbox.front().second;
			inbox.pop_front();
			return true;
		}
	};
	std::vector<End> ends = std::vector<End>(2);
	Loop()
	{
		for (int i = 0; i < 2; ++i)
		{
			ends[(size_t)i].loop = this;
			ends[(size_t)i].slot = i;
		}
	}
};

NetworkConfig config(int slot)
{
	NetworkConfig c;
	c.localSlot = slot;
	c.slotPlayerIndex[0] = 10;
	c.slotPlayerIndex[1] = 11;
	c.slotNames[0] = u"Aragorn";
	c.slotNames[1] = u"Saruman";
	c.runAhead = 2;
	c.crcInterval = 10;
	c.disconnectPath = false;
	return c;
}

GameMessage crc(int player, std::uint32_t value)
{
	GameMessage m(MSG_LOGIC_CRC, player);
	m.appendIntegerArgument((int)value);
	m.appendBooleanArgument(false);
	return m;
}
} // namespace

TEST_CASE("incident report: a desync where the local game is the odd one out, then a drop and the local quit, in words and in order")
{
	NetIncidentReport::Facts f;
	f.mapName = "maps/map mp fords of isen";
	f.build = "abc123 macOS";
	f.started = "2026-10-23 20:00:00";
	f.localSlot = 0;
	f.frame = 1500;
	f.startMs = 1000;
	f.nowMs = 301000;
	f.ended = true;
	f.crcInterval = 100;
	f.crcChecksPassed = 11;
	f.disconnectTimeMs = 5000;
	f.playerTimeoutMs = 60000;
	const char *names[3] = { "Aragorn", "Saruman", "Gandalf" };
	for (int s = 0; s < 3; ++s)
	{
		f.slots[(size_t)s].network = true;
		f.slots[(size_t)s].name = names[s];
	}
	f.slots[2].disconnected = true;
	f.slots[2].departureFrame = 1400;
	f.slots[2].silentSpells = 1;
	DesyncReport d;
	d.frame = 1200;
	d.checkedOnFrame = 1202;
	d.crcs = { { 0, 0x11111111u }, { 1, 0x22222222u }, { 2, 0x22222222u } };
	DesyncHalf h0;
	h0.slot = 0;
	h0.frame = 1200;
	h0.sections = { { "frame, object ids, RNG", 1 }, { "objects", 2 }, { "combat", 3 } };
	h0.objects = { { 7, "GondorFighter", 5 }, { 8, "MordorOrc", 6 } };
	DesyncHalf h1 = h0;
	h1.slot = 1;
	h1.sections[2].value = 4;
	h1.objects[1].value = 9;
	h1.objects.push_back({ 9, "IsengardUrukHai", 1 });
	d.halves[0] = h0;
	d.halves[1] = h1;
	f.desyncs.push_back(d);
	f.desyncDumps.push_back("/tmp/DESYNC-Frame1202-openbfme-Aragorn.txt");
	f.disconnectLog = { { 1350, 271000, "slot 2 silent at frame 1350 (spell 1)" }, { 1351, 271200, "disconnect screen on at frame 1351" },
		{ 1390, 279000, "vote of slot 1 to drop slot 2 (frame 1390)" }, { 1400, 281000, "slot 2 disconnected at frame 1400" },
		{ 1500, 301000, "quit: dropped by the other players (slot 1)" } };
	f.quitReason = "dropped by the other players (slot 1)";
	REQUIRE(NetIncidentReport::worthReporting(f));
	const std::string t = NetIncidentReport::text(f);
	MESSAGE(t);
	CHECK(NetIncidentReport::gameTime(1200) == "4:00");
	// the summary in order: the desync, the drop, the local end
	const size_t desync = t.find("4:00  the players' games stopped matching");
	const size_t drop = t.find("4:40  Gandalf (slot 2) stopped responding and was dropped");
	const size_t end = t.find("5:00  your game ended");
	REQUIRE(desync != std::string::npos);
	REQUIRE(drop != std::string::npos);
	REQUIRE(end != std::string::npos);
	CHECK(desync < drop); // tolerance: the summary's order is checked, not a position
	CHECK(drop < end);    // tolerance: the summary's order is checked, not a position
	CHECK(t.find("The desync came first") != std::string::npos);
	CHECK(t.find("The other players dropped you") != std::string::npos);
	// the desync: the window, the odd one out, the part of the state and the objects
	CHECK(t.find("the games split between 3:40 and 4:00") != std::string::npos);
	CHECK(t.find("Your game is the odd one out") != std::string::npos);
	CHECK(t.find("combat: weapons, projectiles") != std::string::npos);
	CHECK(t.find("objects:") == std::string::npos); // equal sections are not listed
	CHECK(t.find("object 8 MordorOrc: its state differs") != std::string::npos);
	CHECK(t.find("object 9 IsengardUrukHai: exists only for Saruman (slot 1)") != std::string::npos);
	CHECK(t.find("object 7 ") == std::string::npos);
	// the connection timeline in words, with the raw line, and the times since the start
	CHECK(t.find("nothing arrived from Gandalf (slot 2) for 5.0 s") != std::string::npos);
	CHECK(t.find("Saruman (slot 1) voted to drop Gandalf (slot 2)") != std::string::npos);
	CHECK(t.find("278.0 s after the start") != std::string::npos);
	CHECK(t.find("[slot 2 disconnected at frame 1400]") != std::string::npos);
	CHECK(t.find("/tmp/DESYNC-Frame1202-openbfme-Aragorn.txt") != std::string::npos);
	CHECK(NetIncidentReport::fileName("/x/", "20261023-200000", "Ara gorn") == "/x/NETREPORT-20261023-200000-Ara_gorn.txt");
}

TEST_CASE("incident report: a game where nothing went wrong is not reported; two Networks after a desync and a leave are")
{
	{
		Loop loop;
		Network a(config(0), loop.ends[0]);
		NetIncidentReport::Facts f;
		NetIncidentReport::collect(a, f);
		CHECK_FALSE(NetIncidentReport::worthReporting(f));
	}
	Loop loop;
	Network a(config(0), loop.ends[0]);
	Network b(config(1), loop.ends[1]);
	CommandList pa, pb;
	UnsignedInt fa = 0, fb = 0, issued = 0;
	bool bGone = false;
	for (int tick = 0; tick < 400 && fa < 40; ++tick)
	{
		if (fa == 10 && issued != 10)
		{
			issued = 10;
			pa.append(crc(10, 0x1234u));
			pb.append(crc(11, 0x4321u)); // the two games differ at frame 10
		}
		a.update(fa, pa);
		if (!bGone && fb == 20)
		{
			b.leave(fb); // b stops after frame 20 + run-ahead
			bGone = true;
		}
		if (!bGone)
		{
			b.update(fb, pb);
		}
		a.update(fa, pa);
		if (a.isFrameReady(fa))
		{
			CommandList o;
			a.relayCommands(fa++, o);
		}
		if (!bGone && b.isFrameReady(fb))
		{
			CommandList o;
			b.relayCommands(fb++, o);
		}
	}
	CHECK(fa == 40);
	REQUIRE(a.desyncs().size() == 1);
	REQUIRE(a.hasLeft(1));
	NetIncidentReport::Facts f;
	NetIncidentReport::collect(a, f);
	CHECK(f.localSlot == 0);
	CHECK(f.slots[1].name == "Saruman");
	CHECK(f.slots[1].left);
	CHECK(f.slots[1].departureFrame != 0xFFFFFFFFu);
	REQUIRE(NetIncidentReport::worthReporting(f));
	f.frame = fa;
	const std::string t = NetIncidentReport::text(f);
	MESSAGE(t);
	CHECK(t.find("Saruman (slot 1) quit the game") != std::string::npos);
	CHECK(t.find("DESYNC 1") != std::string::npos);
	CHECK(t.find("With two players there is no majority") != std::string::npos);
	CHECK(t.find("you (Aragorn, slot 0)") != std::string::npos);
	// the file
	const std::string path = NetIncidentReport::fileName("", "test", "incident");
	std::string error;
	REQUIRE(NetIncidentReport::write(path, t, &error));
	std::ifstream in(path, std::ios::binary);
	std::stringstream back;
	back << in.rdbuf();
	CHECK(back.str() == t);
	in.close();
	std::remove(path.c_str());
}
