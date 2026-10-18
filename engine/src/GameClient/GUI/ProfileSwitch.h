// OpenBFME. GPL-3.0.
//
// ProfileSwitch (lane UBERJOIN-1, docs/ONLINE.md "Performance mode"): how the lobby screens change the simulation profile (Common/SimulationProfile.h:
// the "Uber performance mode" option) for one game. A game list shows each game's mode; Join on a game of the other mode asks in the game's own Yes / No
// box ("Switch to Uber mode and join? (Your setting changes back after the game.)"), Yes switches the profile for that game and joins, No stays in the
// list. The player's own setting (Options.ini) is never written: the switch lives in the host program's world (GameWorld) and is undone by `restore`
// when the room is left or the game has ended.
//
// NOT A PORT: OpenBFME's own (retail has one simulation, and no such box).

#pragma once

#include "Common/SimulationProfile.h"

#include <functional>
#include <string>

struct ProfileSwitch
{
	std::function<SimulationProfile()> current;                       ///< what the player's next game runs (the setting, or the switched mode)
	std::function<std::string(SimulationProfile)> digestOf;            ///< the profile identity's MD5 of a mode (the lobby compares it at the join)
	std::function<bool(SimulationProfile, std::string *)> request;    ///< switch for the next game; false: refused, with the reason
	std::function<void()> restore;                                    ///< back to the player's own setting (a no-op when not switched)
	bool complete() const { return current && digestOf && request && restore; }
};

inline const char *performanceModeName(SimulationProfile p)
{
	return p == SimulationProfile::Uber ? "Uber" : "Normal";
}

// the Yes / No box's text for a game in `gameMode` (the player is in the other one)
inline std::u16string performanceModeSwitchPrompt(SimulationProfile gameMode)
{
	const std::string name = performanceModeName(gameMode);
	const std::string text = "This game uses " + name + " performance mode. Switch to " + name + " mode and join? (Your setting changes back after the game.)";
	return std::u16string(text.begin(), text.end());
}
