// OpenBFME. GPL-3.0.
//
// Device layer: exposes the retail archive file system to Godot. Godot never reads retail
// files itself; everything goes through the SAGE-port ArchiveFileSystem.

#pragma once

#include "Common/LooseFileScan.h"
#include "Common/RetailArchivePolicy.h"
#include "GameClient/MapCache.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <memory>
#include <string>
#include <vector>

class Win32BIGFileSystem;
class ArchiveFileSystem;
class GameTextTable;

namespace godot
{

class RetailFileSystem : public RefCounted
{
	GDCLASS(RetailFileSystem, RefCounted)

public:
	RetailFileSystem();
	~RetailFileSystem() override;

	// Mount RotWK 2.01 + BFME2 1.06 gameplay archives and an optional BFME2-first UI view.
	// Options.ini OpenBFMEBFME2UI = yes (default) selects BFME2 menus; no selects RotWK.
	// Install paths come from the
	// ROTWK_INSTALL / BFME2_INSTALL environment variables, else user://install-paths.cfg
	// (KEY=VALUE lines). Every archive's size and md5 are verified against the pinned policy
	// (hashes cached in user://retail-md5-cache.tsv by path+size+mtime). Returns a report; on any
	// error nothing is mounted.
	Dictionary mount_retail();
	Dictionary mount_aotr_path(const String &path);
	// Standalone BFME1 installation, using its own archives for both menus and gameplay.
	Dictionary mount_bfme1_path(const String &root);
	Dictionary mount_bfme2_path(const String &root);
	bool is_bfme1() const { return m_bfme1; }
	const std::string &bfme1_root() const { return m_bfme1Root; }
	// lane RELEASE-1: the same mount from two folders the player picked on the first-run screen (nothing is read from the
	// environment or the config file; remembering the pick is InstallSetup.remember's job once this succeeded).
	Dictionary mount_retail_paths(const String &rotwk, const String &bfme2);
	// Build a presentation-only source from the verified BFME2 install of this
	// normal RotWK mount. The returned source never becomes a GameWorld source.
	Ref<RetailFileSystem> create_bfme2_shell();

	String content_signature() const;
	bool is_mounted() const { return m_mounted; }
	bool is_shell_source() const { return m_shellSource; }
	String install_root() const;
	bool file_exists(const String &path) const;
	PackedByteArray read_file(const String &path);
	String get_archive_for_file(const String &path) const;
	String get_last_error() const { return m_lastError; }

	// Engine-side access for other device classes.
	ArchiveFileSystem *archive_fs() const;
	// Compatibility alias for archive_fs(); shell styles pass a separate mounted presentation source.
	ArchiveFileSystem *menu_archive_fs() const;
	bool readBytes(const std::string &path, std::vector<uint8_t> &out, std::string *error);
	bool existsNative(const std::string &path) const;
	// The mounted archive file system (nullptr until mount_retail() succeeded); engine-side only.
	ArchiveFileSystem *archive();
	// Loose .map/.scb files found in the mounted installs at mount time (PLAN rule 7 contamination,
	// stop S-039). Never read; every map build reports them.
	const std::vector<LooseFileFinding> &looseFindings() const { return m_loose; }
	// lane MP-1: the mounted archives in load order with their verified md5 (the profile identity of a network game or replay)
	const std::vector<MountedArchive> &mountedArchives() const { return m_mountedArchives; }
	// GDScript view of the same list: [{install, path, size, line}].
	Array get_loose_files() const;
	// Presentation-only aliases; never inserted into the skirmish map catalog.
	Dictionary inspect_library_map(const String &path, bool preview = false);
	Dictionary install_library_map(const String &path, const String &root = "user://installed-maps");
	Array installed_maps(const String &root = "user://installed-maps");
	// Shared metadata for both lobby listing and actual game preparation.
	void append_installed_maps(std::vector<MapCacheEntry> &maps);
	// lane MOD-5: the user maps folder of the RotWK mount (UserDataFolder::userMapFolder: the user data folder that the install's gi.dat names, in the
	// device's application data folder, + "Maps"), which the archive file system reads from disk (ArchiveFileSystem::setLocalFolder). "" when the
	// mount has none (not mounted, BFME1, or gi.dat unreadable: the mount report's user_map_error says why).
	String get_user_map_folder() const;
	// Engine side: the folder a map name's maps are in (MapClassification::mapDirectory): "maps", or with userMap the user maps folder. False +
	// *error when there is no user maps folder.
	bool mapsFolder(bool userMap, std::string &out, std::string *error) const;

protected:
	static void _bind_methods();

private:
	Dictionary mountFrom(const std::string &rotwkRoot, const std::string &bfme2Root, PackedStringArray errors, const std::string &aotr = std::string());
	Dictionary mountBfme2Shell(const std::string &bfme2Root);

	std::unique_ptr<Win32BIGFileSystem> m_fs;
	bool m_mounted = false;
	bool m_shellSource = false;
	std::string m_installRoot;
	bool m_bfme1 = false;
	std::string m_bfme1Root;
	String m_lastError;
	Array m_installedMaps;
	bool m_installedLoaded = false;
	std::unique_ptr<GameTextTable> m_libraryText;
	std::vector<LooseFileFinding> m_loose;
	std::vector<MountedArchive> m_mountedArchives;
	std::string m_userMapFolder; // lane MOD-5
	std::string m_userMapError;
};

} // namespace godot
