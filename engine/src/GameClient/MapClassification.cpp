// OpenBFME. GPL-3.0.
// See GameClient/MapClassification.h.

#include "Common/BfmeGame.h"
#include "Common/Crc32.h"
#include "GameLogic/SimMath.h"
#include "GameClient/MapClassification.h"

#include <chrono>
#include <cmath>
#include <stdexcept>

namespace
{
double secondsSince(const std::chrono::steady_clock::time_point &t0)
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}
} // namespace

std::string MapClassification::mapDirectory(const std::string &lowerName)
{
	return "maps\\" + lowerName;
}

std::string MapClassification::mapPath(const std::string &lowerName)
{
	return mapDirectory(lowerName) + "\\" + lowerName + ".map";
}

bool MapClassification::run(const std::vector<std::uint8_t> &bytes, const std::string &sourceName, const std::string &lowerName, RetailObjectWorld &world,
	const MapObjectOptions &options, LoadedMap &map, TerrainLogic &terrain, MapObjectDrawables &classified, MapObjectDrawables *clientOnly,
	Result &result, std::string *error, bool applyMapIni)
{
	auto t0 = std::chrono::steady_clock::now();
	const auto worldContext = world.enterContext(); // this world's stores and audio validator for the map.ini parse and the classification
	MapReadOptions mro;
	mro.bfme1 = world.bfme1Data(); // community PR #36
	if (!MapReader::load(bytes, sourceName, mro, map, error))
	{
		return false;
	}
	result.secondsLoad = secondsSince(t0);

	t0 = std::chrono::steady_clock::now();
	terrain.init(map.heightMap, map.chunks, nullptr);
	try
	{
		if (applyMapIni)
		{
			result.mapIni = world.applyMapIni(mapDirectory(lowerName));
		}
	}
	catch (const std::logic_error &e) // lane MAPRESET-1: a game of this world is still live (its map would be torn out from under it)
	{
		if (error)
		{
			*error = e.what();
		}
		return false;
	}
	result.secondsMapIni = secondsSince(t0);

	t0 = std::chrono::steady_clock::now();
	MapObjectCreation::build(map, lowerName, world.things(), terrain, options, classified);
	// Named fork map corrections: content facts live in data/, and the original byte checksum pins the exact map.
	// These are deliberate map fixes, not inferred retail behavior. Keep the capture distance and authored mod maps intact.
	struct Correction { const char *id, *map; std::uint32_t crc; const char *object; float x, y, dx, dy; };
	static const Correction corrections[] = {
#include "../../data/map_object_corrections.inc"
	};
	for (const auto &fix : corrections)
	{
		if (lowerName != fix.map || crc32Bytes(bytes.data(), bytes.size()) != fix.crc) continue;
		unsigned matches = 0;
		for (auto &d : classified.drawables)
		{
			// The table records coordinates to a milliworld unit; this matching margin covers their decimal rounding only.
			if (!d.info || d.info->name != fix.object || std::abs(d.position.x - fix.x) > 0.01f || std::abs(d.position.y - fix.y) > 0.01f) continue;
			const float groundOffset = SimMath::subf32(d.position.z, terrain.getGroundHeight(d.position.x, d.position.y));
			d.position.x = SimMath::addf32(d.position.x, fix.dx);
			d.position.y = SimMath::addf32(d.position.y, fix.dy);
			d.position.z = SimMath::addf32(terrain.getGroundHeight(d.position.x, d.position.y), groundOffset);
			++matches;
		}
		if (matches != 1)
		{
			if (error) *error = std::string("map correction ") + fix.id + ": expected exactly one placement";
			return false;
		}
		classified.report.stops.push_back(std::string("[MAP-FIX ") + fix.id + "] corrected one capture-flag placement in the pinned map");
	}
	result.secondsBuild = secondsSince(t0);
	if (clientOnly)
	{
		clientOnly->sides = classified.sides;
	}
	for (const MapObjectDrawable &d : classified.drawables)
	{
		if (d.fate == MAPOBJ_OBJECT || d.fate == MAPOBJ_OBJECT_BRIDGE)
		{
			++result.fullObjects;
		}
		else if (clientOnly && MapObjectFateDrawsAnything(d.fate))
		{
			clientOnly->drawables.push_back(d);
			++result.clientOnly;
		}
	}
	return true;
}
