// OpenBFME. GPL-3.0. Capture-link regressions for map-authored dock flags.
#include "doctest.h"
#include "LogicTestUtil.h"
#include "GameLogic/CaptureLinks.h"
namespace
{
struct World : logictest::LogicWorld
{
	World()
	{
		REQUIRE(w.load("Object Dock\n KindOf = STRUCTURE PORT LINKED_TO_FLAG SELECTABLE\nEnd\nObject Building\n KindOf = STRUCTURE LINKED_TO_FLAG "
		               "SELECTABLE\nEnd\nObject Flag\n KindOf = STRUCTURE CAPTUREFLAG CAPTURABLE SELECTABLE\nEnd\n")
		            .empty());
	}
	Object *place(const char *name, float x, float y = 0)
	{
		auto *o = make(name, teamOf("Alice"));
		const Coord3D p{x, y, 0};
		o->setPosition(&p);
		return o;
	}
};
} // namespace
TEST_CASE("capture links: normal flags retain the 150-unit building association")
{
	World w;
	auto *dock = w.place("Dock", 100);
	auto *flag = w.place("Flag", 240);
	CHECK(CaptureLinks::buildingForFlag(*flag) == dock);
	CHECK(CaptureLinks::flagForBuilding(*dock) == flag);
}
TEST_CASE("capture links: a detached Evendim-style flag captures its reciprocal dock")
{
	World w;
	auto *dock = w.place("Dock", 100);
	auto *flag = w.place("Flag", 277);
	CHECK(CaptureLinks::buildingForFlag(*flag) == dock);
	CHECK(CaptureLinks::flagForBuilding(*dock) == flag);
}
TEST_CASE("capture links: long association applies to ports only and stays bounded")
{
	World w;
	w.place("Building", 100);
	auto *flag = w.place("Flag", 277);
	CHECK(CaptureLinks::buildingForFlag(*flag) == nullptr);
	auto *dock = w.place("Dock", 1000);
	auto *far = w.place("Flag", 1251);
	CHECK(CaptureLinks::buildingForFlag(*far) == nullptr);
	CHECK(CaptureLinks::flagForBuilding(*dock) == nullptr);
}
TEST_CASE("capture links: an extra detached flag cannot take another flag's dock")
{
	World w;
	auto *dock = w.place("Dock", 100);
	auto *near = w.place("Flag", 230);
	auto *extra = w.place("Flag", 277);
	CHECK(CaptureLinks::flagForBuilding(*dock) == near);
	CHECK(CaptureLinks::buildingForFlag(*extra) == nullptr);
}
