// OpenBFME. GPL-3.0.
// See GameNetwork/NetIncidentReport.h.

#include "GameNetwork/NetIncidentReport.h"

#include "Common/GameCommon.h"
#include "GameClient/GUI/LoadScreenInfo.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace
{
using Facts = NetIncidentReport::Facts;

std::string hex32(std::uint32_t v)
{
	char b[16];
	std::snprintf(b, sizeof(b), "0x%08X", v);
	return b;
}

std::string seconds(std::uint64_t ms)
{
	char b[32];
	std::snprintf(b, sizeof(b), "%.1f s", (double)ms / 1000.0);
	return b;
}

// "Name (slot N)", "you (Name, slot N)" for the local player
std::string who(const Facts &f, int slot)
{
	if (slot < 0 || slot >= MAX_SLOTS)
	{
		return "slot " + std::to_string(slot);
	}
	const std::string &name = f.slots[(size_t)slot].name;
	if (slot == f.localSlot)
	{
		return "you (" + (name.empty() ? std::string() : name + ", ") + "slot " + std::to_string(slot) + ")";
	}
	return (name.empty() ? std::string("a player") : name) + " (slot " + std::to_string(slot) + ")";
}

// the time of a timeline entry: the game time of its frame, and the seconds since the game began when the clock is known
std::string when(const Facts &f, std::uint32_t frame, std::uint64_t ms)
{
	std::string s = NetIncidentReport::gameTime(frame) + " (frame " + std::to_string(frame);
	if (ms != 0 && f.startMs != 0 && ms >= f.startMs)
	{
		s += ", " + seconds(ms - f.startMs) + " after the start";
	}
	return s + ")";
}

// the number after `key` in `line` (-1 when absent)
long long numberAfter(const std::string &line, const std::string &key)
{
	const size_t p = line.find(key);
	if (p == std::string::npos)
	{
		return -1;
	}
	size_t i = p + key.size();
	if (i >= line.size() || line[i] < '0' || line[i] > '9')
	{
		return -1;
	}
	long long v = 0;
	while (i < line.size() && line[i] >= '0' && line[i] <= '9')
	{
		v = v * 10 + (line[i++] - '0');
	}
	return v;
}

bool startsWith(const std::string &s, const std::string &p) { return s.compare(0, p.size(), p) == 0; }

// what a state hash section holds (GameLogic::computeStateHashBreakdown's names), for a reader who does not know the engine
std::string sectionMeaning(const std::string &name)
{
	static const std::map<std::string, std::string> m = {
		{ "frame, object ids, RNG", "the frame counter, the next object id and the random number generator: the games drew a different number of random "
									"numbers or created a different number of objects" },
		{ "scheduler", "the order in which objects' modules are updated" },
		{ "objects", "units and buildings: position, health, orders, upgrades, AI state" },
		{ "partition manager", "the map grid that tracks where every object is" },
		{ "emotion system", "unit morale (fear, cheering near heroes)" },
		{ "players and teams", "the players and their teams: resources, upgrades, powers, relations" },
		{ "settings", "the game's settings" },
		{ "economy", "income and resource gathering" },
		{ "combat", "weapons, projectiles and damage in flight" },
		{ "victory", "the victory conditions (who is still alive)" },
		{ "sell list", "buildings being sold" },
		{ "skirmish AI", "the computer players' decisions" },
		{ "experience", "experience and veterancy" },
		{ "create-a-hero", "the Create-a-Hero heroes" },
		{ "weather", "weather effects" },
		{ "invisibility", "stealth and detection" },
		{ "damage over time", "poison, fire and other damage over time" },
		{ "terrain trees", "trees that were felled or harvested" },
		{ "large group audio", "the large-group audio gate" },
		{ "script engine", "the map's scripts" },
	};
	auto it = m.find(name);
	return it != m.end() ? it->second : std::string();
}

// one disconnect-path log line in words (DisconnectManager's note() texts); "" = show the line as it is
std::string explainLogLine(const Facts &f, const std::string &line)
{
	if (startsWith(line, "slot ") && line.find(" silent at frame ") != std::string::npos)
	{
		const int slot = (int)numberAfter(line, "slot ");
		if (line.find("of the local link") != std::string::npos)
		{
			return "nothing arrived from " + who(f, slot) + " for " + seconds(f.disconnectTimeMs) + " and your own link failed its check: the fault is "
				+ "on your side (counted against you)";
		}
		return "nothing arrived from " + who(f, slot) + " for " + seconds(f.disconnectTimeMs) + ": it went silent";
	}
	if (startsWith(line, "disconnect screen on"))
	{
		return "the disconnect screen opened: the game waits for the silent player(s); each is dropped after " + seconds(f.playerTimeoutMs) + " or a vote";
	}
	if (startsWith(line, "disconnect screen off"))
	{
		return "every player was heard again: the disconnect screen closed and the game went on";
	}
	if (startsWith(line, "vote of slot "))
	{
		return who(f, (int)numberAfter(line, "vote of slot ")) + " voted to drop " + who(f, (int)numberAfter(line, " to drop slot "));
	}
	if (startsWith(line, "automatic vote for slot "))
	{
		return who(f, (int)numberAfter(line, "automatic vote for slot ")) + " stayed silent until its timeout ran out: an automatic vote to drop it";
	}
	if (startsWith(line, "dropping slot "))
	{
		return "your game, acting for the remaining players (the next packet router), dropped " + who(f, (int)numberAfter(line, "dropping slot "));
	}
	if (startsWith(line, "slot ") && line.find(" disconnected at frame ") != std::string::npos)
	{
		return who(f, (int)numberAfter(line, "slot ")) + " is out of the game from frame " + std::to_string(numberAfter(line, " disconnected at frame "));
	}
	if (startsWith(line, "holding slot "))
	{
		return who(f, (int)numberAfter(line, "holding slot ")) + " is held for a rejoin: its army waits, idle";
	}
	if (line.find(" rejoined") != std::string::npos && startsWith(line, "slot "))
	{
		return who(f, (int)numberAfter(line, "slot ")) + " rejoined the game";
	}
	if (startsWith(line, "DISCONNECTPLAYER of slot "))
	{
		return "the drop of " + who(f, (int)numberAfter(line, "DISCONNECTPLAYER of slot ")) + " decided by another player arrived";
	}
	if (startsWith(line, "quit: "))
	{
		return "your game had to end: " + line.substr(6);
	}
	if (startsWith(line, "announced frame "))
	{
		return "your game told the others the last frame it ran (" + line.substr(16) + "), so every remaining player stops at the same frame";
	}
	if (startsWith(line, "load screen gone"))
	{
		return "the loading screen closed" + std::string(line.find("silent") != std::string::npos ? " because a player never finished loading" : "");
	}
	return "";
}

std::string explainQuit(const Facts &f)
{
	const std::string &r = f.quitReason;
	if (r == "the local connection failed five times")
	{
		return "Your own connection dropped out five times. When your link is the one failing, every other player looks silent from here, so the game "
			   "ends on your side instead of dropping them.";
	}
	if (r == "no other player is connected")
	{
		return "Every other player had left or been dropped, so there was nobody left to play with.";
	}
	if (r == "the game is decided for the local player")
	{
		return "You had already won or lost, so a silent player ended your game instead of opening the disconnect screen.";
	}
	if (r == "the local player quit from the disconnect screen")
	{
		return "You pressed Quit on the disconnect screen.";
	}
	if (startsWith(r, "dropped by the other players"))
	{
		const long long s = numberAfter(r, "(slot ");
		return "The other players dropped you: they stopped hearing from you" + (s >= 0 ? ", and " + who(f, (int)s) + " sent the drop" : std::string())
			+ ". Your game kept running locally, but nothing it did reached them any more.";
	}
	return r;
}

void writeDesync(std::ostringstream &o, const Facts &f, size_t index)
{
	const DesyncReport &d = f.desyncs[index];
	o << "DESYNC " << index + 1 << ": the games stopped matching at " << when(f, d.frame, 0) << "\n";
	o << "----------------------------------------------------------------------\n";
	o << "What happened: every " << f.crcInterval << " frames each player sends a checksum of its whole game state. The checksums of frame " << d.frame
	  << " (compared on frame " << d.checkedOnFrame << ") differ, so from then on the players are no longer simulating the same battle.\n";
	if (index > 0)
	{
		o << "When: the games were already apart since DESYNC 1; this check only confirms it.\n";
	}
	else if (f.crcChecksPassed == 0)
	{
		o << "When: this was the first check of the game, so the games split before " << NetIncidentReport::gameTime(d.frame)
		  << ", possibly from the start (a sign of different game data).\n";
	}
	else if (f.crcInterval > 0 && d.frame >= (std::uint32_t)f.crcInterval)
	{
		const std::uint32_t prev = d.frame - (std::uint32_t)f.crcInterval;
		o << "When: the previous check (frame " << prev << ") matched, so the games split between " << NetIncidentReport::gameTime(prev) << " and "
		  << NetIncidentReport::gameTime(d.frame) << ".\n";
	}
	// who agrees with whom
	std::map<std::uint32_t, std::vector<int>> groups;
	for (const auto &kv : d.crcs)
	{
		groups[kv.second].push_back(kv.first);
	}
	o << "\nChecksums:\n";
	for (const auto &g : groups)
	{
		o << "  " << hex32(g.first) << ":";
		for (int s : g.second)
		{
			o << " " << who(f, s) << ";";
		}
		o << "\n";
	}
	int localGroup = 0, largest = 0;
	for (const auto &g : groups)
	{
		largest = std::max(largest, (int)g.second.size());
		if (std::find(g.second.begin(), g.second.end(), f.localSlot) != g.second.end())
		{
			localGroup = (int)g.second.size();
		}
	}
	if (d.crcs.size() <= 2)
	{
		o << "With two players there is no majority: it cannot be told whose game went wrong, only where the two differ.\n";
	}
	else if (localGroup == 1 && largest > 1)
	{
		o << "Your game is the odd one out: the others agree with each other.\n";
	}
	else
	{
		for (const auto &g : groups)
		{
			if (g.second.size() == 1 && (int)g.second.size() < largest)
			{
				o << who(f, g.second[0]) << " is the odd one out: the others agree with each other.\n";
			}
		}
	}
	// where they differ
	if (d.halves.size() < 2)
	{
		o << "\nWhere: only " << d.halves.size() << " of " << d.crcs.size() << " players' detailed breakdowns arrived";
		std::vector<std::string> missing;
		for (const auto &kv : d.crcs)
		{
			if (!d.halves.count(kv.first))
			{
				missing.push_back(who(f, kv.first));
			}
		}
		if (!missing.empty())
		{
			o << " (missing: ";
			for (size_t i = 0; i < missing.size(); ++i)
			{
				o << (i ? ", " : "") << missing[i];
			}
			o << ")";
		}
		o << ", so the subsystems that differ are not known. A player who leaves right after the desync never sends it.\n";
	}
	else
	{
		const std::vector<std::string> diff = d.differingSections();
		o << "\nWhere: " << diff.size() << " part(s) of the game state differ:\n";
		for (const std::string &s : diff)
		{
			const std::string meaning = sectionMeaning(s);
			o << "  - " << s << (meaning.empty() ? "" : ": " + meaning) << "\n";
		}
		// the objects that differ or exist on one side only
		std::map<ObjectID, std::map<int, const GameLogic::ObjectStateHash *>> byId;
		for (const auto &kv : d.halves)
		{
			for (const GameLogic::ObjectStateHash &x : kv.second.objects)
			{
				byId[x.id][kv.first] = &x;
			}
		}
		std::vector<std::string> lines;
		std::map<std::string, int> byTemplate;
		for (const auto &kv : byId)
		{
			bool differs = kv.second.size() != d.halves.size();
			const GameLogic::ObjectStateHash *ref = kv.second.begin()->second;
			for (const auto &e : kv.second)
			{
				differs = differs || e.second->value != ref->value;
			}
			if (!differs)
			{
				continue;
			}
			++byTemplate[ref->templateName];
			if (lines.size() < 15)
			{
				std::string l = "  - object " + std::to_string(kv.first) + " " + ref->templateName;
				if (kv.second.size() != d.halves.size())
				{
					std::string present;
					for (const auto &h : d.halves)
					{
						if (kv.second.count(h.first))
						{
							present += (present.empty() ? "" : ", ") + who(f, h.first);
						}
					}
					l += ": exists only for " + present;
				}
				else
				{
					l += ": its state differs";
				}
				lines.push_back(l);
			}
		}
		if (byTemplate.empty())
		{
			o << "No single unit or building differs: the difference is in the shared state above.\n";
		}
		else
		{
			int total = 0;
			for (const auto &kv : byTemplate)
			{
				total += kv.second;
			}
			o << total << " unit(s) or building(s) differ. By kind:";
			std::vector<std::pair<int, std::string>> kinds;
			for (const auto &kv : byTemplate)
			{
				kinds.push_back({ -kv.second, kv.first });
			}
			std::sort(kinds.begin(), kinds.end());
			for (size_t i = 0; i < kinds.size() && i < 8; ++i)
			{
				o << (i ? ", " : " ") << kinds[i].second << " x" << -kinds[i].first;
			}
			o << (kinds.size() > 8 ? ", ..." : "") << "\nThe first ones:\n";
			for (const std::string &l : lines)
			{
				o << l << "\n";
			}
		}
	}
	if (d.halves.size() >= 2)
	{
		const std::vector<std::string> diff = d.differingSections();
		if (std::find(diff.begin(), diff.end(), "settings") != diff.end())
		{
			o << "\nThe \"settings\" part differs. It holds only configuration (the map's and the game data's rules and the optional wall rules), nothing "
				 "that happens in play, so the players started with different rules: different game data or INI files, or (builds before the wall rules "
				 "were shared through the game setup) different local wall_balance.cfg options. Everything else differing follows from that.\n";
		}
	}
	o << "\nLikely causes: the players run different game data (a mod or edited file on one machine only) or different OpenBFME builds (compare the "
		 "build line of each player's report), or a simulation bug in OpenBFME. The game does not end on a desync, as in RotWK: it goes on, but each "
		 "player sees a different battle, which is usually why someone leaves soon after.\n";
	if (index < f.desyncDumps.size())
	{
		o << "Technical dump: " << f.desyncDumps[index] << "\n";
	}
	o << "\n";
}
} // namespace

std::string NetIncidentReport::gameTime(std::uint32_t frame)
{
	const std::uint32_t s = frame / LOGICFRAMES_PER_SECOND;
	char b[32];
	if (s >= 3600)
	{
		std::snprintf(b, sizeof(b), "%u:%02u:%02u", s / 3600, (s / 60) % 60, s % 60);
	}
	else
	{
		std::snprintf(b, sizeof(b), "%u:%02u", s / 60, s % 60);
	}
	return b;
}

void NetIncidentReport::collect(const Network &net, Facts &out)
{
	const DisconnectManager &dm = net.disconnectManager();
	out.localSlot = net.config().localSlot;
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		Facts::Slot &x = out.slots[(size_t)s];
		x.network = net.isGameSlot(s);
		x.name = loadScreenU16ToUtf8(net.playerName(s));
		x.left = net.hasLeft(s);
		x.disconnected = net.isDisconnected(s);
		x.held = net.isHeld(s);
		x.handedOver = net.isHandedOver(s);
		x.departureFrame = net.departureFrame(s);
		x.rejoinFrame = net.rejoinFrame(s);
		x.silentSpells = dm.silentEpisodes(s);
	}
	out.crcInterval = net.config().crcInterval;
	out.crcChecksPassed = net.crcChecksPassed();
	out.desyncs = net.desyncs();
	out.disconnectLog.clear();
	for (size_t i = 0; i < dm.log().size(); ++i)
	{
		Facts::Event e;
		e.line = dm.log()[i];
		if (i < dm.logTimes().size())
		{
			e.frame = dm.logTimes()[i].frame;
			e.ms = dm.logTimes()[i].ms;
		}
		out.disconnectLog.push_back(e);
	}
	out.quitReason = net.quitReason();
	out.localLinkFailed = !net.pingTest();
	out.disconnectTimeMs = net.config().settings.disconnectTime;
	out.playerTimeoutMs = net.config().settings.playerTimeoutTime;
	out.runAhead = net.currentRunAhead();
	out.maxRoundTripMs = net.maxRoundTripMs();
	out.framesResent = net.framesResent();
	out.rejoinLog = net.rejoinLog();
	out.networkErrors = net.errors();
}

bool NetIncidentReport::worthReporting(const Facts &f)
{
	if (!f.desyncs.empty() || !f.quitReason.empty() || !f.networkErrors.empty())
	{
		return true;
	}
	for (const Facts::Slot &s : f.slots)
	{
		if (s.network && (s.left || s.disconnected || s.held || s.silentSpells > 0 || s.departureFrame != 0xFFFFFFFFu))
		{
			return true;
		}
	}
	for (const Facts::Event &e : f.disconnectLog)
	{
		if (e.line.find(" silent at frame ") != std::string::npos)
		{
			return true;
		}
	}
	return false;
}

std::string NetIncidentReport::text(const Facts &f)
{
	std::ostringstream o;
	o << "OpenBFME network game report\n";
	o << "======================================================================\n";
	o << "Map: " << (f.mapName.empty() ? "(unknown)" : f.mapName) << "\n";
	o << "Started: " << (f.started.empty() ? "(unknown)" : f.started) << "   " << (f.online ? "online game" : "LAN game");
	if (f.online && f.onlineGameId != 0)
	{
		o << " " << f.onlineGameId;
	}
	o << "\nBuild: " << (f.build.empty() ? "(unknown)" : f.build) << "\n";
	if (!f.wallRules.empty())
	{
		o << "Wall rules (shared by every player): " << f.wallRules << "\n";
	}
	o << "Your game reached " << when(f, f.frame, f.nowMs) << (f.ended ? "; the game is over for you" : "; the game was still running") << ".\n\n";

	// ---- summary: one sentence per thing that went wrong, in order ----
	struct Item
	{
		std::uint32_t frame;
		int order;
		std::string text;
	};
	std::vector<Item> summary;
	for (size_t i = 0; i < f.desyncs.size(); ++i)
	{
		summary.push_back({ f.desyncs[i].frame, 0, "the players' games stopped matching (a desync, see DESYNC " + std::to_string(i + 1) + ")" });
	}
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		const Facts::Slot &x = f.slots[(size_t)s];
		if (!x.network || s == f.localSlot)
		{
			continue;
		}
		const std::uint32_t at = x.departureFrame != 0xFFFFFFFFu ? x.departureFrame : f.frame;
		if (x.left)
		{
			summary.push_back({ at, 1, who(f, s) + " quit the game" });
		}
		else if (x.held)
		{
			summary.push_back({ at, 1, who(f, s) + " lost its connection and was dropped; it is held for a rejoin" });
		}
		else if (x.disconnected)
		{
			summary.push_back({ at, 1, who(f, s) + " stopped responding and was dropped" + std::string(x.handedOver ? " (its army was handed over)" : "") });
		}
		else if (x.silentSpells > 0)
		{
			summary.push_back({ at, 2, who(f, s) + " went silent " + std::to_string(x.silentSpells) + " time(s) but came back" });
		}
		if (x.rejoinFrame != 0)
		{
			summary.push_back({ x.rejoinFrame, 1, who(f, s) + " rejoined" });
		}
	}
	if (!f.quitReason.empty())
	{
		summary.push_back({ f.frame, 3, "your game ended: " + f.quitReason });
	}
	std::stable_sort(summary.begin(), summary.end(), [](const Item &a, const Item &b) { return a.frame != b.frame ? a.frame < b.frame : a.order < b.order; });
	o << "SUMMARY\n";
	o << "----------------------------------------------------------------------\n";
	if (summary.empty())
	{
		o << "Nothing went wrong in this game.\n";
	}
	for (const Item &i : summary)
	{
		o << "  " << gameTime(i.frame) << "  " << i.text << "\n";
	}
	if (!f.desyncs.empty())
	{
		const std::uint32_t desyncAt = f.desyncs.front().frame;
		bool goneAfter = false;
		for (int s = 0; s < MAX_SLOTS; ++s)
		{
			const Facts::Slot &x = f.slots[(size_t)s];
			goneAfter = goneAfter || (x.network && (x.left || x.disconnected) && x.departureFrame != 0xFFFFFFFFu && x.departureFrame >= desyncAt);
		}
		if (goneAfter || !f.quitReason.empty())
		{
			o << "\nThe desync came first. A desync does not disconnect anyone by itself: the departures after it are players leaving, or connections "
				 "failing, on their own (see below).\n";
		}
	}
	if (!f.quitReason.empty())
	{
		o << "\nWhy your game ended: " << explainQuit(f) << "\n";
	}
	o << "\n";

	// ---- the players ----
	o << "PLAYERS\n";
	o << "----------------------------------------------------------------------\n";
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		const Facts::Slot &x = f.slots[(size_t)s];
		if (!x.network)
		{
			continue;
		}
		o << "  slot " << s << "  " << (x.name.empty() ? "(no name)" : x.name) << (s == f.localSlot ? "  (you)" : "") << ": ";
		std::string state;
		if (x.left)
		{
			state = "quit at " + gameTime(x.departureFrame);
		}
		else if (x.held)
		{
			state = "dropped at " + gameTime(x.departureFrame) + ", held for a rejoin";
		}
		else if (x.disconnected)
		{
			state = "dropped at " + gameTime(x.departureFrame);
		}
		else if (s == f.localSlot && !f.quitReason.empty())
		{
			state = "left the game";
		}
		else
		{
			state = "in the game";
		}
		if (x.rejoinFrame != 0)
		{
			state += ", rejoined at " + gameTime(x.rejoinFrame);
		}
		if (x.silentSpells > 0)
		{
			state += ", silent spells counted: " + std::to_string(x.silentSpells);
		}
		o << state << "\n";
	}
	o << "\n";

	// ---- the desyncs: the first ones in full; once the games are apart every later check differs too, so the rest are listed by frame ----
	const size_t detailed = std::min<size_t>(f.desyncs.size(), 3);
	for (size_t i = 0; i < detailed; ++i)
	{
		writeDesync(o, f, i);
	}
	if (f.desyncs.size() > detailed)
	{
		o << "LATER CHECKS\n";
		o << "----------------------------------------------------------------------\n";
		o << f.desyncs.size() - detailed << " later checksum checks differed as well (the games stay apart once they split), at frames:";
		for (size_t i = detailed; i < f.desyncs.size(); ++i)
		{
			o << (i == detailed ? " " : ", ") << f.desyncs[i].frame;
			if (i - detailed >= 40 && i + 1 < f.desyncs.size())
			{
				o << ", ... " << f.desyncs.back().frame;
				break;
			}
		}
		o << "\n\n";
	}

	// ---- the disconnect path ----
	if (!f.disconnectLog.empty())
	{
		o << "CONNECTION TIMELINE\n";
		o << "----------------------------------------------------------------------\n";
		o << "A player counts as silent after " << seconds(f.disconnectTimeMs) << " without any packet; it is dropped after " << seconds(f.playerTimeoutMs)
		  << " on the disconnect screen or when the others vote it out.\n";
		for (const Facts::Event &e : f.disconnectLog)
		{
			const std::string words = explainLogLine(f, e.line);
			o << "  " << when(f, e.frame, e.ms) << "  " << (words.empty() ? e.line : words) << "\n";
			if (!words.empty())
			{
				o << "      [" << e.line << "]\n";
			}
		}
		if (f.localLinkFailed)
		{
			o << "At the time of this report your own link fails its check (nothing from the relay for " << seconds(f.disconnectTimeMs)
			  << "): the problem is likely your connection, not the other players'.\n";
		}
		o << "\n";
	}
	if (!f.rejoinLog.empty())
	{
		o << "REJOINS\n";
		o << "----------------------------------------------------------------------\n";
		for (const std::string &l : f.rejoinLog)
		{
			o << "  " << l << "\n";
		}
		o << "\n";
	}

	// ---- the link and the errors ----
	o << "CONNECTION\n";
	o << "----------------------------------------------------------------------\n";
	o << "  command delay (run-ahead): " << f.runAhead << " frame(s)";
	if (f.maxRoundTripMs >= 0)
	{
		o << ", slowest round trip " << f.maxRoundTripMs << " ms";
	}
	o << "\n  checksum checks that matched: " << f.crcChecksPassed << " (one every " << f.crcInterval << " frames)\n";
	if (f.framesResent > 0)
	{
		o << "  frames re-sent to a player that fell behind: " << f.framesResent << "\n";
	}
	if (!f.networkErrors.empty() || !f.recordingErrors.empty())
	{
		o << "  errors (malformed or unexpected data; each one is a bug or a damaged connection):\n";
		for (const std::string &e : f.networkErrors)
		{
			o << "    network: " << e << "\n";
		}
		for (const std::string &e : f.recordingErrors)
		{
			o << "    replay recording: " << e << "\n";
		}
	}
	o << "\n";

	// ---- the files ----
	o << "FILES FOR A BUG REPORT\n";
	o << "----------------------------------------------------------------------\n";
	o << "  this report\n";
	for (const std::string &d : f.desyncDumps)
	{
		o << "  " << d << " (and the .replay next to it)\n";
	}
	o << "  " << (f.replayPath.empty() ? std::string("(this game recorded no replay)") : f.replayPath + " (the replay)") << "\n";
	o << "For a desync, the reports of every player in the game together show the most.\n";
	return o.str();
}

std::string NetIncidentReport::fileName(const std::string &directory, const std::string &started, const std::string &playerName)
{
	std::string name = "NETREPORT-" + started + "-" + playerName + ".txt";
	for (char &c : name)
	{
		if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || c == ' ')
		{
			c = '_';
		}
	}
	if (directory.empty())
	{
		return name;
	}
	const char last = directory.back();
	return directory + (last == '/' || last == '\\' ? "" : "/") + name;
}

bool NetIncidentReport::write(const std::string &path, const std::string &text, std::string *error)
{
	std::ofstream o(path, std::ios::binary | std::ios::trunc); // LF lines on every OS (as DesyncDump)
	if (!o)
	{
		if (error)
		{
			*error = "cannot write " + path;
		}
		return false;
	}
	o << text;
	if (!o)
	{
		if (error)
		{
			*error = "write failed: " + path;
		}
		return false;
	}
	return true;
}
