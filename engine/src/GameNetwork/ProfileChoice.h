// OpenBFME. GPL-3.0.
//
// ProfileChoice (lane UBERJOIN-1, docs/ONLINE.md "Performance mode in the lobby"): the player's simulation profile (Common/SimulationProfile.h) and the one a
// lobby switched to for a single game. GodotDevice's GameWorld owns one; the lobby screens reach it through GameClient/GUI/ProfileSwitch.h.
//   own      the player's setting (the OpenBFME option "Uber performance mode"); never written by a switch
//   current  what the next game runs and what the LAN lobby announces; own, unless a lobby switched it
//   switched a lobby's Yes / No box switched `current` for one game: it goes back to `own` when `restore` is allowed
// A LAN room that is still held (LANAPI::currentGame(): the room is kept after a game, as retail does, so the players return to it) pins `current`: the
// profile cannot change inside a LAN room and a restore waits for the room to be left (`leaveRoomAndRestore` leaves it first).
// NOT SIMULATION CODE: client / lobby state, never part of the logic.

#pragma once

#include "Common/SimulationProfile.h"
#include "GameNetwork/LANAPI.h"

#include <functional>
#include <string>

struct ProfileChoice
{
	SimulationProfile own = SimulationProfile::Faithful;
	SimulationProfile current = SimulationProfile::Faithful;
	bool switched = false;
	LANAPI *lan = nullptr;                                          ///< the LAN lobby to keep in step (not owned; null: none)
	std::function<ProfileIdentity(SimulationProfile)> identityFor;  ///< the profile identity of a mode (the world's mounted archives)

	bool roomHeld() const { return lan && lan->currentGame(); }

	// the player's setting changed (the option). Inside a LAN room the game's profile cannot change: refused, nothing changes. While a lobby's switch holds,
	// only the setting changes (it takes over at the restore); a setting equal to the switched profile ends the switch
	bool setOwn(SimulationProfile p, std::string *why)
	{
		if (!simulationProfileValid((std::int32_t)p))
		{
			*why = "unknown simulation profile";
			return false;
		}
		if (switched)
		{
			own = p;
			switched = own != current;
			return true;
		}
		if (p != current && roomHeld())
		{
			*why = "the simulation profile cannot change inside a LAN game (leave it first)";
			return false;
		}
		own = current = p;
		announce();
		return true;
	}
	// a lobby's Yes box: this game runs `p`. Refused inside a LAN room (nothing changes)
	bool switchForGame(SimulationProfile p, std::string *why)
	{
		if (!simulationProfileValid((std::int32_t)p))
		{
			*why = "unknown simulation profile";
			return false;
		}
		if (p != current && roomHeld())
		{
			*why = "the simulation profile cannot change inside a LAN game (leave it first)";
			return false;
		}
		current = p;
		switched = current != own;
		announce();
		return true;
	}
	// back to the player's setting. True: nothing is switched any more. False: a LAN room is still held, so the switched profile stays until it is left
	bool restore()
	{
		if (!switched)
		{
			return true;
		}
		if (roomHeld())
		{
			return false;
		}
		current = own;
		switched = false;
		announce();
		return true;
	}
	// the LAN room is left (the game is over and nobody returns to the room, or the lobby is closed), then restore
	bool leaveRoomAndRestore()
	{
		if (lan && lan->currentGame())
		{
			lan->requestGameLeave();
		}
		return restore();
	}

private:
	void announce()
	{
		if (lan && !lan->currentGame() && identityFor)
		{
			lan->setProfile(identityFor(current)); // the lobby list announces the identity of the mode the next game runs
		}
	}
};
