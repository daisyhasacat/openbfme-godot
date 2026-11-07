// OpenBFME unit tests. GPL-3.0.
// Lane MOD-5: user maps. The user maps folder (UserDataFolder::userMapFolder, RW 0x70254F), the archive file system's local folder (RW 0xA149A2's
// local step) and a retail map copied into a user maps folder, which loads as its archive copy does.

#include "doctest.h"
#include "RetailTestMount.h"

#include "Common/AsciiString.h"
#include "GameClient/MapClassification.h"
#include "GameClient/MapCreationHooks.h"
#include "GameClient/MapObjectDrawables.h"
#include "GameClient/MapWeather.h"
#include "GameClient/UserDataFolder.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/RetailObjectWorld.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
namespace stdfs = std::filesystem;

// a fresh folder under the system's temporary folder, removed when the test ends
struct TempFolder
{
	stdfs::path path;
	TempFolder()
	{
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		path = stdfs::temp_directory_path() / ("openbfme-mod5-" + std::to_string(stamp));
		stdfs::create_directories(path);
	}
	~TempFolder()
	{
		std::error_code ec;
		stdfs::remove_all(path, ec);
	}
};

void writeFile(const stdfs::path &path, const std::vector<std::uint8_t> &bytes)
{
	stdfs::create_directories(path.parent_path());
	std::ofstream out(path, std::ios::binary);
	out.write((const char *)bytes.data(), (std::streamsize)bytes.size());
}

std::vector<std::uint8_t> bytesOf(const std::string &s)
{
	return std::vector<std::uint8_t>(s.begin(), s.end());
}

// sets an archive file system's local folder for one test and takes it away again
struct LocalFolderScope
{
	ArchiveFileSystem &archives;
	LocalFolderScope(ArchiveFileSystem &a, const std::string &folder) : archives(a) { archives.setLocalFolder(folder); }
	~LocalFolderScope() { archives.setLocalFolder(std::string()); }
};
} // namespace

TEST_CASE("mod5 user maps: the folder is the user data folder + \"Maps\" (RW 0x70254F), a map is <folder>\\<name>\\<name>.map")
{
	CHECK(UserDataFolder::userMapFolder(UserDataFolder::userDataFolder("D:\\AppData\\Roaming", "Leaf")) == "D:\\AppData\\Roaming\\Leaf\\Maps");
	CHECK(UserDataFolder::userMapFolder(UserDataFolder::userDataFolder("/data/share", "Leaf", '/')) == "/data/share/Leaf/Maps");
	CHECK(MapClassification::mapPath("my map") == "maps\\my map\\my map.map");
	CHECK(MapClassification::mapDirectory("my map", "/data/share/Leaf/Maps") == "/data/share/Leaf/Maps\\my map");
	CHECK(MapClassification::mapPath("my map", "/data/share/Leaf/Maps") == "/data/share/Leaf/Maps\\my map\\my map.map");
	CHECK(std::string(UserDataFolder::kUserMapStop) == "S-5160");

	// the user data folder of an install, from its gi.dat (RW 0xAAA6C0): "GI  ", a count, key / value strings
	TempFolder tmp;
	const std::string giDat = std::string("GI  ") + std::string("\x01\x00\x00\x00", 4) + std::string("UserDataLeafName\0My Files\0", 26);
	writeFile(tmp.path / "gi.dat", bytesOf(giDat));
	std::string leaf, error;
	CHECK(UserDataFolder::userDataFolderOfInstall(tmp.path.u8string(), "/data/share", '/', &leaf, &error) == "/data/share/My Files/");
	CHECK(leaf == "My Files");
	CHECK(UserDataFolder::userDataFolderOfInstall((tmp.path / "none").u8string(), "/data/share", '/', nullptr, &error).empty());
	CHECK(error.rfind("cannot read ", 0) == 0);

	// the stop register has the stop every user map build reports
	std::ifstream in(std::string(OPENBFME_DOCS_DIR) + "/STOPS.md");
	REQUIRE(static_cast<bool>(in));
	std::stringstream ss;
	ss << in.rdbuf();
	CHECK(ss.str().find(std::string("| ") + UserDataFolder::kUserMapStop + " | code | User maps (MOD-5) | ") != std::string::npos);
}

TEST_CASE("mod5 local folder: only files under it are read from disk, without case and either separator; nothing outside it, no relative path")
{
	TempFolder tmp;
	writeFile(tmp.path / "Leaf" / "Maps" / "My Map" / "My Map.map", bytesOf("map bytes"));
	writeFile(tmp.path / "Leaf" / "Maps" / "My Map" / "map.ini", bytesOf("Weather\nEnd\n"));
	writeFile(tmp.path / "Leaf" / "MapsAside" / "x" / "x.map", bytesOf("aside"));
	writeFile(tmp.path / "Leaf" / "Options.ini", bytesOf("options"));
	const std::string folder = (tmp.path / "Leaf" / "Maps").u8string();
	const std::string map = folder + "\\my map\\MY MAP.map";

	Win32BIGFileSystem archives; // no archives: every read below is the local step's
	std::vector<std::uint8_t> out;
	std::string error;
	// no local folder: nothing is read from disk (the archives' message as before)
	CHECK_FALSE(archives.doesFileExist(map));
	CHECK_FALSE(archives.readFile(map, out, &error));
	CHECK(error == "file not found in any mounted archive: " + map);

	archives.setLocalFolder(folder + "/");
	CHECK(archives.localFolder() == folder); // the separator at the end is dropped
	CHECK(archives.doesFileExist(map));
	REQUIRE_MESSAGE(archives.readFile(map, out, &error), error);
	CHECK(out == bytesOf("map bytes"));
	const std::string slashes = folder + "/My Map/map.ini";
	CHECK(archives.readFile(slashes, out, &error));
	CHECK(out == bytesOf("Weather\nEnd\n"));
	// the folder compares without case too
	CHECK(archives.doesFileExist(AsciiStringUtil::lowered(folder) + "\\my map\\my map.map"));

	// nothing outside the folder: a parent step, a sibling folder that starts with its name, the user data folder itself, a folder, a drive
	// (joined on Windows, "D:" would replace the whole path) or a stream ("x.map:s") as a part
	CHECK_FALSE(archives.doesFileExist(folder + "\\..\\Options.ini"));
	CHECK_FALSE(archives.doesFileExist(folder + "Aside\\x\\x.map"));
	CHECK_FALSE(archives.doesFileExist((tmp.path / "Leaf" / "Options.ini").u8string()));
	CHECK_FALSE(archives.doesFileExist(folder + "\\my map"));
	CHECK_FALSE(archives.doesFileExist(folder));
	CHECK_FALSE(archives.doesFileExist(folder + "\\my map\\" + tmp.path.root_name().u8string() + "\\" + (tmp.path / "Leaf" / "Options.ini").relative_path().u8string()));
	CHECK_FALSE(archives.doesFileExist(folder + "\\my map\\my map.map:s"));
	// a relative path is the archives' alone (loose files in the install folders stay refused: PLAN rule 7, S-039)
	CHECK_FALSE(archives.doesFileExist("maps\\my map\\my map.map"));
	// a missing file under the folder names the folder
	CHECK_FALSE(archives.readFile(folder + "\\my map\\solo.ini", out, &error));
	CHECK(error == "file not found in any mounted archive or in " + folder + ": " + folder + "\\my map\\solo.ini");

	archives.setLocalFolder(std::string());
	CHECK_FALSE(archives.doesFileExist(map));
}

// A retail map and its folder's files copied into a user maps folder under another name: the map, its map.ini (15 object definitions on Carn Dum)
// and its weather come from disk, and the map classifies exactly as its archive copy.
TEST_CASE("mod5 user maps retail: a retail map copied into the user maps folder loads like its archive copy")
{
	retailtest::Mount *m = retailtest::pureMount();
	if (!m)
	{
		retailtest::printSkip("mod5 user maps retail");
		return;
	}
	REQUIRE_MESSAGE(m->fs != nullptr, m->error);
	ArchiveFileSystem &archives = *m->fs;
	const std::string retailName = "map ang carn dum";
	const std::string userName = "mod5 user copy";
	const std::string retailDir = MapClassification::mapDirectory(retailName);

	TempFolder tmp;
	const std::string folder = (tmp.path / "Maps").u8string();
	FilenameList files;
	archives.getFileListInDirectory("", retailDir + "\\", "*", files, true); // every file of the map's folder (it has no subfolder)
	std::string error;
	bool copiedMap = false, copiedMapIni = false;
	for (const std::string &file : files)
	{
		std::vector<std::uint8_t> bytes;
		REQUIRE_MESSAGE(archives.readFile(file, bytes, &error), error);
		std::string leaf = AsciiStringUtil::lowered(file.substr(retailDir.size() + 1));
		REQUIRE(leaf.find_first_of("\\/") == std::string::npos);
		copiedMapIni = copiedMapIni || leaf == "map.ini";
		if (leaf == retailName + ".map")
		{
			leaf = userName + ".map";
			copiedMap = true;
		}
		writeFile(tmp.path / "Maps" / userName / stdfs::u8path(leaf), bytes);
	}
	REQUIRE(copiedMap);
	REQUIRE(copiedMapIni);

	LocalFolderScope scope(archives, folder);
	const std::string userDir = MapClassification::mapDirectory(userName, folder);
	std::vector<std::uint8_t> retailBytes, userBytes;
	REQUIRE_MESSAGE(archives.readFile(MapClassification::mapPath(retailName), retailBytes, &error), error);
	REQUIRE_MESSAGE(archives.readFile(MapClassification::mapPath(userName, folder), userBytes, &error), error);
	CHECK(userBytes == retailBytes);
	CHECK(archives.getArchiveFilenameForFile(MapClassification::mapPath(userName, folder)).empty()); // read from disk, not an archive
	CHECK(archives.doesFileExist(userDir + "\\map.ini"));
	// the archives' file lists do not include the local folder (stop S-5160)
	FilenameList listed;
	archives.getFileListInDirectory("", userDir, "*", listed, true);
	CHECK(listed.empty());

	// the weather of the map.ini (MapWeatherScan takes the map's folder)
	MapWeather retailWeather, userWeather;
	REQUIRE_MESSAGE(MapWeatherScan::load(archives, retailDir, retailWeather, &error), error);
	REQUIRE_MESSAGE(MapWeatherScan::load(archives, userDir, userWeather, &error), error);
	CHECK(userWeather.fogKeysSeen == retailWeather.fogKeysSeen);
	CHECK(userWeather.fogStart == retailWeather.fogStart);
	CHECK(userWeather.fogEnd == retailWeather.fogEnd);

	// the whole classification: map -> terrain -> map.ini -> objects, once from the archives and once from the user maps folder
	RetailObjectWorld world(archives);
	REQUIRE_MESSAGE(world.load(&error), error);
	MapObjectOptions options;
	REQUIRE_MESSAGE(MapObjectGameData::load(archives, options, &error), error);
	REQUIRE_MESSAGE(MapObjectGameData::loadPlayerTemplates(archives, options, &error), error);
	REQUIRE_MESSAGE(MapCreationHooks::load(archives, options.creationScripts, &error), error);
	auto classify = [&](const std::string &name, const std::string &mapsFolder, const std::vector<std::uint8_t> &bytes, MapClassification::Result &cr,
						MapObjectDrawables &classified) {
		LoadedMap map;
		TerrainLogic terrain;
		REQUIRE_MESSAGE(MapClassification::run(bytes, MapClassification::mapPath(name, mapsFolder), name, world, options, map, terrain, classified, nullptr,
							cr, &error, mapsFolder),
			error);
	};
	MapClassification::Result retailResult, userResult;
	MapObjectDrawables retailObjects, userObjects;
	classify(retailName, "maps", retailBytes, retailResult, retailObjects);
	classify(userName, folder, userBytes, userResult, userObjects);
	CHECK(retailResult.mapIni.mapIniFound);
	CHECK(userResult.mapIni.mapIniFound);
	CHECK(userResult.mapIni.soloIniFound == retailResult.mapIni.soloIniFound);
	CHECK(retailResult.mapIni.definitions.size() == 15);
	CHECK(userResult.mapIni.definitions == retailResult.mapIni.definitions);
	CHECK(userResult.mapIni.errors == retailResult.mapIni.errors);
	CHECK(userResult.fullObjects == retailResult.fullObjects);
	CHECK(userResult.clientOnly == retailResult.clientOnly);
	REQUIRE(userObjects.drawables.size() == retailObjects.drawables.size());
	for (size_t i = 0; i < userObjects.drawables.size(); ++i)
	{
		CHECK(userObjects.drawables[i].fate == retailObjects.drawables[i].fate);
	}
}
