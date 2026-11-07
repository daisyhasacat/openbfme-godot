// OpenBFME. GPL-3.0.
//
// WallBalanceSetup (OpenBFME addition, no retail counterpart): the optional BFME2Lua wall rules (GameLogic/WallBalance.h) as part of the game setup.
// The rules used to come from each machine's own user://wall_balance.cfg when its game loaded, so two players with different files played different rules
// and desynced from frame 0 (the state hash's "settings" section differs). Now the player who starts the game (the skirmish player, the LAN host, the
// online game's creator) stamps its rules into SkirmishGameInfo::wallBalance, the setup carries them to every peer and into the replay, and every peer
// loads the game with them; its own file is not read for a game that has a setup.
//
// The rules travel as one canonical line, "" when they are off (every rule is gated on WallBalance::enabled, so off needs no fields, and a setup without
// the rules keeps its old bytes): "wallbalance 1;<key>=<value>;..." with the keys in a fixed order, booleans 0 / 1, integers in decimal, floats as the
// eight hex digits of their bits (exact on every peer) and the starting hub / gate maps as "<faction>:<template>" lists.

#pragma once

#include "GameLogic/WallBalance.h"

#include <string>

namespace WallBalanceSetup
{
// the canonical line of `w` ("" when the rules are off)
std::string toText(const WallBalance &w);
// the rules of a canonical line ("" = off: WallBalance()); false + *error for a line this build does not read whole (a refused setup, never a guess)
bool fromText(const std::string &text, WallBalance &out, std::string *error);
// the rules this player would start a game with (its user://wall_balance.cfg, read by the device layer); stamped into a setup it starts
void setLocal(const WallBalance &w);
const WallBalance &local();
// a short description for the lobby and the reports ("off", or the main choices)
std::string describe(const WallBalance &w);
} // namespace WallBalanceSetup
