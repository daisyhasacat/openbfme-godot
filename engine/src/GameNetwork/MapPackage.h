// OpenBFME. GPL-3.0.
//
// MapPackage (OpenBFME addition; retail's LAN map transfer, ZH FileTransfer.cpp, is not ported): an installed custom map as one byte string, so a game's
// host can send it to the players who do not have it (LAN: LANAPI's MAP_REQUEST / MAP_DATA; online: the lobby service's map store).
//
// A package is every file of the map's folder (the .map and its sidecars: map.ini, map.str, textures, ...), by their FolderArchiveFile key (lower case,
// '\' separators), sorted, in one canonical encoding:
//   "OBMP" u8 version (1) u32 count, then per file: u32 name length, the name, u32 size, the bytes (little-endian)
// Its id is the SHA-256 of those bytes (lower-case hex). The id names the map in the game setup (mapKey below), so two machines with the same files
// agree on the map's name, and a received package is installed only when its bytes hash to the id it was asked for.
//
// Limits (a package from another machine is untrusted): at most kMaxFiles files and kMaxBytes bytes, names of parts joined by '\' (printable, lower
// case, none of / : * ? " < > |, no empty, "." or ".." part, no Windows device name), exactly one .map file at the top level.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace MapPackage
{
constexpr std::uint32_t kMaxFiles = 512;
constexpr std::uint64_t kMaxBytes = 64ull << 20; ///< 64 MiB

struct File
{
	std::string name; ///< FolderArchiveFile key form
	std::vector<std::uint8_t> bytes;
};

// the canonical package of `files` (sorted by name here); false + *error when a file breaks the limits
bool encode(std::vector<File> files, std::vector<std::uint8_t> &out, std::string *error);
// the files of a package, checked whole (limits, order, no duplicate); false + *error otherwise
bool decode(const std::vector<std::uint8_t> &bytes, std::vector<File> &out, std::string *error);
// SHA-256 of the bytes, lower-case hex (64 characters)
std::string id(const std::vector<std::uint8_t> &bytes);
// the setup's map name of a package id: "maps/openbfme_map_<first 32 hex>/openbfme_map_<first 32 hex>.map" (the map cache key form)
std::string mapKey(const std::string &packageId);
// the 32 hex digits of a map key made by mapKey ("" for any other map: a retail or archive map, never transferred)
std::string keyDigest(const std::string &mapName);
// a package id fits a key's digest
bool idMatchesKey(const std::string &packageId, const std::string &mapName);
// the name of the package's .map file
std::string mapFileName(const std::vector<File> &files);
} // namespace MapPackage
