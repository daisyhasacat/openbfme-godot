// OpenBFME. GPL-3.0.
//
// NetIncidentReport (OpenBFME addition, no retail counterpart): a readable account of what went wrong in a network game, written when the game desynced, a
// player went silent, left or was dropped, or the local player had to quit. It joins what the peer knows into one timeline with an explanation per event:
// the CRC exchange's desyncs (Network.h DesyncReport: who disagreed with whom, in which subsystems and objects), the disconnect path's log
// (DisconnectManager::log with its frames and times: silence, the disconnect screen, votes, drops), the players who left, the local quit reason and the
// network's errors. The technical dumps stay where they are (DesyncDump.h, the replay); the report names them.
//
// The report is local: it carries the players' names, so it is never uploaded (game.gd's desync upload sends only the dump).

#pragma once

#include "GameNetwork/Network.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace NetIncidentReport
{
struct Facts
{
	// the game
	std::string mapName;
	std::string build;    ///< the build that wrote the report (commit, platform)
	std::string started;  ///< the local wall-clock time the game started ("YYYY-MM-DD HH:MM:SS")
	bool online = false;
	std::uint64_t onlineGameId = 0;
	int localSlot = -1;
	std::uint32_t frame = 0;      ///< the logic frame the local game reached
	std::uint64_t startMs = 0;    ///< the Network's clock when the game began (the timeline's times are relative to it)
	std::uint64_t nowMs = 0;      ///< the Network's clock when the report was written
	bool ended = false;           ///< the local game is over (the report is final)
	// the players (by GameInfo slot)
	struct Slot
	{
		bool network = false; ///< a human player of the network game
		std::string name;
		bool left = false;         ///< PLAYERLEAVE (it quit the game)
		bool disconnected = false; ///< dropped by the disconnect path
		bool held = false;         ///< dropped and held for a rejoin (online)
		bool handedOver = false;   ///< its army was handed over
		std::uint32_t departureFrame = 0xFFFFFFFFu;
		std::uint32_t rejoinFrame = 0;
		int silentSpells = 0;      ///< DisconnectManager::silentEpisodes
	};
	std::array<Slot, MAX_SLOTS> slots{};
	// the CRC exchange
	int crcInterval = 0;
	unsigned long long crcChecksPassed = 0;
	std::vector<DesyncReport> desyncs;
	std::vector<std::string> desyncDumps; ///< the dump files (DesyncDump.h), by desync
	std::string replayPath;
	std::string wallRules; ///< WallBalanceSetup::describe of the setup's wall rules (every peer plays them)
	// the disconnect path
	struct Event
	{
		std::uint32_t frame = 0;
		std::uint64_t ms = 0;
		std::string line;
	};
	std::vector<Event> disconnectLog;
	std::string quitReason;
	bool localLinkFailed = false; ///< the ping test fails now (relay mode: nothing came from the relay for NetworkDisconnectTime)
	std::uint32_t disconnectTimeMs = 0, playerTimeoutMs = 0;
	// the link
	int runAhead = 0;
	int maxRoundTripMs = -1;
	unsigned long long framesResent = 0;
	std::vector<std::string> rejoinLog;
	std::vector<std::string> networkErrors;
	std::vector<std::string> recordingErrors;
};

// the network's part of the facts (players, desyncs, the disconnect path, the link); the caller adds the game, the files and the times
void collect(const Network &net, Facts &out);
// something worth a report happened: a desync, a silent spell, a departure, a quit request or a network error
bool worthReporting(const Facts &f);
// the report: a summary, the players, the timeline, one section per desync and per player that went away, the files to send with a bug report
std::string text(const Facts &f);
// "NETREPORT-<started>-<player>.txt" in `directory` ("" = the current directory)
std::string fileName(const std::string &directory, const std::string &started, const std::string &playerName);
// false + *error when the file cannot be written
bool write(const std::string &path, const std::string &text, std::string *error);
// the game time of a logic frame ("m:ss", LOGICFRAMES_PER_SECOND)
std::string gameTime(std::uint32_t frame);
} // namespace NetIncidentReport
