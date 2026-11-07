// OpenBFME. GPL-3.0. Independent geometric contracts for automatic fishing spots.
#include "doctest.h"
#include "GameLogic/FishingPlacement.h"
#include <cmath>
#include <limits>
using namespace FishingPlacement;
namespace
{
bool ocean(float x, float y, float &depth, float &surface)
{
	if (x < 200 || y < 200 || x > 2800 || y > 2800)
		return false;
	depth = 40;
	surface = 60;
	return true;
}
float distance(const Coord3D &a, const Coord3D &b)
{
	const float dx = a.x - b.x, dy = a.y - b.y;
	return std::sqrt(dx * dx + dy * dy);
}
} // namespace
TEST_CASE("fishing placement: dry maps, shallow water and narrow channels have no spots")
{
	Settings s;
	CHECK(positions(s, 3000, 3000, [](float, float, float &, float &) { return false; }).empty());
	CHECK(positions(s, 3000, 3000, [](float, float, float &d, float &z) {
		      d = 19;
		      z = 60;
		      return true;
	      }).empty());
	CHECK(positions(s, 3000, 3000, [](float x, float, float &d, float &z) {
		      d = 40;
		      z = 60;
		      return x > 1000 && x < 1100;
	      }).empty());
}
TEST_CASE("fishing placement: spots stay in deep open water, clear of banks and each other")
{
	Settings s;
	const auto points = positions(s, 3000, 3000, ocean);
	REQUIRE_FALSE(points.empty());
	CHECK(points.size() <= (size_t)s.maxSpots); // tolerance: the configured upper bound is the placement contract
	for (size_t i = 0; i < points.size(); ++i)
	{
		const auto &p = points[i];
		CHECK(p.x >= 320);  // tolerance: bank at x=200 plus the configured 120-unit shore clearance
		CHECK(p.y >= 320);  // tolerance: bank at y=200 plus the configured 120-unit shore clearance
		CHECK(p.x <= 2680); // tolerance: bank at x=2800 minus the configured 120-unit shore clearance
		CHECK(p.y <= 2680); // tolerance: bank at y=2800 minus the configured 120-unit shore clearance
		CHECK(p.z == 60);
		for (size_t j = 0; j < i; ++j)
			CHECK(distance(p, points[j]) >= 650); // tolerance: any two schools must meet the configured minimum separation
	}
}
TEST_CASE("fishing placement: authored spots reserve their area and count against the cap")
{
	Settings s;
	s.maxSpots = 4;
	const std::vector<Coord3D> authored{{1500, 1500, 60}};
	const auto points = positions(s, 3000, 3000, ocean, authored);
	REQUIRE(points.size() == 3);
	for (const auto &p : points)
		CHECK(distance(p, authored[0]) >= 650); // tolerance: authored schools have the same minimum separation as generated ones
	CHECK(positions(s, 3000, 3000, ocean, std::vector<Coord3D>(4, authored[0])).empty());
}
TEST_CASE("fishing placement: identical terrain and settings produce identical distributed positions")
{
	Settings s;
	const auto a = positions(s, 3000, 3000, ocean), b = positions(s, 3000, 3000, ocean);
	REQUIRE_FALSE(a.empty());
	REQUIRE(a.size() == b.size());
	CHECK(positions(s, 3000, 3000, ocean, a).empty());
	bool west = false, east = false, north = false, south = false;
	for (size_t i = 0; i < a.size(); ++i)
	{
		CHECK(a[i].x == b[i].x);
		CHECK(a[i].y == b[i].y);
		CHECK(a[i].z == b[i].z);
		west |= a[i].x < 1500;
		east |= a[i].x > 1500;
		south |= a[i].y < 1500;
		north |= a[i].y > 1500;
	}
	CHECK(west);
	CHECK(east);
	CHECK(north);
	CHECK(south);
}
TEST_CASE("fishing placement: malformed or unbounded placement settings are refused")
{
	Settings s;
	s.step = 0;
	CHECK_THROWS(validate(s));
	s = Settings{};
	s.spacing = 1;
	CHECK_THROWS(validate(s));
	s = Settings{};
	s.minDepth = std::numeric_limits<float>::infinity();
	CHECK_THROWS(validate(s));
	s = Settings{};
	s.maxSpots = 0;
	CHECK_THROWS(validate(s));
	s = Settings{};
	CHECK_THROWS(positions(s, 0, 3000, ocean));
}
