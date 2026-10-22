// OpenBFME. GPL-3.0.
//
// UserDataFolder (lane CAH-2): where RotWK keeps the player's own files (Options.ini's folder, the Create-a-Hero saves), read from the install's gi.dat.
// Client code: no simulation state.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * RW 0xAAA6C0 reads "gi.dat" (RW 0xCFE5F4) from the install folder: the 32-bit magic 0x20204947 ("GI  "), a 32-bit count, then count pairs of
//     NUL-terminated strings (key, value); RW 0xAAA5A0 keeps the value of the keys SkuName, GameName, GameRegPath, InstallerRegPath, OnlineServer,
//     UserDataLeafName, G1 .. G4 (compared with _strcmpi) and ignores any other key. A missing or bad file leaves every value "GI_DAT_ERROR"
//     (RW 0xDC1340 ..);
//   * the user data folder (GlobalData + 0x1240, set by RW 0x644148): SHGetSpecialFolderPathW(0, buf, CSIDL_APPDATA 0x1A, create 1), a '\' appended when
//     missing, the leaf name, '\' again, CreateDirectoryW. The leaf name (RW 0x64126F) is the registry value "UserDataLeafName" under GameRegPath
//     (RW 0x641160 -> RW 0x64148E: HKLM, then HKCU) and, when there is none, gi.dat's UserDataLeafName (RW 0xAAA8E0); RotWK 2.01's gi.dat holds
//     "My The Lord of the Rings, The Rise of the Witch-king Files";
//   * the Create-a-Hero saves are "MyHero*.cah" in the user data folder + "Save\" (RW 0x6DD398 with L"Save\" RW 0xC1A320; CreateAHeroHeroList.h);
//   * lane MOD-5: the user maps folder (MapCache::getUserMapDir, RW 0x70254F) is "UserData\Maps" (RW 0xC1D9A8, relative to the working folder) when
//     RW 0x6417DC is true, else the user data folder (TheGlobalData's getPath_UserData, RW 0x6427AA) + MapCache::getMapDir()'s "Maps" (RW 0x701C6D,
//     RW 0xC1D964), with no separator at the end. RW 0x6417DC reads the registry DWORD "UseLocalUserMaps" under GameRegPath (RW 0x64159D: HKLM, then
//     HKCU) once (RW 0xD9F540) and counts a missing value as 1; the installers write 0 (docs/AIO.md), so an installed game keeps its user maps
//     in the user data folder.
// OpenBFME DIFFERENCES (stop S-1407): the registry value is not read (the leaf name is gi.dat's; the installer writes the same text); a missing or bad gi.dat
// is an error (retail's "GI_DAT_ERROR" folder is not reproduced); the application data folder is the device's (Godot OS::get_data_dir: %APPDATA% on
// Windows as CSIDL_APPDATA, the XDG data folder on Linux). Stop S-5160 (lane MOD-5): UseLocalUserMaps is not read either; the user maps folder is
// always the user data folder's, as the installers' 0 makes it.

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace UserDataFolder
{

// RW 0xAAA6C0 / 0xAAA5A0: the known keys of a gi.dat image (key spelling as the file has it, matched without case). False + *error for a bad image.
bool parseGameInfo(const std::vector<std::uint8_t> &bytes, std::map<std::string, std::string> &values, std::string *error);
// the UserDataLeafName of a gi.dat image ("" + *error when the image is bad or has none)
std::string leafName(const std::vector<std::uint8_t> &bytes, std::string *error);
// RW 0x644148: appData + separator + leaf + separator (`separator` '\\' as retail; the device passes '/')
std::string userDataFolder(const std::string &appData, const std::string &leaf, char separator = '\\');
// The user data folder of an install: the leaf name of <installRoot>/gi.dat (RW 0xAAA6C0) under appData (RW 0x644148). "" + *error when the
// file cannot be read or names no leaf; *leaf (when given) gets the leaf name.
std::string userDataFolderOfInstall(const std::string &installRoot, const std::string &appData, char separator, std::string *leaf, std::string *error);
// RW 0x6DD398: the Create-a-Hero save folder, the user data folder + "Save" + separator
std::string heroSaveFolder(const std::string &userData, char separator = '\\');
// RW 0x70254F (lane MOD-5): the user maps folder, the user data folder + "Maps" (no separator at the end, as retail's); a user map is
// <folder>\<name>\<name>.map
std::string userMapFolder(const std::string &userData);
// Stop S-5160 (lane MOD-5): what of retail's user maps is not ported. Every build of a user map lists it in its report's stops.
constexpr const char *kUserMapStop = "S-5160";

} // namespace UserDataFolder
