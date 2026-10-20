// OpenBFME. GPL-3.0.

#include "GodotDevice/GodotRetailFileSystem.h"
#include "Common/BfmeGame.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "GameEngineDevice/Win32Device/Common/FolderArchiveFile.h"

#include "Common/RetailArchivePolicy.h"
#include "Common/ArchiveFile.h"
#include "Common/MD5.h"
#include "GameClient/MapUtil.h"
#include "GameClient/GameText.h"
#include <algorithm>
#include <filesystem>
#include <set>
#include <chrono>
#include <godot_cpp/classes/config_file.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include "GameClient/OptionPreferences.h"
#include <godot_cpp/classes/os.hpp>
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot
{

namespace
{
String toGodot(const std::string &s)
{
	return String::utf8(s.c_str(), (int64_t)s.size());
}

std::string toNative(const String &s)
{
	CharString utf8 = s.utf8();
	return std::string(utf8.get_data(), (size_t)utf8.length());
}
}

RetailFileSystem::RetailFileSystem() = default;
RetailFileSystem::~RetailFileSystem() = default;

String RetailFileSystem::content_signature() const
{
 String identity = m_bfme1 ? "bfme1" : "rotwk";
 for (const auto &archive : m_mountedArchives) identity += String::utf8((archive.canonicalPath + ":" + archive.md5 + "\n").c_str());
 return identity.sha256_text();
}

void RetailFileSystem::_bind_methods()
{
 ClassDB::bind_method(D_METHOD("install_library_map", "path", "root"), &RetailFileSystem::install_library_map, DEFVAL(String("user://installed-maps")));
 ClassDB::bind_method(D_METHOD("installed_maps", "root"), &RetailFileSystem::installed_maps, DEFVAL(String("user://installed-maps")));
 ClassDB::bind_method(D_METHOD("inspect_library_map", "path", "preview"), &RetailFileSystem::inspect_library_map, DEFVAL(false));
 ClassDB::bind_method(D_METHOD("content_signature"), &RetailFileSystem::content_signature);
	ClassDB::bind_method(D_METHOD("mount_bfme1_path", "root"), &RetailFileSystem::mount_bfme1_path);
	ClassDB::bind_method(D_METHOD("is_bfme1"), &RetailFileSystem::is_bfme1);
	ClassDB::bind_method(D_METHOD("mount_retail"), &RetailFileSystem::mount_retail);
	ClassDB::bind_method(D_METHOD("mount_aotr_path", "path"), &RetailFileSystem::mount_aotr_path);
	ClassDB::bind_method(D_METHOD("mount_retail_paths", "rotwk", "bfme2"), &RetailFileSystem::mount_retail_paths); // RELEASE-1
	ClassDB::bind_method(D_METHOD("create_bfme2_shell"), &RetailFileSystem::create_bfme2_shell);
	ClassDB::bind_method(D_METHOD("is_mounted"), &RetailFileSystem::is_mounted);
	ClassDB::bind_method(D_METHOD("is_shell_source"), &RetailFileSystem::is_shell_source);
	ClassDB::bind_method(D_METHOD("install_root"), &RetailFileSystem::install_root);
	ClassDB::bind_method(D_METHOD("file_exists", "path"), &RetailFileSystem::file_exists);
	ClassDB::bind_method(D_METHOD("read_file", "path"), &RetailFileSystem::read_file);
	ClassDB::bind_method(D_METHOD("get_archive_for_file", "path"), &RetailFileSystem::get_archive_for_file);
	ClassDB::bind_method(D_METHOD("get_last_error"), &RetailFileSystem::get_last_error);
	ClassDB::bind_method(D_METHOD("get_loose_files"), &RetailFileSystem::get_loose_files);
}

Dictionary RetailFileSystem::mount_retail()
{
	if (m_shellSource)
	{
		m_lastError = "a presentation-only BFME2 source cannot be mounted as a game file system";
		Dictionary result;
		PackedStringArray errors;
		errors.push_back(m_lastError);
		result["ok"] = false;
		result["errors"] = errors;
		return result;
	}
	PackedStringArray errors;
	std::string config = toNative(ProjectSettings::get_singleton()->globalize_path("user://install-paths.cfg"));
	std::string rotwkRoot;
	std::string bfme2Root;
	std::string error;
	if (!ResolveInstallPath("ROTWK_INSTALL", config, rotwkRoot, &error))
	{
		errors.push_back(toGodot(error));
	}
	if (!ResolveInstallPath("BFME2_INSTALL", config, bfme2Root, &error))
	{
		errors.push_back(toGodot(error));
	}
	return mountFrom(rotwkRoot, bfme2Root, errors);
}

Dictionary RetailFileSystem::mount_aotr_path(const String &path)
{
	PackedStringArray errors;
	const std::string config = toNative(ProjectSettings::get_singleton()->globalize_path("user://install-paths.cfg"));
	std::string rotwk, bfme2, error;
	if (path.is_empty()) errors.push_back("Choose the Age of the Ring installation folder.");
	if (!ResolveInstallPath("ROTWK_INSTALL", config, rotwk, &error)) errors.push_back(toGodot(error));
	if (!ResolveInstallPath("BFME2_INSTALL", config, bfme2, &error)) errors.push_back(toGodot(error));
	return mountFrom(rotwk, bfme2, errors, toNative(path));
}

Dictionary RetailFileSystem::mount_retail_paths(const String &rotwk, const String &bfme2)
{
	if (m_shellSource)
	{
		m_lastError = "a presentation-only BFME2 source cannot be mounted as a game file system";
		Dictionary result;
		PackedStringArray errors;
		errors.push_back(m_lastError);
		result["ok"] = false;
		result["errors"] = errors;
		return result;
	}
	return mountFrom(toNative(rotwk), toNative(bfme2), PackedStringArray());
}

Dictionary RetailFileSystem::mount_bfme1_path(const String &root)
{
	setBfme1Mounted(false);
 m_installedMaps.clear();m_installedLoaded=false;m_libraryText.reset();
	m_fs.reset();
	m_mounted = false;
	m_bfme1 = false;
	m_bfme1Root.clear();
	m_mountedArchives.clear();
	m_loose.clear();
	PackedStringArray errors;
	auto fs = std::make_unique<Win32BIGFileSystem>();
	const auto directory = std::filesystem::u8path(toNative(root));
	std::error_code ec;
	std::vector<std::string> names;
	PackedStringArray excluded; // community PR #36 review: reported, never silently skipped (PLAN rule 7)
	for (std::filesystem::recursive_directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec))
	{
		if (!it->is_regular_file()) continue;
		auto extension = it->path().extension().u8string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
		if (extension != ".big") continue;
		const auto filename = it->path().filename().u8string();
		auto relative = it->path().lexically_relative(directory).u8string();
		// the AIO launcher's generated graphics overrides ("!!!*.big") are not BFME1 1.03 data: left out, and named in the report
		if (filename.rfind("!!!", 0) == 0)
		{
			excluded.push_back(toGodot(relative));
			continue;
		}
		std::replace(relative.begin(), relative.end(), '/', '\\');
		names.push_back(relative);
	}
	if (ec) errors.push_back(toGodot("Cannot read BFME1 folder: " + ec.message()));
	if (names.empty()) errors.push_back("Choose a BFME1 installation containing BIG archives");
	// BFME1 Win32BIGFileSystem_loadBigFiles.cpp (0x009CDB90): descending
	// strcmp order, first mounted wins. Do not mix in BFME2/RotWK archives.
	std::vector<MountedArchive> mounted;
	Ref<ConfigFile> cache;
	cache.instantiate();
	cache->load("user://bfme1-archive-cache.cfg");
	int hashed = 0, cached = 0;
	for (const auto &name : BFMEArchiveLoadOrder(names))
	{
		if (!errors.is_empty()) break;
		auto relative = name;
		std::replace(relative.begin(), relative.end(), '\\', '/');
		const auto path = (directory / std::filesystem::u8path(relative)).u8string();
		std::string error;
		auto archive = fs->openArchiveFile(path, &error);
		if (!archive) { errors.push_back(toGodot(error)); break; }
		MountedArchive entry;
		entry.install = "bfme1";
		entry.canonicalPath = relative;
		entry.diskPath = path;
		entry.size = std::filesystem::file_size(std::filesystem::u8path(path), ec);
		const auto stamp = std::filesystem::last_write_time(std::filesystem::u8path(path), ec).time_since_epoch().count();
		const String key = toGodot(MD5::ofBytes(path.data(), path.size()));
		if (!ec && (int64_t)cache->get_value(key, "size", (int64_t)-1) == (int64_t)entry.size &&
			(int64_t)cache->get_value(key, "mtime", (int64_t)0) == stamp)
		{
			entry.md5 = toNative(cache->get_value(key, "md5", String()));
			++cached;
		}
		if (entry.md5.empty())
		{
			entry.md5 = MD5::ofFile(path, &error);
			++hashed;
			cache->set_value(key, "size", (int64_t)entry.size);
			cache->set_value(key, "mtime", (int64_t)stamp);
			cache->set_value(key, "md5", toGodot(entry.md5));
		}
		if (ec || entry.md5.empty()) { errors.push_back(toGodot("Cannot fingerprint " + path + ": " + error)); break; }
		mounted.push_back(entry);
		fs->mountArchive(std::move(archive), path, false);
	}
	for (const char *required : { "data/ini/gamedata.ini", "data/ini/playertemplate.ini", "maps/mapcache.ini", "MainMenu.apt" })
	{
		if (!fs->doesFileExist(required)) errors.push_back(toGodot(std::string("BFME1 installation is missing ") + required));
	}
	// Sol review r1: the archives must be BFME1's by the same test every gate uses (isBfme1Data: its legend without BFME2's expansion legend), so the
	// client paths (bfme1Mounted) and the parsing / simulation (the data) never select different games
	if (errors.is_empty() && !isBfme1Data(*fs))
	{
		errors.push_back("not BFME1 data: Data\\INI\\Default\\SubsystemLegend.ini without SubsystemLegendExpansion1.ini is required (a BFME2 / RotWK install mounts with mount_retail)");
	}
	// BFME1 stores these files below lang/<language>; its file-system search
	// exposes them at the root. Build a transient view of the same bytes so
	// all existing UI readers see the selected language consistently.
	if (errors.is_empty())
	{
		std::vector<std::pair<std::string, std::vector<std::uint8_t>>> languageFiles;
		for (const char *name : {"headertemplate.ini", "language.ini"})
		{
			if (fs->doesFileExist(name)) continue;
			std::vector<std::uint8_t> bytes;
			std::string error;
			if (!fs->readFile(std::string("lang/english/") + name, bytes, &error)) { errors.push_back(toGodot(error)); break; }
			languageFiles.emplace_back(name, std::move(bytes));
		}
		if (!languageFiles.empty() && errors.is_empty())
		{
			auto bytes = std::make_shared<std::vector<std::uint8_t>>();
			auto be32 = [&](std::uint32_t value) { for (int i = 3; i >= 0; --i) bytes->push_back((std::uint8_t)(value >> (i * 8))); };
			std::uint32_t header = 16, total = 16;
			for (const auto &file : languageFiles) header += 9 + file.first.size();
			total = header;
			for (const auto &file : languageFiles) total += file.second.size();
			bytes->insert(bytes->end(), {'B', 'I', 'G', 'F'});
			// BIG's length word is little-endian; directory words are big-endian.
			for (int i = 0; i < 4; ++i) bytes->push_back((std::uint8_t)(total >> (i * 8)));
			be32(languageFiles.size()); be32(header);
			std::uint32_t offset = header;
			for (const auto &file : languageFiles)
			{
				be32(offset); be32(file.second.size());
				bytes->insert(bytes->end(), file.first.begin(), file.first.end()); bytes->push_back(0);
				offset += file.second.size();
			}
			for (const auto &file : languageFiles) bytes->insert(bytes->end(), file.second.begin(), file.second.end());
			std::string error;
			auto view = fs->openArchiveFromMemory("bfme1-language-view.big", bytes, &error);
			if (!view) errors.push_back(toGodot(error));
			else fs->mountArchive(std::move(view), "bfme1-language-view.big", false);
		}
	}
	if (errors.is_empty())
	{
		m_fs = std::move(fs);
		m_mountedArchives = std::move(mounted);
		m_mounted = true;
		m_bfme1 = true;
		m_bfme1Root = toNative(root);
		setBfme1Mounted(true); // the client paths (Common/BfmeGame.h); the model / texture layout comes from the archives themselves
	}
	m_lastError = errors.is_empty() ? String() : String(errors[0]);
	Dictionary result;
	result["ok"] = m_mounted;
	result["errors"] = errors;
	result["game"] = "bfme1";
	result["excluded_archives"] = excluded;
	PackedStringArray stops;
	for (const std::string &l : bfme1StopLines())
	{
		stops.push_back(toGodot(l));
	}
	result["stops"] = stops; // S-5017
	result["archives_hashed"] = hashed;
	result["md5_cache_hits"] = cached;
	if (m_mounted) cache->save("user://bfme1-archive-cache.cfg");
	result["ui_theme"] = "bfme1";
	result["bfme1_install"] = root;
	return result;
}

Dictionary RetailFileSystem::mountFrom(const std::string &rotwkRoot, const std::string &bfme2Root, PackedStringArray errors, const std::string &aotr)
{
 m_installedMaps.clear();m_installedLoaded=false;m_libraryText.reset();
	Dictionary result;
	std::string error;
	setBfme1Mounted(false); // RotWK / BFME2
	m_fs.reset();
	m_mounted = false;
	m_bfme1 = false;
	m_bfme1Root.clear();
	m_mountedArchives.clear();
	m_loose.clear();
	m_shellSource = false;
	m_installRoot.clear();

	std::vector<RetailInstall> installs(2);
	installs[0].label = "rotwk";
	installs[0].root = rotwkRoot;
	installs[1].label = "bfme2";
	installs[1].root = bfme2Root;
	if (!RetailArchivePolicy::loadBuiltin("rotwk-201", installs[0].policy, &error))
	{
		errors.push_back(toGodot(error));
	}
	if (!RetailArchivePolicy::loadBuiltin("bfme2-106", installs[1].policy, &error))
	{
		errors.push_back(toGodot(error));
	}

	result["rotwk_install"] = toGodot(rotwkRoot);
	result["bfme2_install"] = toGodot(bfme2Root);

	if (errors.is_empty())
	{
		RetailMountOptions options;
		// md5 of every archive is always verified; the cache only makes repeat mounts fast.
		options.md5CachePath = toNative(ProjectSettings::get_singleton()->globalize_path("user://retail-md5-cache.tsv"));
		std::unique_ptr<Win32BIGFileSystem> fs = std::make_unique<Win32BIGFileSystem>();
		fs->setAotrProfile(!aotr.empty());
		const auto mountStart = std::chrono::steady_clock::now();
		auto elapsed = [&]() { return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - mountStart).count(); };
		RetailMountReport report = MountRetailArchives(*fs, installs, options);
		UtilityFunctions::print("STARTUP retail verification/index: ", elapsed(), " ms; hashed=", (int64_t)report.archivesHashed, "; cached=", (int64_t)report.md5CacheHits);
		Ref<ConfigFile> wallOptions; wallOptions.instantiate();
		wallOptions->load("res://dev/wall_balance.cfg");
		Ref<ConfigFile> savedWalls; savedWalls.instantiate();
		if (savedWalls->load("user://wall_balance.cfg") == OK) wallOptions = savedWalls;
		const bool wantsTowers = aotr.empty() && (bool)wallOptions->get_value("walls","new_towers",false);
		const bool hasTowerPack = FileAccess::file_exists("res://mods/wall_towers.big");
		if (report.ok && wantsTowers && !hasTowerPack)
		{
			report.warnings.push_back("Optional wall tower assets are not included; using native wall towers.");
		}
		// lane MODS-1: the enabled mods (user://mods.cfg [mods] enabled, in load order; RotWK's -mod "<folder>" as --mod=<folder> too)
		PackedStringArray modPaths;
		std::set<std::string> texturesOnly; // mods.cfg [mods] textures_only: mods mounted as texture packs (TextureOnlyArchiveFile)
		if (!aotr.empty())
		{
			const String dependency = toGodot(aotr).get_base_dir().path_join("rotwk/#aotr_patch202.big");
			modPaths.push_back(dependency);
			modPaths.push_back(toGodot(aotr));
		}
		else
		{
			Ref<ConfigFile> modsCfg;
			modsCfg.instantiate();
			if (modsCfg->load("user://mods.cfg") == OK)
			{
				const Array enabled = modsCfg->get_value("mods", "enabled", Array());
				for (int64_t i = 0; i < enabled.size(); ++i)
					modPaths.push_back(String(enabled[i]));
				const Array textures = modsCfg->get_value("mods", "textures_only", Array());
				for (int64_t i = 0; i < textures.size(); ++i)
					texturesOnly.insert(toNative(ProjectSettings::get_singleton()->globalize_path(String(textures[i]))));
			}
			const PackedStringArray modArgs = OS::get_singleton()->get_cmdline_user_args();
			for (int64_t i = 0; i < modArgs.size(); ++i)
				if (modArgs[i].begins_with("--mod="))
					modPaths.push_back(modArgs[i].substr(6));
		}
		// the BFME2Lua wall tower pack stacks under the enabled mods: they mount after it and win any file both provide
		if (report.ok && wantsTowers && hasTowerPack)
		{
			const String resource = "res://mods/wall_towers.big";
			String path = ProjectSettings::get_singleton()->globalize_path(resource);
			std::error_code nativePathError;
			if (!std::filesystem::is_regular_file(toNative(path), nativePathError))
			{
				path = ProjectSettings::get_singleton()->globalize_path("user://mods/wall_towers.big");
				DirAccess::make_dir_recursive_absolute(path.get_base_dir());
				const auto bytes = FileAccess::get_file_as_bytes(resource);
				Ref<FileAccess> output = FileAccess::open(path,FileAccess::WRITE);
				if (output.is_valid() && !bytes.is_empty()) {
					output->store_buffer(bytes);
					output->close(); // the native BIG reader must see a fully written physical file
				} else {
					report.ok = false;
					report.errors.push_back("BFME2Lua wall towers: could not extract packaged tower assets to " + toNative(path));
				}
			}
			std::string problem;
			auto archive = fs->openArchiveFile(toNative(path), &problem);
			if (archive) fs->mountArchive(std::move(archive),toNative(path),true);
			else { report.ok = false; report.errors.push_back("BFME2Lua wall towers: " + problem); }
		}
		// Optional local 2.02 multiplayer map pack, generated by tools/import_rotwk_202_maps.py.
		// Mount into both views: the lobby and the world must use the same map cache and bytes.
		const String mapsResource = "res://mods/rotwk_202_maps.big";
		if (report.ok && FileAccess::file_exists(mapsResource))
		{
			String path = ProjectSettings::get_singleton()->globalize_path(mapsResource);
			std::error_code pathError;
			if (!std::filesystem::is_regular_file(toNative(path), pathError))
			{
				path = ProjectSettings::get_singleton()->globalize_path("user://mods/rotwk_202_maps.big");
				DirAccess::make_dir_recursive_absolute(path.get_base_dir());
				const auto bytes = FileAccess::get_file_as_bytes(mapsResource);
				Ref<FileAccess> output = FileAccess::open(path, FileAccess::WRITE);
				if (output.is_valid() && !bytes.is_empty())
				{
					output->store_buffer(bytes);
					output->close();
				}
				else { report.ok = false; report.errors.push_back("2.02 maps: could not extract packaged map archive"); }
			}
			auto mountMaps = [&](Win32BIGFileSystem &target) {
				std::string problem;
				auto archive = target.openArchiveFile(toNative(path), &problem);
				if (archive) target.mountArchive(std::move(archive), toNative(path), true);
				else { report.ok = false; report.errors.push_back("2.02 maps: " + problem); }
			};
			if (report.ok) mountMaps(*fs);
		}
		// lane MODS-1: the enabled mods over both views: each mod's *.big, then its loose files on top (FolderArchiveFile). A later mod overrides
		// an earlier one.
		Array modsMounted;
		std::vector<std::string> modObjectInis; // the mods' Data\INI\Object\*.ini (a GameData block there is the game's: MergeGameDataBlocks)
		if (report.ok)
		{
			for (int64_t i = 0; i < modPaths.size() && report.ok; ++i)
			{
				const std::string modPath = toNative(ProjectSettings::get_singleton()->globalize_path(modPaths[i]));
				const bool textureOnly = texturesOnly.count(modPath) != 0;
				size_t textureFiles = 0;
				Dictionary mod;
				mod["path"] = toGodot(modPath);
				mod["textures_only"] = textureOnly;
				std::error_code dirError;
				std::vector<std::string> bigs;
				size_t looseFiles = 0, skipped = 0;
				const bool isFolder = std::filesystem::is_directory(std::filesystem::u8path(modPath), dirError);
				if (!isFolder)
					bigs.push_back(modPath); // a single .big mod
				std::string problem;
				std::unique_ptr<FolderArchiveFile> loose;
				if (isFolder)
				{
					loose = std::make_unique<FolderArchiveFile>();
					if (!loose->open(modPath, &problem))
					{
						report.ok = false;
						report.errors.push_back("mod " + modPath + ": " + problem);
						break;
					}
					bigs.assign(loose->bigFiles().rbegin(), loose->bigFiles().rend()); // ascending: the first in ZH's descending order is mounted last (wins)
					looseFiles = loose->fileCount();
					skipped = loose->skippedPrograms();
					for (const std::string &p : loose->paths())
						if (!textureOnly && p.rfind("data\\ini\\object\\", 0) == 0 && p.size() > 4 && p.compare(p.size() - 4, 4, ".ini") == 0)
							modObjectInis.push_back(p);
				}
				size_t emptyBigs = 0;
				for (const std::string &big : bigs)
				{
					std::error_code sizeError;
					if (std::filesystem::file_size(std::filesystem::u8path(big), sizeError) == 0 && !sizeError)
					{
						// an empty .big holds nothing (RJ RotWK ships art\test.big and art\w3d\ar\s.big at 0 bytes): reported, not mounted
						++emptyBigs;
						report.warnings.push_back("mod " + modPath + ": " + big + " is empty (0 bytes), not mounted");
						continue;
					}
					for (Win32BIGFileSystem *target : { fs.get() })
					{
						if (!target) continue;
						auto archive = target->openArchiveFile(big, &problem);
						if (!archive)
						{
							report.ok = false;
							report.errors.push_back("mod " + modPath + ": " + problem);
							break;
						}
						if (textureOnly)
						{
							auto textures = std::make_unique<TextureOnlyArchiveFile>(std::move(archive));
							textureFiles += textures->fileCount();
							target->mountArchive(std::move(textures), big, true);
						}
						else
							target->mountArchive(std::move(archive), big, true);
					}
				}
				if (loose && report.ok && textureOnly)
				{
					auto textures = std::make_unique<TextureOnlyArchiveFile>(std::move(loose));
					textureFiles += textures->fileCount();
					fs->mountArchive(std::move(textures), modPath, true);
				}
				else if (loose && report.ok)
				{
					fs->mountArchive(std::move(loose), modPath, true);

				}
				if (!aotr.empty() && i == 0 && report.ok)
				{
					// The installed AOTR runtime includes its own 2.02 dependency.
					// Keep that layer's macro source available before the AOTR overlay.
					std::vector<std::uint8_t> definitions;
					if (!fs->readFile("data\\ini\\gamedata.ini", definitions, &problem))
					{
						report.ok = false;
						report.errors.push_back("AOTR dependency GameData: " + problem);
					}
					else
					{
						auto source = std::make_unique<MemoryArchiveFile>("AOTR dependency macro source");
						source->put("data\\ini\\openbfme_aotr_dependency_gamedata.ini", definitions);
						fs->mountArchive(std::move(source), "AOTR dependency macro source", true);
					}
				}
				mod["big_files"] = (int64_t)(bigs.size() - emptyBigs);
				mod["empty_big_files"] = (int64_t)emptyBigs;
				mod["loose_files"] = (int64_t)looseFiles;
				mod["skipped_programs"] = (int64_t)skipped;
				mod["texture_files"] = (int64_t)textureFiles;
				modsMounted.push_back(mod);
				if (textureOnly)
					report.warnings.push_back("mod " + modPath + ": texture pack, " + std::to_string(textureFiles) +
						" texture file(s) override the verified game's (not verified; its models, INI, maps and UI are not mounted)");
				else
					report.warnings.push_back("mod " + modPath + ": " + std::to_string(bigs.size() - emptyBigs) + " archive(s) and " + std::to_string(looseFiles) +
					" loose file(s) override the verified game files (not verified; " + std::to_string(skipped) + " program file(s) not mounted)");
			}
		}
		// a mod's GameData block in an object INI (RJ RotWK): one merged Data\INI\GameData.ini over both views, so every GameData reader
		// sees the rows retail applies (its later object files override GameData.ini's rows)
		if (report.ok && !modObjectInis.empty())
		{
			std::sort(modObjectInis.begin(), modObjectInis.end());
			modObjectInis.erase(std::unique(modObjectInis.begin(), modObjectInis.end()), modObjectInis.end());
			std::vector<std::pair<std::string, std::string>> blocks;
			for (const std::string &p : modObjectInis)
			{
				std::vector<std::uint8_t> bytes;
				std::string readError;
				if (!fs->readFile(p, bytes, &readError))
					continue;
				std::string text(bytes.begin(), bytes.end());
				if (!GameDataBlockRows(text).empty())
					blocks.emplace_back(p, std::move(text));
			}
			if (!blocks.empty())
			{
				std::vector<std::uint8_t> base;
				std::string readError;
				fs->readFile("data\\ini\\gamedata.ini", base, &readError);
				const std::string merged = MergeGameDataBlocks(std::string(base.begin(), base.end()), blocks);
				std::string sources;
				for (const auto &b : blocks)
					sources += (sources.empty() ? "" : ", ") + b.first;
				for (Win32BIGFileSystem *target : { fs.get() })
				{
					if (!target) continue;
					auto mem = std::make_unique<MemoryArchiveFile>("mods: merged GameData");
					mem->put("data\\ini\\gamedata.ini", std::vector<std::uint8_t>(merged.begin(), merged.end()));
					target->mountArchive(std::move(mem), "mods: merged GameData", true);
				}
				report.warnings.push_back("mods: Data\\INI\\GameData.ini merged with the GameData block of " + sources + " (RotWK reads GameData from any INI)");
			}
		}
		result["mods"] = modsMounted;
		for (const std::string &e : report.errors)
		{
			errors.push_back(toGodot(e));
		}
		Array mounted;
		for (const MountedArchive &m : report.mounted)
		{
			Dictionary d;
			d["install"] = toGodot(m.install);
			d["path"] = toGodot(m.canonicalPath);
			d["disk_path"] = toGodot(m.diskPath);
			d["size"] = (int64_t)m.size;
			mounted.push_back(d);
		}
		PackedStringArray excluded;
		for (const std::string &e : report.excludedFound)
		{
			excluded.push_back(toGodot(e));
		}
		PackedStringArray warnings;
		for (const std::string &w : report.warnings)
		{
			warnings.push_back(toGodot(w));
			UtilityFunctions::push_warning("RetailFileSystem: ", toGodot(w));
		}
		result["warnings"] = warnings;
		result["archives_hashed"] = report.archivesHashed;
		result["md5_cache_hits"] = report.md5CacheHits;
		result["mounted"] = mounted;
		result["excluded_found"] = excluded;
		if (report.ok)
		{
			m_fs = std::move(fs);
			m_mounted = true;
			m_installRoot = bfme2Root;
			m_mountedArchives = report.mounted; // MP-1
			// PLAN rule 7 / stop S-039: loose map files in the install folders are contamination; they are
			// reported by the mount and by every map build, never read.
			const auto scanStart = elapsed();
			std::string scanError;
			if (!LooseFileScan::scanInstalls({ { "rotwk", rotwkRoot }, { "bfme2", bfme2Root } }, m_loose, &scanError))
			{
				errors.push_back(toGodot("loose file scan: " + scanError));
				m_fs.reset();
						m_mounted = false;
			}
			UtilityFunctions::print("STARTUP loose file scan: ", elapsed() - scanStart, " ms");
			for (const LooseFileFinding &f : m_loose)
			{
				UtilityFunctions::push_warning("RetailFileSystem: ", toGodot(f.line));
			}
		}
	}

	result["loose_files"] = get_loose_files();
	result["ok"] = m_mounted;
	result["errors"] = errors;
	m_lastError = errors.is_empty() ? String() : errors[0];
	for (int64_t i = 0; i < errors.size(); ++i)
	{
		UtilityFunctions::push_error("RetailFileSystem: ", errors[i]);
	}
	return result;
}

Ref<RetailFileSystem> RetailFileSystem::create_bfme2_shell()
{
	if (!m_mounted || m_shellSource)
	{
		m_lastError = m_shellSource ? "a presentation-only source cannot create another shell source" : "the normal retail file system is not mounted";
		return Ref<RetailFileSystem>();
	}
	if (m_installRoot.empty())
	{
		m_lastError = "the mounted retail file system has no BFME2 install root";
		return Ref<RetailFileSystem>();
	}
	Ref<RetailFileSystem> shell;
	shell.instantiate();
	const Dictionary report = shell->mountBfme2Shell(m_installRoot);
	if (!(bool)report.get("ok", false))
	{
		m_lastError = shell->get_last_error();
		return Ref<RetailFileSystem>();
	}
	return shell;
}

Dictionary RetailFileSystem::mountBfme2Shell(const std::string &bfme2Root)
{
	Dictionary result;
	PackedStringArray errors;
	m_fs.reset();
	m_mounted = false;
	m_shellSource = false;
	m_installRoot.clear();
	m_loose.clear();
	m_mountedArchives.clear();

	RetailInstall install;
	install.label = "bfme2";
	install.root = bfme2Root;
	std::string error;
	if (!RetailArchivePolicy::loadBuiltin("bfme2-106", install.policy, &error))
	{
		errors.push_back(toGodot(error));
	}
	result["bfme2_install"] = toGodot(bfme2Root);
	if (errors.is_empty())
	{
		RetailMountOptions options;
		options.md5CachePath = toNative(ProjectSettings::get_singleton()->globalize_path("user://retail-md5-cache.tsv"));
		auto fs = std::make_unique<Win32BIGFileSystem>();
		RetailMountReport report = MountRetailArchives(*fs, { install }, options);
		for (const std::string &e : report.errors)
		{
			errors.push_back(toGodot(e));
		}
		Array mounted;
		for (const MountedArchive &a : report.mounted)
		{
			Dictionary d;
			d["install"] = toGodot(a.install);
			d["path"] = toGodot(a.canonicalPath);
			d["disk_path"] = toGodot(a.diskPath);
			d["size"] = (int64_t)a.size;
			mounted.push_back(d);
		}
		result["mounted"] = mounted;
		result["archives_hashed"] = report.archivesHashed;
		result["md5_cache_hits"] = report.md5CacheHits;
		PackedStringArray excluded;
		for (const std::string &e : report.excludedFound)
		{
			excluded.push_back(toGodot(e));
		}
		result["excluded_found"] = excluded;
		PackedStringArray warnings;
		for (const std::string &w : report.warnings)
		{
			warnings.push_back(toGodot(w));
			UtilityFunctions::push_warning("RetailFileSystem: ", toGodot(w));
		}
		result["warnings"] = warnings;
		if (report.ok)
		{
			std::string scanError;
			if (!LooseFileScan::scanInstalls({ { "bfme2", bfme2Root } }, m_loose, &scanError))
			{
				errors.push_back(toGodot("loose file scan: " + scanError));
			}
			if (errors.is_empty())
			{
				m_fs = std::move(fs);
				m_mounted = true;
				m_shellSource = true;
				m_installRoot = bfme2Root;
				m_mountedArchives = report.mounted;
			}
		}
	}
	result["loose_files"] = get_loose_files();
	result["ok"] = m_mounted;
	result["errors"] = errors;
	m_lastError = errors.is_empty() ? String() : String(errors[0]);
	for (int64_t i = 0; i < errors.size(); ++i)
	{
		UtilityFunctions::push_error("RetailFileSystem: ", errors[i]);
	}
	return result;
}

String RetailFileSystem::install_root() const
{
	return toGodot(m_installRoot);
}

ArchiveFileSystem *RetailFileSystem::archive_fs() const
{
	return m_fs.get();
}

ArchiveFileSystem *RetailFileSystem::menu_archive_fs() const
{
	return m_fs.get(); // compatibility accessor; presentation uses its own RetailFileSystem source
}

Array RetailFileSystem::get_loose_files() const
{
	Array out;
	for (const LooseFileFinding &f : m_loose)
	{
		Dictionary d;
		d["install"] = toGodot(f.install);
		d["path"] = toGodot(f.file.relativePath);
		d["size"] = (int64_t)f.file.size;
		d["line"] = toGodot(f.line);
		out.push_back(d);
	}
	return out;
}

bool RetailFileSystem::existsNative(const std::string &path) const
{
	return m_fs && m_fs->doesFileExist(path);
}

ArchiveFileSystem *RetailFileSystem::archive()
{
	return m_fs.get();
}

bool RetailFileSystem::readBytes(const std::string &path, std::vector<uint8_t> &out, std::string *error)
{
	if (!m_fs)
	{
		if (error)
		{
			*error = "retail archives are not mounted";
		}
		return false;
	}
	return m_fs->readFile(path, out, error);
}

bool RetailFileSystem::file_exists(const String &path) const
{
	return existsNative(toNative(path));
}

String RetailFileSystem::get_archive_for_file(const String &path) const
{
	if (!m_fs)
	{
		return String();
	}
	return toGodot(m_fs->getArchiveFilenameForFile(toNative(path)));
}

PackedByteArray RetailFileSystem::read_file(const String &path)
{
	PackedByteArray bytes;
	std::vector<uint8_t> data;
	std::string error;
	if (!readBytes(toNative(path), data, &error))
	{
		m_lastError = toGodot(error);
		UtilityFunctions::push_error("RetailFileSystem: ", m_lastError);
		return bytes;
	}
	bytes.resize((int64_t)data.size());
	if (!data.empty())
	{
		memcpy(bytes.ptrw(), data.data(), data.size());
	}
	return bytes;
}

} // namespace godot

// OpenBFME browser feature: an isolated virtual name prevents library files from
// shadowing installed maps or entering the lobby's MapCache-based playable list.
namespace {
class LibraryPreviewArchive : public ArchiveFile {
public:
 FolderArchiveFile folder;
 std::map<std::string, std::string> source;
 std::string identity;
 bool readFile(const std::string &path, std::vector<uint8_t> &out, std::string *error) override {
  auto it=source.find(FolderArchiveFile::key(path));
  if(it==source.end()) { if(error)*error="Library preview file is absent: "+path; return false; }
  return folder.readFile(it->second,out,error);
 }
 std::string getName() const override { return identity; }
 std::string getPath() const override { return folder.getPath(); }
 bool isIdentityEnforced() const override { return false; }
};
}

godot::Dictionary godot::RetailFileSystem::inspect_library_map(const String &path, bool preview)
{
 Dictionary result; result["ok"]=false;
 auto fail=[&](const std::string &error) { result["error"]=toGodot(error); return result; };
 if(!m_fs)return fail("Retail archives are not mounted");
 auto file=FileAccess::open(path,FileAccess::READ);
 if(file.is_null())return fail("Cannot read library map: "+toNative(path));
 auto data=file->get_buffer(file->get_length());
 std::vector<uint8_t> bytes(data.ptr(),data.ptr()+data.size());
 LoadedMap map; std::string error;
 if(!MapReader::load(bytes,toNative(path),MapReadOptions(),map,&error))return fail(error);
 // Include sidecar modification times in the alias, so feature caches also expire
 // when a library map's own INI or assets change.
 auto archive=std::make_unique<LibraryPreviewArchive>();
 if(!archive->folder.open(toNative(path.get_base_dir()),&error))return fail(error);
 String identity=path+String(":")+FileAccess::get_sha256(path);
 for(const auto &source:archive->folder.paths()) {
  String disk=path.get_base_dir().path_join(toGodot(source).replace("\\","/"));
  identity+=toGodot(source)+String::num_uint64(FileAccess::get_modified_time(disk));
 }
 String alias="openbfme_library_"+identity.sha256_text();
 result["key"]=String("maps/")+alias+String("/")+alias+String(".map");
 result["preview_name"]=alias; result["library"]=true; result["official"]=false;
 // WorldInfo keys: BFME2 MapUtil.cpp / MapCache::addMap. Strings may be
 // literal Unicode/Windows-1252 or labels in this map's map.str / game text.
 GameTextTable localText; std::vector<uint8_t> strings;
 if(archive->folder.readFile("map.str",strings,&error) && !localText.parse(strings,&error))return fail("map.str: "+error);
 auto mapText=[&](const char *key) -> String {
  const auto *value=map.chunks.worldInfo.find(key);
  if(!value)return String();
  String raw=value->type==Dict::DICT_UNICODESTRING ? String::utf16(reinterpret_cast<const char16_t *>(value->u.data()),value->u.size()) : toGodot(Windows1252ToUtf8(value->s));
  std::string label=toNative(raw.begins_with("$")?raw.substr(1):raw),text;
  if(localText.lookup(label,text))return toGodot(text);
  if(raw.begins_with("$") || raw.contains(":")) {
   if(!m_libraryText) { m_libraryText=std::make_unique<GameTextTable>(); if(!m_libraryText->loadRetail(*m_fs,&error)) { m_libraryText.reset();return raw; } }
   if(m_libraryText->lookup(label,text))return toGodot(text);
  }
  return raw;
 };
 result["name"]=mapText("mapName"); result["description"]=mapText("mapDescription");
 result["snowy"]=map.chunks.hasWorldInfo && map.chunks.worldInfo.getInt("weather")==1;
 result["content_id"]=FileAccess::get_sha256(path); result["file_size"]=data.size();
 result["path"]=path;
 Array starts;
 for(const auto &object:map.chunks.objects) {
  if(!object.isWaypoint())continue;
  for(int i=1;i<=32;++i)if(object.getWaypointName()=="Player_"+std::to_string(i)+"_Start") {
   Dictionary start; start["index"]=i-1; start["x"]=object.m_location.x; start["y"]=object.m_location.y; start["z"]=object.m_location.z; starts.push_back(start);
  }
 }
 result["starts"]=starts;result["players"]=starts.size();
 if(preview) {
  if(!m_fs)return fail("Retail archives are not mounted");
  const std::string prefix="maps\\"+toNative(alias)+"\\";
  if(!m_fs->doesFileExist(prefix+toNative(alias)+".map")) {
   archive->identity=toNative(alias);
   for(const auto &source:archive->folder.paths()) {
    // A neighboring map must not replace the selected .map.
    std::string target=source==FolderArchiveFile::key(toNative(path.get_file())) ? toNative(alias)+".map" : source;
    std::string dest=prefix+target;
    archive->source[dest]=source;
    ArchivedFileInfo info; info.m_filename=target.substr(target.find_last_of('\\')+1);
    auto sep=dest.find_last_of('\\'); archive->addFile(dest.substr(0,sep+1),&info);
   }
   m_fs->mountArchive(std::move(archive),toNative(alias),false);
  }
 }
 result["ok"]=true;return result;
}

// OpenBFME installs live in user data, never in either verified game install.
// Copy into a new staging directory, validate it, then rename before publishing.
godot::Dictionary godot::RetailFileSystem::install_library_map(const String &path, const String &root)
{
 Dictionary result;result["ok"]=false;
 auto fail=[&](const String &error){result["error"]=error;return result;};
 auto inspected=inspect_library_map(path,false);
 if(!bool(inspected.get("ok",false)))return inspected;
 if(int(inspected["players"])<2)return fail("This map has fewer than two player starts.");
 // Load existing imports before adding this one, avoiding a duplicate on the first catalog read.
 installed_maps(root);
 String installRoot=m_bfme1 && root=="user://installed-maps" ? String("user://bfme1-installed-maps") : root;
 String destination=ProjectSettings::get_singleton()->globalize_path(installRoot).path_join(String(inspected["content_id"]));
 String target=destination.path_join(path.get_file());
 std::error_code ec;
 if(!FileAccess::file_exists(target) && !FileAccess::file_exists(destination.path_join(path.get_file().to_lower()))) {
  const std::string staging=toNative(destination)+".installing";
  std::filesystem::create_directories(std::filesystem::u8path(staging),ec);
  if(ec)return fail(toGodot(ec.message()));
  FolderArchiveFile folder;std::string error;
  if(!folder.open(toNative(path.get_base_dir()),&error))return fail(toGodot(error));
  for(const auto &source:folder.paths()) {
   String rel=toGodot(source).replace("\\","/");
   if(rel.get_extension().to_lower()=="map" && rel!=path.get_file().to_lower())continue;
   // Resolve the real filename case on case-sensitive disks through the archive.
   std::vector<uint8_t> bytes;
   if(!folder.readFile(source,bytes,&error))return fail(toGodot(error));
   String out=toGodot(staging).path_join(rel);
   if(DirAccess::make_dir_recursive_absolute(out.get_base_dir())!=OK)return fail("Cannot create installed map directory.");
   auto file=FileAccess::open(out,FileAccess::WRITE);
   if(file.is_null())return fail("Cannot write installed map file.");
   PackedByteArray buffer;buffer.resize(bytes.size());if(!bytes.empty())memcpy(buffer.ptrw(),bytes.data(),bytes.size());file->store_buffer(buffer);
  }
  // FolderArchiveFile normalizes names to lower case.
  target=destination.path_join(path.get_file().to_lower());
  auto staged=inspect_library_map(toGodot(staging).path_join(path.get_file().to_lower()),false);
  if(!bool(staged.get("ok",false)))return staged;
  std::filesystem::rename(std::filesystem::u8path(staging),std::filesystem::u8path(toNative(destination)),ec);
  if(ec)return fail(toGodot(ec.message()));
 }
 if(!FileAccess::file_exists(target))target=destination.path_join(path.get_file().to_lower());
 result=inspect_library_map(target,true);
 if(!bool(result.get("ok",false)))return result;
 result["library"]=false;result["installed"]=true;
 for(int64_t i=0;i<m_installedMaps.size();++i)if(String(Dictionary(m_installedMaps[i])["key"])==String(result["key"]))return result;
 m_installedMaps.push_back(result);return result;
}

godot::Array godot::RetailFileSystem::installed_maps(const String &directory)
{
 if(!m_installedLoaded) {
  m_installedLoaded=true;
  String installRoot=m_bfme1 && directory=="user://installed-maps" ? String("user://bfme1-installed-maps") : directory;
  String root=ProjectSettings::get_singleton()->globalize_path(installRoot);
  auto dir=DirAccess::open(root);
  if(dir.is_valid())for(const String &folder:dir->get_directories()) {
   if(folder.ends_with(".installing"))continue;
   auto files=DirAccess::open(root.path_join(folder));if(files.is_null())continue;
   for(const String &name:files->get_files())if(name.get_extension().to_lower()=="map") {
    auto entry=inspect_library_map(root.path_join(folder).path_join(name),true);
    if(bool(entry.get("ok",false))) { entry["library"]=false;entry["installed"]=true;m_installedMaps.push_back(entry); }
    else UtilityFunctions::push_warning("Installed map: ",entry.get("error",String()));
   }
  }
 }
 return m_installedMaps;
}

void godot::RetailFileSystem::append_installed_maps(std::vector<MapCacheEntry> &maps)
{
 for(const Variant &value:installed_maps()) {
  Dictionary d=value;MapCacheEntry entry;
  entry.name=toNative(String(d["key"]));entry.rawName=entry.name;
  auto name=String(d["name"]).utf16();entry.displayName.assign(name.get_data(),name.length());
  if(entry.displayName.empty())entry.displayName=u"Untitled map";
  auto description=String(d["description"]).utf16();entry.description.assign(description.get_data(),description.length());
  entry.numPlayers=int(d["players"]);entry.isMultiplayer=entry.numPlayers>=2;
  entry.fileSize=int64_t(d.get("file_size",0));
  Array starts=d["starts"];
  for(const Variant &start:starts) {Dictionary p=start;Coord3D point;point.x=double(p["x"]);point.y=double(p["y"]);point.z=double(p["z"]);entry.startPositions[int(p["index"])+1]=point;}
  auto found=std::find_if(maps.begin(),maps.end(),[&](const MapCacheEntry &map){return map.name==entry.name;});
  if(found==maps.end())maps.push_back(std::move(entry));else *found=std::move(entry);
 }
}
