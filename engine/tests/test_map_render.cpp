// OpenBFME unit tests: roads, water geometry, weather keys and the stop register of the maps lane. GPL-3.0.
// Expected values are hand-computed from the cited ZH formulas (W3DRoadBuffer.cpp) and geometry.

#include "doctest.h"

#include "Common/LooseFileScan.h"
#include "GameClient/MapStops.h"
#include "GameClient/MapWeather.h"
#include "GameClient/TerrainRoads.h"
#include "GameClient/WaterGeometry.h"
#include "GameClient/MapUtil.h"
#include "Libraries/WWVegas/WW3D2/meshmdl.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <limits>
#include <sstream>
#include <string>

#ifndef OPENBFME_DOCS_DIR
#define OPENBFME_DOCS_DIR "../docs"
#endif

namespace
{

WorldHeightMap flatMap(int w, int h, int border, int raw)
{
	WorldHeightMap m;
	m.m_width = w;
	m.m_height = h;
	m.m_borderSize = border;
	m.m_dataSize = w * h;
	m.m_data.assign((size_t)w * h, (std::uint16_t)raw);
	return m;
}

MapObject roadPoint(const char *name, float x, float y, int flags)
{
	MapObject o;
	o.m_objectName = name;
	o.m_location.x = x;
	o.m_location.y = y;
	o.m_flags = flags;
	return o;
}

float polygonArea(const std::vector<Point2F> &p, const std::vector<std::uint32_t> &tri)
{
	double a = 0;
	for (size_t i = 0; i + 2 < tri.size(); i += 3)
	{
		const Point2F &A = p[tri[i]], &B = p[tri[i + 1]], &C = p[tri[i + 2]];
		a += std::fabs(((double)(B.x - A.x) * (C.y - A.y) - (double)(B.y - A.y) * (C.x - A.x)) * 0.5);
	}
	return (float)a;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// Roads
// ------------------------------------------------------------------------------------------------

TEST_CASE("TerrainRoads: roads.ini Road and Bridge blocks; commented-out blocks do not count")
{
	const char *ini =
		";Road Hidden\n"
		";  Texture = X.tga\n"
		";End\n"
		"Road FourLaneDirt\n"
		"  Texture = TRFourLaneDirt.tga\n"
		"  RoadWidth = 70\n"
		"  RoadWidthInTexture = 0.9\n"
		"End\n"
		"Bridge TEST_Bridge\n"
		"  Texture = B.tga\n"
		"  BridgeScale = 0.6\n"
		"End\n";
	RoadTypeIndex idx;
	TerrainRoads::scanText(ini, idx);
	CHECK(idx.byName.size() == 2);
	const RoadType *r = idx.find("fourlanedirt");
	REQUIRE(r != nullptr);
	CHECK(r->texture == "TRFourLaneDirt.tga");
	CHECK(r->roadWidth == 70.0f);
	CHECK(r->roadWidthInTexture == 0.9f);
	CHECK_FALSE(r->isBridge);
	REQUIRE(idx.find("TEST_Bridge") != nullptr);
	CHECK(idx.find("TEST_Bridge")->isBridge);
	CHECK(idx.find("Hidden") == nullptr);
}

TEST_CASE("TerrainRoads::buildStrips: one straight segment, columns every 10 units, ZH UVs, flat z above the cell maximum")
{
	// flat map at raw 256 = 10 world units, 10x10 grid, border 0
	WorldHeightMap map = flatMap(10, 10, 0, 256);
	map.m_data[(size_t)(0 * 10 + 1)] = 512; // cell (0,0) gets a corner at 20 world units
	RoadTypeIndex types;
	RoadType t;
	t.name = "R";
	t.texture = "TR.tga";
	t.roadWidth = 20.0f;
	t.roadWidthInTexture = 0.5f;
	types.byName["r"] = t;

	std::vector<MapObject> objs = { roadPoint("R", 0, 0, FLAG_ROAD_POINT1), roadPoint("R", 30, 0, FLAG_ROAD_POINT2) };
	std::vector<RoadStrip> strips;
	RoadBuildReport rep;
	TerrainRoads::buildStrips(objs, types, map, 0.5f, strips, rep);
	CHECK(rep.pairs == 1);
	REQUIRE(strips.size() == 1);
	const RoadStrip &s = strips[0];
	CHECK(s.texture == "TR.tga");
	// half width = RoadWidth * RoadWidthInTexture / 2 = 5; length 30 -> uCount = 30/10 + 1 = 4 columns, 2 vertices each
	REQUIRE(s.position.size() == 4 * 2 * 3);
	CHECK(s.index.size() == 3 * 6);
	// column 0 bottom/top at x = 0, y = -5 / +5 (normal = (-dy, dx) = (0, 1))
	CHECK(s.position[0] == 0.0f);  CHECK(s.position[1] == -5.0f);
	CHECK(s.position[3] == 0.0f);  CHECK(s.position[4] == 5.0f);
	CHECK(s.position[6] == 10.0f); CHECK(s.position[7] == -5.0f);
	CHECK(s.position[18] == 30.0f);
	// z = max cell height under the column + lift: column 0 touches cell (0,0) whose corner is 20 units: 20 + 0.5
	CHECK(s.position[2] == 20.5f);
	CHECK(s.position[5] == 20.5f);
	// a column over a flat cell (x = 30 -> cell (3,0)): 10 + 0.5
	CHECK(s.position[20] == 10.5f);
	// uv: u = U/(scale*4), v = 85/512 - V/(scale*4); scale = RoadWidth = 20 -> divisor 80
	CHECK(s.uv[0] == 0.0f);
	CHECK(s.uv[1] == doctest::Approx(85.0f / 512.0f + 5.0f / 80.0f).epsilon(1e-6));  // bottom: V = -5
	CHECK(s.uv[3] == doctest::Approx(85.0f / 512.0f - 5.0f / 80.0f).epsilon(1e-6));  // top: V = +5
	CHECK(s.uv[4] == doctest::Approx(10.0f / 80.0f).epsilon(1e-6));
	// first quad: (p0,p1,c1),(p0,c1,c0) with p0..p1 column 0 and c0..c1 column 1
	const std::uint32_t want[6] = { 0, 1, 3, 0, 3, 2 };
	for (int i = 0; i < 6; ++i) CHECK(s.index[(size_t)i] == want[i]);
}

TEST_CASE("TerrainRoads::buildStrips pairing: ZH pairs by flags only, nudges zero-length segments, drops duplicates, counts orphans and unknown types")
{
	WorldHeightMap map = flatMap(20, 20, 0, 0);
	RoadTypeIndex types;
	RoadType t;
	t.name = "R";
	t.texture = "TR.tga";
	t.roadWidth = 10.0f;
	t.roadWidthInTexture = 1.0f;
	types.byName["r"] = t;
	const int P1 = FLAG_ROAD_POINT1, P2 = FLAG_ROAD_POINT2;
	std::vector<MapObject> objs = {
		roadPoint("R", 0, 0, P1), roadPoint("R", 20, 0, P2),               // pair 1
		roadPoint("R", 20, 0, P1), roadPoint("R", 0, 0, P2),               // the same segment reversed: a duplicate
		roadPoint("R", 5, 5, P1 | FLAG_ROAD_CORNER_TIGHT), roadPoint("R", 5, 5, P2), // zero length: nudged by 0.25
		roadPoint("R", 50, 50, P1), roadPoint("Other", 60, 50, 0),         // POINT1 not followed by POINT2: orphan
		roadPoint("R", 70, 50, P2),                                        // POINT2 with no POINT1: orphan
		roadPoint("Nope", 80, 80, P1), roadPoint("Nope", 90, 80, P2),      // a pair whose type is not in roads.ini
		roadPoint("R", 100, 100, P1), roadPoint("Mismatch", 110, 100, P2), // names differ: still a pair (ZH ignores the 2nd name)
		roadPoint("R", 150, 150, P1),                                      // trailing POINT1: orphan
	};
	std::vector<RoadStrip> strips;
	RoadBuildReport rep;
	TerrainRoads::buildStrips(objs, types, map, 0.0f, strips, rep);
	CHECK(rep.pairs == 5);
	CHECK(rep.duplicateSegments == 1);
	CHECK(rep.nudgedZeroLength == 1);
	CHECK(rep.orphanPoints == 3);
	CHECK(rep.unknownRoadTypes == 1);
	REQUIRE(rep.unknownNames.size() == 1);
	CHECK(rep.unknownNames[0] == "Nope");
	CHECK(strips.size() == 3); // pair 1, the nudged one, and the mismatched-name pair
	CHECK(rep.strips == 3);
	// the nudged segment is 0.25 long: 2 columns (uCount < 2 -> 2)
	CHECK(strips[1].position.size() == 2 * 2 * 3);
	CHECK(strips[1].position[6] == 5.25f); // second column x

	CHECK(TerrainRoads::getMaxCellHeight(map, 0, 0) == 0.0f);
}

TEST_CASE("TerrainRoads::getMaxCellHeight takes the maximum of the cell's four corners, clamped to the map, in world units")
{
	WorldHeightMap map = flatMap(6, 6, 1, 0);
	// cell (iX=2, iY=2) in index space = world (10,10) with border 1
	map.m_data[(size_t)(2 + 2 * 6)] = 256;
	map.m_data[(size_t)(3 + 3 * 6)] = 768;
	CHECK(TerrainRoads::getMaxCellHeight(map, 10.0f, 10.0f) == 768 * (10.0f / 256.0f));
	CHECK(TerrainRoads::getMaxCellHeight(map, 19.9f, 19.9f) == 768 * (10.0f / 256.0f));
	CHECK(TerrainRoads::getMaxCellHeight(map, 20.0f, 20.0f) == 768 * (10.0f / 256.0f)); // the next cell shares the raised corner (3,3)
	CHECK(TerrainRoads::getMaxCellHeight(map, 30.0f, 30.0f) == 0.0f);
	// far outside: clamped to the last cell
	CHECK(TerrainRoads::getMaxCellHeight(map, 1e6f, 1e6f) == 0.0f);
	CHECK(TerrainRoads::getMaxCellHeight(map, -1e6f, -1e6f) == 0.0f);
}

// ------------------------------------------------------------------------------------------------
// Water geometry
// ------------------------------------------------------------------------------------------------

TEST_CASE("WaterGeometry::triangulate: convex, non-convex and either winding give n-2 triangles that cover the polygon")
{
	std::vector<std::uint32_t> tri;
	std::vector<Point2F> square = { { 0, 0 }, { 10, 0 }, { 10, 10 }, { 0, 10 } };
	REQUIRE(WaterGeometry::triangulate(square, tri));
	CHECK(tri.size() == 6);
	CHECK(polygonArea(square, tri) == doctest::Approx(100.0f));
	CHECK(WaterGeometry::signedArea(square) == 100.0f);

	std::vector<Point2F> cw(square.rbegin(), square.rend());
	CHECK(WaterGeometry::signedArea(cw) == -100.0f);
	REQUIRE(WaterGeometry::triangulate(cw, tri));
	CHECK(tri.size() == 6);
	CHECK(polygonArea(cw, tri) == doctest::Approx(100.0f));

	// an L shape (area 20*10 + 10*10 = 300): 6 points -> 4 triangles, none may cover the notch
	std::vector<Point2F> L = { { 0, 0 }, { 20, 0 }, { 20, 10 }, { 10, 10 }, { 10, 20 }, { 0, 20 } };
	REQUIRE(WaterGeometry::triangulate(L, tri));
	CHECK(tri.size() == 12);
	CHECK(polygonArea(L, tri) == doctest::Approx(300.0f));

	// a comb (two teeth of width 3): area 9*3 + 2*(3*7) = 69
	std::vector<Point2F> comb = { { 0, 0 }, { 9, 0 }, { 9, 10 }, { 6, 10 }, { 6, 3 }, { 3, 3 }, { 3, 10 }, { 0, 10 } };
	REQUIRE(WaterGeometry::triangulate(comb, tri));
	CHECK(tri.size() == 18);
	CHECK(polygonArea(comb, tri) == doctest::Approx(9.0f * 3.0f + 2.0f * 3.0f * 7.0f));

	// degenerate input is refused, not invented
	std::vector<Point2F> line = { { 0, 0 }, { 5, 0 }, { 10, 0 } };
	CHECK_FALSE(WaterGeometry::triangulate(line, tri));
	CHECK_FALSE(WaterGeometry::triangulate({ { 0, 0 }, { 1, 1 } }, tri));
}

TEST_CASE("WaterGeometry::buildRiverStrip: donor width-scaled UVs")
{
	RiverArea r;
	r.waterHeight = 290;
	r.lines = { {0,0,0,10}, {30,0,30,10}, {100,0,100,10} };
	WaterGeometry::RiverStrip s;
	REQUIRE(WaterGeometry::buildRiverStrip(r, s));
	CHECK(s.position.size() == 18);
	CHECK(s.position[2] == 290.0f);
	CHECK(s.index.size() == 12);
	// Rectangle perimeter 220, width 10 => ten repeats over two sections.
	const std::vector<float> main = {0,0,0.5f,0,0,5,0.5f,5,0,10,0.5f,10};
	const std::vector<float> edge = {0,0,1,0,0,5,1,5,0,10,1,10};
	CHECK(s.uv0 == main);
	CHECK(s.uv1 == edge);
	for (auto &l : r.lines) { l.x0 *= 2; l.y0 *= 2; l.x1 *= 2; l.y1 *= 2; }
	WaterGeometry::RiverStrip scaled;
	REQUIRE(WaterGeometry::buildRiverStrip(r, scaled));
	CHECK(scaled.uv0 == main);
	CHECK(scaled.uv1 == edge);
	r.lines.front().y1 = r.lines.front().y0;
	CHECK_FALSE(WaterGeometry::buildRiverStrip(r, s));
	r.lines.resize(1);
	CHECK_FALSE(WaterGeometry::buildRiverStrip(r, s));
}

// ------------------------------------------------------------------------------------------------
// Weather
// ------------------------------------------------------------------------------------------------

TEST_CASE("MapWeather: hardware fog and cloud keys of the Weather block; map.ini overrides weather.ini; other keys ignored")
{
	MapWeather w;
	MapWeatherScan::applyText(
		"; defaults\n"
		"Weather\n"
		"  SnowEnabled = yes\n"
		"  CloudTextureSize = X:660.0 Y:660.0 ; comment\n"
		"  CloudOffsetPerSecond = X:-0.012 Y:-0.018\n"
		"End\n",
		w);
	CHECK(w.cloudSize[0] == 660.0f);
	CHECK(w.cloudOffsetPerSecond[1] == -0.018f);
	CHECK_FALSE(w.fogEnabled);
	CHECK_FALSE(w.fogKeysSeen);

	MapWeatherScan::applyText(
		"Object Foo\n"
		"  HardwareFogEnable = Yes\n" // outside a Weather block: ignored
		"End\n"
		"Weather\n"
		"  HardwareFogColor = R:220 G:200 B:100\n"
		"  HardwareFogEnable = Yes\n"
		"  HardwareFogStart = 500\n"
		"  HardwareFogEnd = 1700\n"
		"  CloudTextureSize = X:1000 Y:500\n"
		"End\n",
		w);
	CHECK(w.fogEnabled);
	CHECK(w.fogKeysSeen);
	CHECK(w.fogColor[0] == 220.0f / 255.0f);
	CHECK(w.fogColor[2] == 100.0f / 255.0f);
	CHECK(w.fogStart == 500.0f);
	CHECK(w.fogEnd == 1700.0f);
	CHECK(w.cloudSize[0] == 1000.0f);
	CHECK(w.cloudSize[1] == 500.0f);
	CHECK(w.cloudOffsetPerSecond[0] == -0.012f); // untouched by the second block

	MapWeather off;
	MapWeatherScan::applyText("Weather\n HardwareFogEnable = No\nEnd\n", off);
	CHECK_FALSE(off.fogEnabled);
	CHECK(off.fogKeysSeen);
}

// ------------------------------------------------------------------------------------------------
// Loose files (stop S-039): the scan the production mount runs
// ------------------------------------------------------------------------------------------------

TEST_CASE("LooseFileScan::scanInstalls: loose .map/.scb files are findings carrying the S-039 line; a missing install is an error")
{
	namespace fs = std::filesystem;
	const fs::path root = fs::temp_directory_path() / "openbfme_loose_scan_test";
	std::error_code ec;
	fs::remove_all(root, ec);
	fs::create_directories(root / "libraries" / "lib_a");
	fs::create_directories(root / "maps");
	auto put = [](const fs::path &p, const std::string &data) {
		std::ofstream(p, std::ios::binary) << data;
	};
	put(root / "libraries" / "lib_a" / "lib_a.map", "");        // 0 bytes: would hide an archive copy
	put(root / "maps" / "Extra.SCB", "abc");                    // case-insensitive extension, 3 bytes
	put(root / "maps" / "notes.txt", "not a map");              // ignored
	put(root / "game.dat", "x");                                // ignored

	std::vector<LooseFileFinding> found;
	std::string err;
	REQUIRE_MESSAGE(LooseFileScan::scanInstalls({ { "testinstall", root.string() } }, found, &err), err);
	REQUIRE(found.size() == 2);
	CHECK(found[0].install == "testinstall");
	CHECK(found[0].file.relativePath == "libraries/lib_a/lib_a.map");
	CHECK(found[0].file.size == 0);
	CHECK(found[0].line == "S-039 contamination (PLAN rule 7): loose testinstall file libraries/lib_a/lib_a.map (0 bytes) is not part of retail; "
		"it is never read, the archive copy is used (a 0-byte loose map would hide that archive copy if loose files were mounted)");
	CHECK(found[1].file.relativePath == "maps/Extra.SCB");
	CHECK(found[1].file.size == 3);
	CHECK(found[1].line.find("(3 bytes)") != std::string::npos);
	CHECK(found[1].line.find("0-byte") == std::string::npos);

	// nothing is skipped silently: an unreadable install is an error and yields no partial result
	CHECK_FALSE(LooseFileScan::scanInstalls({ { "ok", root.string() }, { "gone", (root / "missing").string() } }, found, &err));
	CHECK(err.find("not a directory") != std::string::npos);
	CHECK(found.empty());
	fs::remove_all(root, ec);
}

// ------------------------------------------------------------------------------------------------
// The stop register
// ------------------------------------------------------------------------------------------------

TEST_CASE("Stops S-030..S-039 and S-060 are all in code, unique, and registered in docs/STOPS.md with the same area text")
{
	const std::vector<MapStop> &stops = MapStops::all();
	REQUIRE(stops.size() == 11);
	std::set<std::string> ids;
	for (size_t i = 0; i < stops.size(); ++i)
	{
		char want[8];
		// this lane owns S-030 to S-039, in order, and S-060 (the lane's extended range S-060..S-069)
		std::snprintf(want, sizeof(want), "S-%03d", i < 10 ? 30 + (int)i : 60);
		CHECK(std::string(stops[i].id) == want); // this lane owns S-030 to S-039, in order
		ids.insert(stops[i].id);
		CHECK(std::string(stops[i].area).size() > 3);
		CHECK(std::string(stops[i].gap).size() > 20);
	}
	CHECK(ids.size() == 11);
	CHECK(MapStops::find("S-034") != nullptr);
	CHECK(MapStops::find("S-099") == nullptr);
	CHECK(MapStops::terrainRender().size() == 7); // S-030..S-036
	CHECK(MapStops::mapLoad().size() == 3);       // S-037..S-039
	REQUIRE(MapStops::terrainLogic().size() == 1);
	CHECK(std::string(MapStops::terrainLogic()[0]->id) == "S-060");

	// the register
	std::ifstream in(std::string(OPENBFME_DOCS_DIR) + "/STOPS.md");
	REQUIRE_MESSAGE(static_cast<bool>(in), "cannot read docs/STOPS.md (OPENBFME_DOCS_DIR = " << OPENBFME_DOCS_DIR << ")");
	std::stringstream ss;
	ss << in.rdbuf();
	const std::string doc = ss.str();
	for (const MapStop &s : stops)
	{
		// | ID | Kind | Area | Gap | ...: every stop of this lane is a `code` stop with this area and gap text
		const std::string row = std::string("| ") + s.id + " | code | " + s.area + " | " + s.gap + " | ";
		CHECK_MESSAGE(doc.find(row) != std::string::npos, "docs/STOPS.md lacks the row '" << row << "'");
		// the gap text of the register row is the code's text
		CHECK_MESSAGE(doc.find(s.gap) != std::string::npos, "docs/STOPS.md gap text of " << s.id << " differs from the code");
	}

	// the load-time stops (every LoadedMap reports S-037 and S-038, asserted in test_map_chunks.cpp)
	CHECK(std::string(MapStops::mapLoad()[0]->id) == "S-037");
	CHECK(std::string(MapStops::mapLoad()[1]->id) == "S-038");
	CHECK(std::string(MapStops::mapLoad()[2]->id) == "S-039");
}

TEST_CASE("MapWeather: map snowfall overrides defaults, including dense snow and explicit disabling")
{
	MapWeather w;
	MapWeatherScan::applyText("Weather\n SnowEnabled = Yes\n IsSnowing = No\n SnowTexture = EXRainDrop.tga\n SnowSpacing = 30\n SnowSpeed = 50\nEnd\n", w);
	CHECK(w.snowEnabled);
	CHECK_FALSE(w.isSnowing);
	MapWeatherScan::applyText("Weather\n IsSnowing = Yes\n SnowTexture = EXSnowFlake2.tga ; snow\n SnowSpacing = 5\n SnowSpeed = 60\n SnowBoxHeight = 1500\n SnowQuadSize = 3\n SnowAmplitude = 20\n SnowXYSpeed = X:2 Y:-3\nEnd\n", w);
	CHECK(w.isSnowing);
	CHECK(w.snowTexture == "EXSnowFlake2.tga");
	CHECK(w.snowSpacing == 5);
	CHECK(w.snowSpeed == 60);
	CHECK(w.snowHeight == 1500);
	CHECK(w.snowQuadSize == 3);
	CHECK(w.snowAmplitude == 20);
	CHECK(w.snowXYSpeed[0] == 2);
	CHECK(w.snowXYSpeed[1] == -3);
	MapWeatherScan::applyText("Weather\n SnowEnabled = No\nEnd\n", w);
	CHECK_FALSE(w.snowEnabled);
}

TEST_CASE("TerrainRoads: authored angled corners meet on both edges regardless of segment direction")
{
	WorldHeightMap map = flatMap(30, 30, 0, 0);
	RoadTypeIndex types;
	types.byName["r"] = {"R", "TR.tga", 20.0f, 1.0f, false};
	for (bool reversed : {false, true})
	{
		std::vector<MapObject> objects = {
			roadPoint("R", 50, 100, FLAG_ROAD_POINT1), roadPoint("R", 100, 100, FLAG_ROAD_POINT2 | FLAG_ROAD_CORNER_ANGLED),
			roadPoint("R", 100, reversed ? 150 : 100, FLAG_ROAD_POINT1 | FLAG_ROAD_CORNER_ANGLED),
			roadPoint("R", 100, reversed ? 100 : 150, FLAG_ROAD_POINT2 | FLAG_ROAD_CORNER_ANGLED)};
		std::vector<RoadStrip> strips;
		RoadBuildReport report;
		TerrainRoads::buildStrips(objects, types, map, 0.15f, strips, report);
		REQUIRE(strips.size() == 2);
		CHECK(report.miteredJoins == 1);
		const auto &a = strips[0].position;
		const auto &b = strips[1].position;
		const size_t endpoint = reversed ? b.size()-6 : 0;
		CHECK(a[a.size()-6] == 110.0f);
		CHECK(a[a.size()-5] == 90.0f);
		CHECK(a[a.size()-3] == 90.0f);
		CHECK(a[a.size()-2] == 110.0f);
		CHECK(a[a.size()-6] == b[endpoint + (reversed ? 3 : 0)]);
		CHECK(a[a.size()-5] == b[endpoint + (reversed ? 4 : 1)]);
		CHECK(a[a.size()-3] == b[endpoint + (reversed ? 0 : 3)]);
		CHECK(a[a.size()-2] == b[endpoint + (reversed ? 1 : 4)]);
	}
}

TEST_CASE("TerrainRoads: duplicate suppression preserves different authored road types")
{
	WorldHeightMap map = flatMap(20,20,0,0);
	RoadTypeIndex types;
	types.byName["r"] = {"R","R.tga",20,1,false};
	types.byName["s"] = {"S","S.tga",20,1,false};
	std::vector<MapObject> objects = {
		roadPoint("R",0,50,FLAG_ROAD_POINT1),roadPoint("R",100,50,FLAG_ROAD_POINT2),
		roadPoint("S",0,50,FLAG_ROAD_POINT1),roadPoint("S",100,50,FLAG_ROAD_POINT2)};
	std::vector<RoadStrip> strips;
	RoadBuildReport report;
	TerrainRoads::buildStrips(objects,types,map,0,strips,report);
	CHECK(strips.size() == 2);
	CHECK(report.duplicateSegments == 0);
}

TEST_CASE("TerrainRoads: rounded corners trim their legs and use dedicated normal and tight atlas rows")
{
	WorldHeightMap map = flatMap(50,50,0,0);
	RoadTypeIndex types;
	types.byName["r"] = {"R","R.tga",20,1,false};
	for(bool tight : {false,true})
	{
		const int corner = tight ? FLAG_ROAD_CORNER_TIGHT : 0;
		std::vector<MapObject> objects = {
			roadPoint("R",50,150,FLAG_ROAD_POINT1),roadPoint("R",150,150,FLAG_ROAD_POINT2|corner),
			roadPoint("R",150,150,FLAG_ROAD_POINT1|corner),roadPoint("R",150,250,FLAG_ROAD_POINT2)};
		std::vector<RoadStrip> strips;
		RoadBuildReport report;
		TerrainRoads::buildStrips(objects,types,map,0,strips,report);
		CHECK(report.roundedJoins == 1);
		CHECK(report.miteredJoins == 0);
		REQUIRE(strips.size() == 5); // three 30-degree curve pieces
		const float tangentX = tight ? 140.0f : 120.0f;
		CHECK(strips[0].position[strips[0].position.size()-6] == tangentX);
		// tolerance: atlas projection can extend slightly around its tile, but stays in the selected row.
		CHECK(strips[2].uv[1] > (tight ? 0.7f : 0.4f));
		// tolerance: the curve must not sample the straight road row near V=85/512.
		CHECK(strips[2].uv[1] < (tight ? 1.0f : 0.7f));
	}
}

TEST_CASE("TerrainRoads: tees replace overlapping center rectangles with their authored atlas tile")
{
	WorldHeightMap map = flatMap(30,30,0,0);
	RoadTypeIndex types;
	types.byName["r"] = {"R","R.tga",20,1,false};
	std::vector<MapObject> objects;
	for (const auto &end : std::vector<Point2F>{{50,100},{150,100},{100,150}})
	{
		objects.push_back(roadPoint("R",100,100,FLAG_ROAD_POINT1));
		objects.push_back(roadPoint("R",end.x,end.y,FLAG_ROAD_POINT2));
	}
	std::vector<RoadStrip> strips;
	RoadBuildReport report;
	TerrainRoads::buildStrips(objects,types,map,0,strips,report);
	CHECK(strips.size() == 4);
	CHECK(report.teeJoins == 1);
	CHECK(report.miteredJoins == 0);
	CHECK(report.roundedJoins == 0);
	REQUIRE(MapStops::find("S-036"));
	CHECK(std::string(MapStops::find("S-036")->gap) == "Straight paths, mitered and curved corners, tee and four-way atlas junctions and authored JOIN end caps are rendered; Y/H junctions, cross-type alpha alignment and BFME2 stacking remain unported");
}

TEST_CASE("TerrainRoads: four-way tile connects four stubs independent of branch direction")
{
	WorldHeightMap map = flatMap(40,40,0,0);
	RoadTypeIndex types;
	types.byName["r"] = {"R","R.tga",20,1,false};
	std::vector<MapObject> objects;
	const std::vector<Point2F> points={{50,150},{250,150},{150,50},{150,250}};
	for(size_t i=0;i<points.size();++i)
	{
		const auto p=points[i];
		objects.push_back(roadPoint("R",i%2?150:p.x,i%2?150:p.y,FLAG_ROAD_POINT1));
		objects.push_back(roadPoint("R",i%2?p.x:150,i%2?p.y:150,FLAG_ROAD_POINT2));
	}
	std::vector<RoadStrip> strips;
	RoadBuildReport report;
	TerrainRoads::buildStrips(objects,types,map,0,strips,report);
	REQUIRE(strips.size()==5);
	CHECK(report.fourWayJoins==1);
	CHECK(report.teeJoins==0);
	const auto &tile=strips.back();
	// Symmetrical donor four-way tile: center (425,425)/512, radius 10.3.
	CHECK(tile.position.front()==doctest::Approx(139.7f)); // tolerance: decimal donor 1.03 extent rounds in float.
	CHECK(tile.uv[0]==doctest::Approx(425.0f/512.0f-10.3f/80.0f)); // tolerance: projected atlas coordinate float rounding.
	CHECK(tile.uv[1]==doctest::Approx(425.0f/512.0f+10.3f/80.0f)); // tolerance: projected atlas coordinate float rounding.
}

TEST_CASE("TerrainRoads: authored JOIN endpoints receive alpha end caps in either direction, square ends remain authored")
{
	WorldHeightMap map = flatMap(40,40,0,0);
	RoadTypeIndex types;
	types.byName["r"] = {"R","R.tga",20,1,false};
	for(bool reversed:{false,true})
	{
		std::vector<MapObject> objects={
			roadPoint("R",reversed?200:100,150,FLAG_ROAD_POINT1|(reversed?0:FLAG_ROAD_JOIN)),
			roadPoint("R",reversed?100:200,150,FLAG_ROAD_POINT2|(reversed?FLAG_ROAD_JOIN:0))};
		std::vector<RoadStrip> strips;
		RoadBuildReport report;
		TerrainRoads::buildStrips(objects,types,map,0,strips,report);
		REQUIRE(strips.size()==2);
		CHECK(report.endCaps==1);
		// Donor fade extends 0.65 * (48/128)*width outside the endpoint.
		CHECK(strips.back().position[0]==95.125f);
		CHECK(strips.back().uv[0]==106.0f/512.0f-4.875f/80.0f);
		CHECK(strips.back().uv[1]==425.0f/512.0f+10.625f/80.0f);
		objects[0].m_flags&=~FLAG_ROAD_JOIN;
		objects[1].m_flags&=~FLAG_ROAD_JOIN;
		TerrainRoads::buildStrips(objects,types,map,0,strips,report);
		CHECK(strips.size()==1);
		CHECK(report.endCaps==0);
	}
}

TEST_CASE("TerrainRoads: an editor path width replaces the road type's visible width and keeps the atlas band")
{
	WorldHeightMap map = flatMap(10, 10, 0, 256);
	RoadTypeIndex types;
	RoadType t;
	t.name = "R";
	t.texture = "TR.tga";
	t.roadWidth = 20.0f;
	t.roadWidthInTexture = 0.5f;
	types.byName["r"] = t;
	std::vector<MapObject> objs = { roadPoint("R", 0, 0, FLAG_ROAD_POINT1), roadPoint("R", 30, 0, FLAG_ROAD_POINT2) };
	objs[0].m_properties.setReal(TerrainRoads::kEditorRoadWidthKey, 30.0f);
	std::vector<RoadStrip> strips;
	RoadBuildReport rep;
	TerrainRoads::buildStrips(objs, types, map, 0.0f, strips, rep);
	REQUIRE(strips.size() == 1);
	const RoadStrip &s = strips[0];
	// visible width 30 -> half 15; scale = 30 / 0.5 = 60, so v still spans 85/512 +- RoadWidthInTexture/8
	CHECK(s.position[1] == -15.0f);
	CHECK(s.position[4] == 15.0f);
	// tolerance: the UV is float32 V / (scale * 4) with scale = 30 / 0.5, the expectation folds 0.5 / 8 exactly
	CHECK(s.uv[1] == doctest::Approx(85.0f / 512.0f + 0.5f / 8.0f).epsilon(1e-6));
	// tolerance: as above
	CHECK(s.uv[3] == doctest::Approx(85.0f / 512.0f - 0.5f / 8.0f).epsilon(1e-6));
}

TEST_CASE("TerrainRoads: reversing corners preserve bounded source strips when neither curve nor miter fits")
{
	WorldHeightMap map = flatMap(40,40,0,0);
	RoadTypeIndex types;
	types.byName["r"] = {"R","R.tga",20,1,false};
	for (bool reversed : {false,true})
		for (int flags : {0,int(FLAG_ROAD_CORNER_ANGLED)})
			for (const Point2F far : std::vector<Point2F>{{0,99},{0,101},{100,105}})
			{
				std::vector<MapObject> objects = {
					roadPoint("R",0,100,FLAG_ROAD_POINT1),roadPoint("R",100,100,FLAG_ROAD_POINT2|flags),
					roadPoint("R",reversed?far.x:100,reversed?far.y:100,FLAG_ROAD_POINT1|flags),
					roadPoint("R",reversed?100:far.x,reversed?100:far.y,FLAG_ROAD_POINT2|flags)};
				std::vector<RoadStrip> strips;
				RoadBuildReport report;
				TerrainRoads::buildStrips(objects,types,map,0,strips,report);
				REQUIRE(strips.size()==2);
				CHECK(report.miteredJoins==0);
				CHECK(report.roundedJoins==0);
				for(size_t segment=0;segment<2;++segment)
				{
					std::vector<MapObject> single(objects.begin()+segment*2,objects.begin()+segment*2+2);
					std::vector<RoadStrip> source;
					RoadBuildReport singleReport;
					TerrainRoads::buildStrips(single,types,map,0,source,singleReport);
					REQUIRE(source.size()==1);
					CHECK(strips[segment].position==source[0].position);
					CHECK(strips[segment].uv==source[0].uv);
				}
			}
}

TEST_CASE("TerrainRoads: four branches on the same side preserve authored strips and report an unsupported junction")
{
	WorldHeightMap map = flatMap(40,40,0,0);
	RoadTypeIndex types;
	types.byName["r"] = {"R","R.tga",20,1,false};
	const std::vector<std::vector<Point2F>> cases={
		{{250,150},{50,150},{150,250},{250,250}},
		{{250,150},{50,150},{130,250},{170,250}},
		{{250,150},{50,150},{130,50},{170,50}}};
	for (const auto &points : cases)
	for (bool reversed : {false,true})
		for (size_t first=0;first<points.size();++first)
		{
			std::vector<MapObject> objects;
			for(size_t k=0;k<points.size();++k)
			{
				const auto p=points[(first+k)%points.size()];
				objects.push_back(roadPoint("R",reversed?p.x:150,reversed?p.y:150,FLAG_ROAD_POINT1));
				objects.push_back(roadPoint("R",reversed?150:p.x,reversed?150:p.y,FLAG_ROAD_POINT2));
			}
			std::vector<RoadStrip> strips;
			RoadBuildReport report;
			TerrainRoads::buildStrips(objects,types,map,0,strips,report);
			REQUIRE(strips.size()==4);
			CHECK(report.fourWayJoins==0);
			CHECK(report.specialJunctionsPending==1);
			for(size_t segment=0;segment<4;++segment)
			{
				std::vector<MapObject> single(objects.begin()+segment*2,objects.begin()+segment*2+2);
				std::vector<RoadStrip> source;
				RoadBuildReport singleReport;
				TerrainRoads::buildStrips(single,types,map,0,source,singleReport);
				REQUIRE(source.size()==1);
				CHECK(strips[segment].position==source[0].position);
				CHECK(strips[segment].uv==source[0].uv);
			}
		}
}

TEST_CASE("TerrainRoads: short tee and cross arms preserve source strips before any stub changes")
{
	WorldHeightMap map=flatMap(40,40,0,0);
	RoadTypeIndex types;
	types.byName["r"]={"R","R.tga",20,1,false};
	for (int degree : {3,4})
		for (bool reversed : {false,true})
			for (int shortArm=0;shortArm<degree;++shortArm)
			{
				const Point2F directions[]={{1,0},{-1,0},{0,1},{0,-1}};
				std::vector<MapObject> objects;
				for (int arm=0;arm<degree;++arm)
				{
					const float length=arm==shortArm?5.0f:100.0f;
					const Point2F end={150+directions[arm].x*length,150+directions[arm].y*length};
					objects.push_back(roadPoint("R",reversed?end.x:150,reversed?end.y:150,FLAG_ROAD_POINT1));
					objects.push_back(roadPoint("R",reversed?150:end.x,reversed?150:end.y,FLAG_ROAD_POINT2));
				}
				std::vector<RoadStrip> strips;
				RoadBuildReport report;
				TerrainRoads::buildStrips(objects,types,map,0,strips,report);
				REQUIRE(strips.size()==size_t(degree));
				CHECK(report.teeJoins==0);
				CHECK(report.fourWayJoins==0);
				CHECK(report.specialJunctionsPending==1);
				for (int arm=0;arm<degree;++arm)
				{
					std::vector<MapObject> pair(objects.begin()+arm*2,objects.begin()+arm*2+2);
					std::vector<RoadStrip> source;
					RoadBuildReport single;
					TerrainRoads::buildStrips(pair,types,map,0,source,single);
					REQUIRE(source.size()==1);
					CHECK(strips[arm].position==source[0].position);
					CHECK(strips[arm].uv==source[0].uv);
				}
			}
}

TEST_CASE("TerrainRoads: endpoint index isolates coordinates and matches mixed-case road names")
{
	WorldHeightMap map=flatMap(30,30,0,0);
	RoadTypeIndex types;
	types.byName["r"]={"R","R.tga",20,1,false};
	std::vector<MapObject> objects;
	for (int junction=0;junction<64;++junction)
	{
		const float x=150.0f+float(junction)*300.0f;
		for (const Point2F d : std::vector<Point2F>{{-100,0},{100,0},{0,100}})
		{
			objects.push_back(roadPoint("r",x,150,FLAG_ROAD_POINT1));
			objects.push_back(roadPoint("R",x+d.x,150+d.y,FLAG_ROAD_POINT2));
		}
	}
	std::vector<RoadStrip> strips;
	RoadBuildReport report;
	TerrainRoads::buildStrips(objects,types,map,0,strips,report);
	CHECK(report.pairs==192);
	CHECK(report.teeJoins==64);
	CHECK(report.specialJunctionsPending==0);
	CHECK(strips.size()==256);
}

TEST_CASE("water material: retail effect defaults and authored scales replace the placeholder colour")
{
	MeshShaderMaterialDef source;
	source.TypeName = "WaterShader.FX";
	WaterGeometry::StandingMaterial material;
	REQUIRE(WaterGeometry::readStandingMaterial(source, material));
	CHECK(material.diffuse[0] == 0.15f);
	CHECK(material.diffuse[3] == 0.5f);
	CHECK(material.dimming == 0.8f);
	CHECK(material.bumpUV[0] == 7.0f);
	CHECK(material.envUV[0] == 0.2f);
	CHECK(material.windSpeed == 0.02f);
	// WaterPlaneFX's authored scale and texture, read from the mounted W3D.
	MeshShaderMaterialProperty scale;
	scale.Name = "BumpUVScale";
	scale.Float[0] = scale.Float[1] = 60.0f;
	source.Properties.push_back(scale);
	MeshShaderMaterialProperty texture;
	texture.Name = "WaterBumpTexture";
	texture.StringValue = "WtrBump_01.tga";
	source.Properties.push_back(texture);
	REQUIRE(WaterGeometry::readStandingMaterial(source, material));
	CHECK(material.bumpUV[0] == 60.0f);
	CHECK(material.bumpUV[1] == 60.0f);
	CHECK(material.bumpTexture == "WtrBump_01.tga");
	source.TypeName = "normalmapped.fx";
	CHECK_FALSE(WaterGeometry::readStandingMaterial(source, material));
}

TEST_CASE("water acceptance: remaining shader and geometry approximations stay explicit")
{
	const auto *stop = MapStops::find("S-035");
	REQUIRE(stop != nullptr);
	CHECK(std::string(stop->gap) == "Standing water preserves authored FX techniques, PCA/PCA_U normals and shoreline Gerstner displacement on a donor high-detail grid alongside the bump/specular path; TIME, LOD selection, scene-light binding and terrain depth are inferred; reflections, shadow filtering, other PCA LODs, depth-colour LUT and legacy shoreline texture tracks remain unported");
}

namespace
{
std::string syntheticPcaBasis()
{
	std::ostringstream text;
	text << "NumFrames: 2\nSpeed: 0.5\n";
	for (int row = 0; row < 13; ++row)
	{
		text << "Row:";
		for (int value = 0; value < 6; ++value) text << " " << (row + value) * 255;
		text << "\n";
	}
	return text.str();
}
}

TEST_CASE("water PCA basis: donor XZY layout, normalization, interpolation and wrap")
{
	WaterGeometry::PcaBasis basis;
	std::string error;
	REQUIRE_MESSAGE(WaterGeometry::readPcaBasis(syntheticPcaBasis(), basis, &error), error);
	REQUIRE(basis.frames.size() == 2);
	CHECK(basis.speed == 0.5f);
	CHECK(basis.frames[0][0] == WaterGeometry::PcaVector{0, 4, 2, 0});
	CHECK(basis.frames[1][0] == WaterGeometry::PcaVector{1, 5, 3, 0});
	CHECK(basis.frames[0][1] == WaterGeometry::PcaVector{1, 2, 3, 4});
	CHECK(basis.frames[0][2] == WaterGeometry::PcaVector{5, 6, 7, 8});
	CHECK(basis.frames[0][3] == WaterGeometry::PcaVector{3, 4, 5, 6});
	CHECK(basis.frames[0][9] == WaterGeometry::PcaVector{11, 12, 13, 14});
	CHECK(WaterGeometry::samplePcaBasis(basis, 1.0f)[0] == WaterGeometry::PcaVector{0.5f, 4.5f, 2.5f, 0});
	CHECK(WaterGeometry::samplePcaBasis(basis, 3.0f)[0] == WaterGeometry::PcaVector{0.5f, 4.5f, 2.5f, 0});
	CHECK(WaterGeometry::samplePcaBasis(basis, 4.0f) == basis.frames[0]);
	CHECK(WaterGeometry::samplePcaBasis(WaterGeometry::PcaBasis{}, 0.0f)[0] == WaterGeometry::PcaVector{0, 0, 1, 0});
}

TEST_CASE("water PCA basis: malformed data fails without retaining a prior track")
{
	WaterGeometry::PcaBasis basis;
	std::string error;
	REQUIRE(WaterGeometry::readPcaBasis(syntheticPcaBasis(), basis, &error));
	CHECK_FALSE(WaterGeometry::readPcaBasis("NumFrames: 2\nSpeed: 1\n", basis, &error));
	CHECK(error == "PCA basis: missing coefficient row");
	CHECK(basis.frames.empty());
	CHECK_FALSE(WaterGeometry::readPcaBasis("NumFrames: 0\n", basis, &error));
	CHECK(error == "PCA basis: invalid frame count");
	CHECK_FALSE(WaterGeometry::readPcaBasis(syntheticPcaBasis() + "extra\n", basis, &error));
	CHECK(error == "PCA basis: trailing data");
}

TEST_CASE("water effect techniques: names come from the effect table with checked offsets")
{
	std::vector<std::uint8_t> bytes(96, 0);
	auto put = [&](size_t offset, std::uint32_t value) { for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = (std::uint8_t)(value >> (8 * i)); };
	put(0, 0xfeff0901); put(4, 0); put(12, 1); // no parameters, one technique
	put(24, 56); put(32, 1); // name at 8+56, one pass
	put(44, 1); // one pass state, occupying 48..63
	const std::string name = "CustomPCA";
	put(64, (std::uint32_t)name.size() + 1);
	std::copy(name.begin(), name.end(), bytes.begin() + 68);
	std::vector<std::string> names;
	std::string error;
	REQUIRE_MESSAGE(WaterGeometry::readEffectTechniques(bytes, names, &error), error);
	CHECK(names == std::vector<std::string>{ "CustomPCA" });
	put(24, 0xfffffff0);
	CHECK_FALSE(WaterGeometry::readEffectTechniques(bytes, names, &error));
	CHECK(names.empty());
}

TEST_CASE("WaterGeometry::standing grid shares vertices and queries active shoreline segments")
{
	StandingWaterArea area; area.points={{0,0},{40,0},{40,40},{0,40}}; area.waterHeight=20;
	StandingWaveArea wave; wave.points={{0,20},{40,20}}; wave.enablePcaWave=1;
	WaterGeometry::StandingGrid grid;
	REQUIRE(WaterGeometry::buildStandingGrid(area,{wave},[](float,float){return 0.0f;},grid));
	CHECK(grid.spacing==20);
	CHECK(grid.vertices.size()==9);
	CHECK(grid.indices==std::vector<uint32_t>{0,3,2,0,1,3,1,5,3,1,4,5,2,7,6,2,3,7,3,8,7,3,5,8});
	for(const auto &v:grid.vertices) {
		CHECK(v.nearest.x==20); CHECK(v.nearest.y==20);
		CHECK(v.direction.x==0); CHECK(v.direction.y==-1); CHECK(v.halfLength==20);
	}
	wave.enablePcaWave=0;
	REQUIRE(WaterGeometry::buildStandingGrid(area,{wave},[](float,float){return 0.0f;},grid));
	CHECK(grid.vertices[0].halfLength==0);
	CHECK(grid.vertices[0].nearest.x==100000);
	wave.enablePcaWave=1;
	REQUIRE(WaterGeometry::buildStandingGrid(area,{wave},[](float,float){return -80.0f;},grid));
	CHECK(grid.vertices[0].halfLength==0); // source strict depth < 100
	wave.points={{20,20},{20,20}};
	REQUIRE(WaterGeometry::buildStandingGrid(area,{wave},[](float,float){return 0.0f;},grid));
	CHECK(grid.vertices[0].halfLength==0);
	area.points={{0,0},{4000,0},{4000,6000},{0,6000}};
	REQUIRE(WaterGeometry::buildStandingGrid(area,{},[](float,float){return 0.0f;},grid));
	CHECK(grid.spacing==40);
	CHECK(grid.vertices.size()==15251);
	CHECK(grid.indices.size()==90000);
	area.points={{-39,-39},{1,-39},{1,1},{-39,1}};
	REQUIRE(WaterGeometry::buildStandingGrid(area,{},[](float,float){return 0.0f;},grid));
	CHECK(grid.vertices[0].point.x==-20); // donor truncates grid origin toward zero
	CHECK(grid.vertices[0].point.y==-20);
}
TEST_CASE("water material: authored Gerstner vectors remain distinct")
{
	MeshShaderMaterialDef source; source.TypeName="WaterShader.FX";
	for(int i=0;i<3;++i) {
		MeshShaderMaterialProperty prop; prop.Name="GWAmpFreqPhaseSteepness"+std::to_string(i+1);
		for(int j=0;j<4;++j) prop.Float[j]=float(i*4+j+1);
		source.Properties.push_back(prop);
	}
	WaterGeometry::StandingMaterial material;
	REQUIRE(WaterGeometry::readStandingMaterial(source,material));
	for(int i=0;i<3;++i) for(int j=0;j<4;++j) CHECK(material.gerstner[i][j]==float(i*4+j+1));
}

TEST_CASE("WaterGeometry::standing grid rejects non-finite and overflowing coordinates")
{
	StandingWaterArea area;
	WaterGeometry::StandingGrid grid;
	auto ground = [](float, float) { return 0.0f; };
	for (float invalid : {std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
		1e12f, -1e12f})
	{
		for (int axis = 0; axis < 2; ++axis)
		{
			area.points = {{0,0},{40,0},{40,40},{0,40}};
			// Place invalid data after a valid first point: min/max must not hide NaN.
			if (axis == 0) area.points[1].x = invalid;
			else area.points[1].y = invalid;
			CHECK_FALSE(WaterGeometry::buildStandingGrid(area, {}, ground, grid));
			CHECK(grid.vertices.empty());
			CHECK(grid.indices.empty());
		}
	}
}
