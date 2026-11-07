// OpenBFME. GPL-3.0. Optional user-requested fishing-mod placement, not retail gameplay.
#pragma once
#include "Common/INIDataTypes.h"
#include <functional>
#include <string>
#include <vector>
class ArchiveFileSystem;
class GameLogic;
class TerrainLogic;
namespace FishingPlacement
{
struct Settings
{
	std::string spotTemplate;
	float minDepth = 20.0f;
	float clearance = 120.0f;
	float spacing = 650.0f;
	float step = 120.0f;
	int maxSpots = 64;
};
// Query returns water depth and surface height; false on dry ground or outside the map.
using DepthQuery = std::function<bool(float, float, float &, float &)>;
void validate(const Settings &settings);
std::vector<Coord3D> positions(const Settings &settings, float width, float height, const DepthQuery &depth, const std::vector<Coord3D> &existing = {});
bool populate(ArchiveFileSystem &fs, GameLogic &logic, const TerrainLogic &terrain, std::string *error);
} // namespace FishingPlacement
