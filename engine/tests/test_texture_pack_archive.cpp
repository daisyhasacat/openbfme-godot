// OpenBFME unit tests: TextureOnlyArchiveFile (FolderArchiveFile.h), a mod used as a texture pack (mods.cfg [mods]
// textures_only) exposes only its texture folders. GPL-3.0.

#include "doctest.h"

#include "GameEngineDevice/Win32Device/Common/FolderArchiveFile.h"

#include <memory>

TEST_CASE("TextureOnlyArchiveFile: only art\\compiledtextures, art\\textures and art\\terrain files are listed and readable")
{
	auto inner = std::make_unique<MemoryArchiveFile>("pack");
	inner->put("art\\compiledtextures\\au\\auarnorstar.dds", {1});
	inner->put("Art/Textures/TRTrim.png", {2});
	inner->put("art\\terrain\\grass.tga", {3});
	inner->put("art\\w3d\\ci\\cine_gryrng.w3d", {4});
	inner->put("data\\ini\\gamedata.ini", {5});
	inner->put("asset.dat", {6});
	TextureOnlyArchiveFile pack(std::move(inner));
	CHECK(pack.fileCount() == 3);
	FilenameList listed;
	pack.getFileListInDirectory(std::string(), std::string(), "*", listed, true);
	CHECK(listed.size() == 3);
	CHECK(listed.count("art\\w3d\\ci\\cine_gryrng.w3d") == 0);
	std::vector<std::uint8_t> bytes;
	std::string error;
	CHECK(pack.readFile("art\\textures\\trtrim.png", bytes, &error));
	CHECK(bytes == std::vector<std::uint8_t>{2});
	CHECK(pack.getArchivedFileInfo("art\\compiledtextures\\au\\auarnorstar.dds") != nullptr);
	CHECK(pack.getArchivedFileInfo("data\\ini\\gamedata.ini") == nullptr);
	CHECK_FALSE(pack.readFile("data\\ini\\gamedata.ini", bytes, &error));
	CHECK(error.find("not a texture") != std::string::npos);
}
