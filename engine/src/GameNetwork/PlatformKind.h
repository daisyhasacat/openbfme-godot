// OpenBFME. GPL-3.0.
//
// Lane PLATFORMICON-1 (OpenBFME, not retail): the system a player's client runs on (Windows / Linux / macOS), shown as a small icon left of the player's
// name in the staging rooms (online and LAN) and in a running game's player list. DISPLAY DATA ONLY: it is never read by the simulation, never part of
// a slot's equality, the game hash, a start message or a replay's simulation input. A client tells ONE short value (the name below or the one byte of
// the LAN message) and nothing else of its machine (no OS version, no hardware).
//
// A peer that does not tell (an older build) has PlatformKind::None: no icon, nothing else changes.

#pragma once

#include <cstdint>
#include <string>

enum class PlatformKind : std::uint8_t
{
	None = 0, ///< not told (an older client, a computer player, a slot nobody holds)
	Windows = 1,
	Linux = 2,
	MacOS = 3
};

// the closed set of values read from outside: the lobby's three names and the LAN message's byte 0 .. 3
inline bool platformFromByte(std::uint8_t b, PlatformKind &out)
{
	if (b > 3)
	{
		return false;
	}
	out = (PlatformKind)b;
	return true;
}

// "windows" / "linux" / "macos" (the lobby's `platform` field; lobby.mjs PLATFORMS); false for anything else (the empty string too)
inline bool platformFromName(const std::string &name, PlatformKind &out)
{
	if (name == "windows")
	{
		out = PlatformKind::Windows;
	}
	else if (name == "linux")
	{
		out = PlatformKind::Linux;
	}
	else if (name == "macos")
	{
		out = PlatformKind::MacOS;
	}
	else
	{
		return false;
	}
	return true;
}

inline const char *platformName(PlatformKind k)
{
	switch (k)
	{
		case PlatformKind::Windows: return "windows";
		case PlatformKind::Linux: return "linux";
		case PlatformKind::MacOS: return "macos";
		default: return "";
	}
}

// the hover / help text of the icon
inline const char *platformHelpText(PlatformKind k)
{
	switch (k)
	{
		case PlatformKind::Windows: return "Playing on Windows";
		case PlatformKind::Linux: return "Playing on Linux";
		case PlatformKind::MacOS: return "Playing on macOS";
		default: return "";
	}
}

// the mapped image of the icon (registered by registerPlatformIcons, GameClient/GUI/PlatformIcons.h); empty for None
inline const char *platformImageName(PlatformKind k)
{
	switch (k)
	{
		case PlatformKind::Windows: return "OpenBFMEPlatformWindows";
		case PlatformKind::Linux: return "OpenBFMEPlatformLinux";
		case PlatformKind::MacOS: return "OpenBFMEPlatformMacOS";
		default: return "";
	}
}

// the system this build runs on: the build's own platform, the one value the client tells
inline PlatformKind localPlatform()
{
#if defined(_WIN32)
	return PlatformKind::Windows;
#elif defined(__APPLE__)
	return PlatformKind::MacOS;
#elif defined(__linux__)
	return PlatformKind::Linux;
#else
	return PlatformKind::None;
#endif
}
