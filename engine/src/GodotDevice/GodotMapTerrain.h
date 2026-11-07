// OpenBFME. GPL-3.0.
//
// Device layer: builds the Godot scene of one retail map's terrain (heightmap mesh in three blend layers,
// standing water, rivers, roads, object markers) from the SAGE-port map reader. All parsing, UV, atlas,
// vertex and geometry maths lives in the core library; this class only converts SAGE space to Godot's
// frame (G = (x, z, -y), spec maps-and-terrain.md 2.1) and creates nodes, textures and materials.
//
// Object markers: map objects need object templates (INI object model, another lane), so they are only
// placed as coloured markers by category. Their template names are resolved against the INI object names
// only when options["resolve_objects"] is true.

#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <map>
#include <string>

struct LoadedMap;

namespace godot
{

class RetailFileSystem;
class ImageTexture;
class Texture2D;

class MapTerrainBuilder : public RefCounted
{
	GDCLASS(MapTerrainBuilder, RefCounted)

public:
	// map_name: the map directory/file stem, e.g. "map mp evendim". Returns the scene root or null on
	// failure (see get_report()["errors"]). options (all optional):
	//   cliff_uv: "hypothesis" (default) | "atlas"       vertex_color: "accent" (default) | "all"
	//   markers: bool (default true)   water: bool   rivers: bool   roads: bool   terrain: bool
	//   resolve_objects: bool (default false)
	//   height_probes: PackedVector2Array of SAGE (x, y); their ground heights come back as map_info.probe_heights
	//   user_map: bool (default false; lane MOD-5) the map is in the user maps folder (RetailFileSystem::get_user_map_folder), not the archives
	Node3D *build_map(const Ref<RetailFileSystem> &fs, const String &map_name, const Dictionary &options);

	// Native gameplay path: reuse its already validated map; editor requests parse their own map.
	Node3D *build_loaded_map(const Ref<RetailFileSystem> &fs, const String &map_name, const Dictionary &options, const LoadedMap &map);

	// { map, errors: [..], stops: [..], assumptions: [..], timings_ms: {..}, map_info: {..}, atlas: {..},
	//   mesh: {..}, water: {..}, rivers: {..}, roads: {..}, objects: {..}, camera: {..} }
	Dictionary preview_texture(const Ref<RetailFileSystem> &fs, const String &terrain_name);
	// the map editor's path library: a roads.ini road texture (its whole atlas) by the file name the Road block names
	Dictionary preview_road_texture(const Ref<RetailFileSystem> &fs, const String &texture_file);

	Dictionary get_report() const { return m_report; }

	// Test hook (godot/tests/shader_test.gd): one 10 x 10 cell drawn through the SAME composite path and shader as a
	// map's terrain (TerrainComposite::build, makeCompositeMesh, res://shaders/terrain.gdshader) in debug mode 7
	// (composite of the raw layer albedos, converted to linear once). spec: { base_atlas: Image, uv0: PackedVector2Array(4)
	// (SW,SE,NE,NW), flip: bool, base_wrap: Rect2 (optional), layers: [ { uv, alpha: PackedFloat32Array(4), flip, wrap: Rect2 } ] (0..2) }.
	// Returns null and reports the problem through push_error when the spec is malformed.
	Node3D *build_test_patch(const Dictionary &spec);

protected:
	static void _bind_methods();

private:
	Node3D *build_internal(const Ref<RetailFileSystem> &fs, const String &map_name, const Dictionary &options, const LoadedMap *loaded);
	Ref<Texture2D> loadTexture(RetailFileSystem &fs, const std::string &name, Array &errors, bool mipmaps = true);

	Dictionary m_report;
	std::map<std::string, Ref<Texture2D>> m_textureCache;
};

} // namespace godot
