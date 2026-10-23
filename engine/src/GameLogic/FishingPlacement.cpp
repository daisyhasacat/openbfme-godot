// OpenBFME. GPL-3.0. Authored mod rules: deterministic deep-water resource distribution.
#include "GameLogic/FishingPlacement.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Thing/ThingFactory.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
namespace FishingPlacement
{
namespace
{
const char *const configFile = "data/ini/openbfme_fishing_placement.ini";
const FieldParse fields[] = {{"Template", INI::parseAsciiString, nullptr, (int)offsetof(Settings, spotTemplate)},
                             {"MinimumDepth", INI::parseReal, nullptr, (int)offsetof(Settings, minDepth)},
                             {"ShoreClearance", INI::parseReal, nullptr, (int)offsetof(Settings, clearance)},
                             {"Spacing", INI::parseReal, nullptr, (int)offsetof(Settings, spacing)},
                             {"ScanStep", INI::parseReal, nullptr, (int)offsetof(Settings, step)},
                             {"MaximumSpots", INI::parseInt, nullptr, (int)offsetof(Settings, maxSpots)},
                             {nullptr, nullptr, nullptr, 0}};
float distanceSquared(const Coord3D &a, const Coord3D &b)
{
	return SimMath::sumSquares2(SimMath::sseSub(a.x, b.x), SimMath::sseSub(a.y, b.y));
}
std::uint32_t rank(int x, int y)
{
	std::uint32_t h = (std::uint32_t)x * 73856093u ^ (std::uint32_t)y * 19349663u;
	h ^= h >> 16;
	h *= 0x7feb352du;
	h ^= h >> 15;
	h *= 0x846ca68bu;
	return h ^ (h >> 16);
}
} // namespace
void validate(const Settings &s)
{
	if (!std::isfinite(s.minDepth) || !std::isfinite(s.clearance) || !std::isfinite(s.spacing) || !std::isfinite(s.step) || s.minDepth <= 0 ||
	    s.clearance <= 0 || s.step <= 0 || s.spacing < SimMath::sseMul(s.clearance, 2.0f) || s.maxSpots <= 0 || s.maxSpots > 4096)
		throw std::runtime_error("FishingPlacement: require positive finite depth, clearance and step; spacing at least twice clearance; MaximumSpots 1..4096");
}
std::vector<Coord3D> positions(const Settings &s, float width, float height, const DepthQuery &query, const std::vector<Coord3D> &existing)
{
	validate(s);
	if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0)
		throw std::runtime_error("FishingPlacement: invalid map extent");
	struct Candidate
	{
		Coord3D point;
		std::uint32_t priority;
	};
	std::vector<Candidate> candidates;
	// A stable spatial hash chooses among candidates across the whole map, without
	// consuming gameplay RNG or depending on frame timing / rendering / local player.
	const float half = SimMath::sseMul(s.step, 0.5f);
	int visited = 0;
	for (int row = 0;; ++row)
	{
		const float y = SimMath::sseAdd(half, SimMath::sseMul(s.step, SimMath::sseFromInt32(row)));
		if (y >= height)
			break;
		for (int col = 0;; ++col)
		{
			const float x = SimMath::sseAdd(half, SimMath::sseMul(s.step, SimMath::sseFromInt32(col)));
			if (x >= width)
				break;
			if (++visited > 1048576)
				throw std::runtime_error("FishingPlacement: ScanStep produces more than 1048576 samples");
			float depth = 0, z = 0;
			if (!query(x, y, depth, z) || depth < s.minDepth)
				continue;
			bool clear = true;
			// Two rings reject shallow banks, tiny ponds and narrow channels around a spot.
			for (float fraction : {0.5f, 1.0f})
				for (const Coord2D direction : {Coord2D{1, 0},
				                                {-1, 0},
				                                {0, 1},
				                                {0, -1},
				                                {0.70710677f, 0.70710677f},
				                                {-0.70710677f, 0.70710677f},
				                                {0.70710677f, -0.70710677f},
				                                {-0.70710677f, -0.70710677f}})
				{
					const float radius = SimMath::sseMul(s.clearance, fraction);
					const float px = SimMath::sseAdd(x, SimMath::sseMul(radius, direction.x));
					const float py = SimMath::sseAdd(y, SimMath::sseMul(radius, direction.y));
					float d = 0, surface = 0;
					if (px < 0 || py < 0 || px >= width || py >= height || !query(px, py, d, surface) || d < s.minDepth || surface != z)
						clear = false;
				}
			if (clear)
				candidates.push_back({{x, y, z}, rank(col, row)});
		}
	}
	std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
		if (a.priority != b.priority)
			return a.priority < b.priority;
		if (a.point.y != b.point.y)
			return a.point.y < b.point.y;
		return a.point.x < b.point.x;
	});
	std::vector<Coord3D> result;
	const float spacingSquared = SimMath::sseMul(s.spacing, s.spacing);
	for (const auto &candidate : candidates)
	{
		if (existing.size() + result.size() >= (size_t)s.maxSpots)
			break;
		bool clear = true;
		for (const auto &p : existing)
			if (distanceSquared(candidate.point, p) < spacingSquared)
				clear = false;
		for (const auto &p : result)
			if (distanceSquared(candidate.point, p) < spacingSquared)
				clear = false;
		if (clear)
			result.push_back(candidate.point);
	}
	return result;
}
bool populate(ArchiveFileSystem &fs, GameLogic &logic, const TerrainLogic &terrain, std::string *error)
{
	if (!fs.doesFileExist(configFile))
		return true;
	try
	{
		Settings settings;
		INIEnvironment env;
		env.fileSystem = &fs;
		int blocks = 0;
		env.blocks.registerBlock("FishingPlacement", [&](INI *ini) {
			++blocks;
			ini->initFromINI(&settings, fields);
		});
		INI ini(env);
		ini.load(configFile, INI_LOAD_OVERWRITE);
		validate(settings);
		if (blocks != 1 || settings.spotTemplate.empty())
			throw std::runtime_error("require exactly one FishingPlacement block with Template");
		const auto *tt = logic.things().findTemplate(settings.spotTemplate);
		auto *neutral = logic.players().getNeutralPlayer();
		if (!tt || !neutral || !neutral->getDefaultTeam() || !logic.aiWorld())
			throw std::runtime_error("missing fishing template or neutral team");
		float width = 0, height = 0;
		if (!terrain.getExtent(0, width, height))
			throw std::runtime_error("map has no playable extent");
		std::vector<Coord3D> existing;
		for (auto *obj = logic.getFirstObject(); obj; obj = obj->getNextObject())
			if (!obj->isDestroyed() && obj->getTemplate()->getName() == settings.spotTemplate)
				existing.push_back(*obj->getPosition());
		const auto points = positions(
		    settings, width, height,
		    [&](float x, float y, float &depth, float &z) {
			    if (!terrain.getStandingWaterHeight(x, y, z))
				    return false;
			    const Coord3D point{x, y, z};
			    const auto *cell = logic.aiWorld()->pathfinder().getCell(LAYER_GROUND, &point);
			    if (!cell || cell->getType() != PathfindCell::CELL_DEEP_WATER)
				    return false;
			    depth = SimMath::sseSub(z, terrain.getGroundHeight(x, y));
			    return true;
		    },
		    existing);
		for (const auto &p : points)
		{
			auto *obj = logic.newObject(tt, neutral->getDefaultTeam(), ObjectStatusMaskType{});
			if (!obj)
				throw std::runtime_error("could not create fishing spot");
			obj->setPosition(&p);
		}
		return true;
	}
	catch (const std::exception &e)
	{
		if (error)
			*error = std::string(configFile) + ": " + e.what();
		return false;
	}
}
} // namespace FishingPlacement
