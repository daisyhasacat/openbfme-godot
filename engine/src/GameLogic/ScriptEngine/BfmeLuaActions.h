// OpenBFME. GPL-3.0. Host decisions as bounded, replayable simulation commands.
#pragma once
#include "GameLogic/GameMessage.h"
#include "GameLogic/ScriptEngine/BfmeLuaRuntime.h"
#include <cstdint>
#include <map>
class GameLogic;
class GameLogicDispatch;
class BfmeLuaActionState
{
public:
	int authorityPlayer = -1; // fixed by the START; no packet-router fallback or client claim
	std::uint64_t lastRequest = 0; // protects against old requests even after their UI result expires
	struct Outcome { std::string status = "pending"; unsigned object = 0; };
	std::map<std::uint64_t, Outcome> outcomes; // UI tickets, not simulation input; bounded at 1024
	Outcome outcome(std::uint64_t request) const;
};
namespace BfmeLuaActions
{
constexpr unsigned kMaxBytes = 16384;
// Presentation mutations remain local. Gameplay actions are emitted only by the authority.
bool presentationOnly(const BfmeLuaRuntime::Mutation &mutation);
GameMessage encode(int authorityPlayer, std::uint64_t request, const BfmeLuaRuntime::Mutation &mutation);
bool decode(const GameMessage &message, std::uint64_t &request, BfmeLuaRuntime::Mutation &mutation, std::string &error);
// Portable: Godot peers, replays, spectators and a future simulation server share this executor.
std::string apply(GameLogic &logic, GameLogicDispatch &dispatcher, const BfmeLuaRuntime::Mutation &mutation, unsigned &created);
std::string dispatch(GameLogic &logic, GameLogicDispatch &dispatcher, const GameMessage &message);
}
