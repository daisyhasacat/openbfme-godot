// OpenBFME. GPL-3.0. Expected fade edges from BFME2 ScriptEngine
// 0x203777 / 0x2036CC, not sampled from the implementation.
#include "doctest.h"
#include "ScriptTestUtil.h"
#include "GameClient/MapUtil.h"
#include "GameClient/ShellSceneEffects.h"
using namespace scripttest;

namespace
{
LoadedMap opening(int increase, int hold, int decrease)
{
	LoadedMap map;
	map.playerScripts.lists.resize(1);
	map.playerScripts.lists[0].items.push_back(item(script("opening", {{cond("CONDITION_TRUE", {})}},
		{act("CAMERA_FADE_ADD", {rp(0), rp(1), ip(0,increase), ip(0,hold), ip(0,decrease)})}, {}, true)));
	return map;
}
}
TEST_CASE("shell effects: BFME2 opening fade starts white and uses inclusive hold and decrease plus one")
{
	ShellSceneEffects effects; std::string error;
	REQUIRE(effects.load(opening(0,6,12), &error));
	effects.advance(0);
	CHECK(effects.fadeAdd() == 1.0f);
	CHECK(effects.fadeStarts() == 1);
	effects.advance(1.0); // immediate frame 1 plus five ticks: frame 6 still holds
	CHECK(effects.fadeAdd() == 1.0f);
	effects.advance(1.2); // frame 7: decrease frame 1 / (12 + 1)
	CHECK(effects.fadeAdd() == 1.0f - 1.0f/13.0f);
	effects.advance(3.4); // frame 18: decrease 12 / 13, not zero yet
	CHECK(effects.fadeAdd() == 1.0f - 12.0f/13.0f);
	effects.advance(3.6);
	CHECK(effects.fadeAdd() == 0.0f);
	CHECK(effects.fadeStarts() == 1);
}
TEST_CASE("shell effects: authored timer fade survives a render stall and has the source increase hold decrease boundaries")
{
	auto map = opening(0,0,0);
	auto &items = map.playerScripts.lists[0].items;
	items[0].script->actions = {act("SET_MILLISECOND_TIMER", {sp(4,"cut"),rp(18)})};
	items.push_back(item(script("fade", {{cond("TIMER_EXPIRED", {sp(4,"cut")})}},
		{act("CAMERA_FADE_ADD", {rp(0),rp(1),ip(0,8),ip(0,8),ip(0,12)})}, {}, true)));
	ShellSceneEffects effects; std::string error;
	REQUIRE(effects.load(std::move(map), &error));
	effects.advance(17.8);
	CHECK(effects.fadeStarts() == 0);
	effects.advance(18.0);
	CHECK(effects.fadeStarts() == 1);
	CHECK(effects.fadeAdd() == 0.0f);
	effects.advance(19.6);
	CHECK(effects.fadeAdd() == 1.0f);
	effects.advance(21.2);
	CHECK(effects.fadeAdd() == 1.0f);
	effects.advance(21.4);
	CHECK(effects.fadeAdd() == 1.0f - 1.0f/13.0f);
	effects.advance(23.8);
	CHECK(effects.fadeAdd() == 0.0f);
	CHECK(effects.fadeStarts() == 1);
}
TEST_CASE("shell effects: unsupported script behavior fails explicitly and reload clears a prior fade")
{
	ShellSceneEffects effects; std::string error;
	REQUIRE(effects.load(opening(0,6,12), &error));
	effects.advance(0);
	auto map = opening(0,6,12);
	map.playerScripts.lists[0].items[0].script->actions[0].internalName = "CREATE_OBJECT";
	CHECK_FALSE(effects.load(std::move(map), &error));
	CHECK(error == "unsupported shell effect action: CREATE_OBJECT");
	CHECK(effects.fadeAdd() == 0.0f);
	REQUIRE(effects.load(opening(0,6,12), &error));
	effects.advance(0);
	CHECK(effects.fadeStarts() == 1);
}

TEST_CASE("shell effects: repeated timer fades are identical at fine and stalled render cadences")
{
	auto looping = []() {
		auto map = opening(0,0,0);
		auto &items = map.playerScripts.lists[0].items;
		items[0].script->actions = {act("SET_MILLISECOND_TIMER", {sp(4,"cut"),rp(1)})};
		items.push_back(item(script("repeat", {{cond("TIMER_EXPIRED", {sp(4,"cut")})}},
			{act("CAMERA_FADE_ADD", {rp(0),rp(1),ip(0,2),ip(0,2),ip(0,2)}),
				act("SET_MILLISECOND_TIMER", {sp(4,"cut"),rp(2)})})));
		return map;
	};
	ShellSceneEffects fine, stalled; std::string error;
	REQUIRE(fine.load(looping(), &error));
	REQUIRE(stalled.load(looping(), &error));
	for (int frame = 0; frame <= 50; ++frame) fine.advance(frame / 5.0);
	stalled.advance(10.0);
	CHECK(stalled.ticks() == 51);
	CHECK(stalled.fadeStarts() == 5); // seconds 1, 3, 5, 7, 9
	CHECK(stalled.fadeAdd() == 1.0f - 1.0f/3.0f);
	CHECK(fine.fadeStarts() == stalled.fadeStarts());
	CHECK(fine.fadeAdd() == stalled.fadeAdd());
	stalled.advance(10.0);
	CHECK(stalled.ticks() == 51);
	CHECK(stalled.fadeStarts() == 5);
}

TEST_CASE("shell effects: display samples interpolate without advancing script ticks or shortening the white hold")
{
	ShellSceneEffects effects; std::string error;
	REQUIRE(effects.load(opening(2,2,2), &error));
	effects.advance(0.0);
	CHECK(effects.renderFadeAdd() == 0.0f);
	effects.advance(0.05);
	CHECK(effects.renderFadeAdd() == 0.125f);
	effects.advance(0.10);
	CHECK(effects.renderFadeAdd() == 0.25f);
	effects.advance(0.15);
	CHECK(effects.renderFadeAdd() == 0.375f);
	CHECK(effects.fadeAdd() == 0.0f);
	CHECK(effects.ticks() == 1);
	CHECK(effects.fadeStarts() == 1);
	effects.advance(0.2);
	CHECK(effects.fadeAdd() == 0.5f);
	CHECK(effects.renderFadeAdd() == 0.5f);
	effects.advance(0.3);
	CHECK(effects.renderFadeAdd() == 0.75f);
	effects.advance(0.4);
	CHECK(effects.renderFadeAdd() == 1.0f);
	effects.advance(0.75);
	CHECK(effects.renderFadeAdd() == 1.0f);
	effects.advance(1.4);
	CHECK(effects.renderFadeAdd() == 0.0f);
	CHECK(effects.fadeStarts() == 1);
}

TEST_CASE("shell cameras: named move interpolates all authored fields and rejects a missing marker")
{
	auto map = opening(0,0,0);
	NamedCamera a; a.name = "start"; a.position = {10,20,0};
	a.values[0] = -0.25f; a.values[3] = 0.25f; a.values[4] = 0.8f; a.values[5] = 16;
	NamedCamera b = a; b.name = "end"; b.position = {110,220,0}; b.values[0] = -0.75f; b.values[3] = 0.75f;
	map.chunks.namedCameras = {a,b};
	map.playerScripts.lists[0].items[0].script->actions = {
		act("MOVE_CAMERA_TO", {sp(51,"start"),rp(0),rp(0),rp(0),rp(0)}),
		act("MOVE_CAMERA_TO", {sp(51,"end"),rp(20),rp(0),rp(0),rp(0)})};
	ShellSceneEffects effects; std::string error;
	REQUIRE(effects.load(std::move(map), &error));
	effects.advance(10);
	NamedCamera camera;
	REQUIRE(effects.camera(camera));
	CHECK(camera.position.x == 60);
	CHECK(camera.position.y == 120);
	CHECK(camera.values[0] == -0.5f);
	CHECK(camera.values[3] == 0.5f);
	CHECK(camera.values[4] == 0.8f);
	CHECK(camera.values[5] == 16);
	CHECK(effects.cameraName() == "end");
	CHECK(effects.cameraMoves() == 2);
	effects.advance(20);
	REQUIRE(effects.camera(camera));
	CHECK(camera.position.x == 110);
	CHECK(camera.position.y == 220);
	CHECK(camera.values[0] == -0.75f);
	auto missing = opening(0,0,0);
	missing.playerScripts.lists[0].items[0].script->actions = {act("MOVE_CAMERA_TO", {sp(51,"missing"),rp(0),rp(0),rp(0),rp(0)})};
	CHECK_FALSE(effects.load(std::move(missing), &error));
	CHECK(error == "shell MOVE_CAMERA_TO has no named camera: missing");
}
TEST_CASE("shell cameras: BFME2 named Pitch adjusts look-at height, not the ordinary pitch rotation")
{
	NamedCamera camera; camera.position = {100,200,0};
	camera.values[0] = -0.5f; camera.values[3] = 0.5f; camera.values[5] = 10;
	Coord3D eye, target;
	ShellSceneEffects::cameraPose(camera, {0,-100,100}, eye, target);
	CHECK(eye.x == 100);
	CHECK(eye.y == 160);
	CHECK(eye.z == 50);
	CHECK(target.x == 100);
	CHECK(target.y == 200);
	CHECK(target.z == 70); // source Z - (source Z - ground) * fxPitch
}

#include "RetailTestMount.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
TEST_CASE("shell cameras retail: the four authored camera segments and white fades repeat")
{
	auto *mount = retailtest::bfme2Mount();
	if (!mount) { retailtest::printSkip("BFME2 shell camera script cycle"); return; }
	REQUIRE(mount->error.empty());
	std::vector<std::uint8_t> bytes; std::string error;
	REQUIRE(mount->fs->readFile("maps/shellmap1/shellmap1.map", bytes, &error));
	LoadedMap map;
	REQUIRE(MapReader::load(bytes, "maps/shellmap1/shellmap1.map", {}, map, &error));
	CHECK(map.chunks.namedCameras.size() == 8);
	ShellSceneEffects effects;
	REQUIRE(effects.load(std::move(map), &error));
	// Expected from the map's four 20-second moves, the CameraMove* == 1
	// countdown conditions and file-order ENABLE_SCRIPT Reset actions.
	const std::vector<std::pair<double,std::string>> stops = {{0,"CameraStart"}, {1,"CameraEnd"},
		{21,"CameraStartB"}, {22,"CameraEndB"}, {41.4,"CameraStartC"}, {42.2,"CameraEndC"},
		{62.0,"CameraStartD"}, {63,"CameraEndD"}, {84,"CameraEnd"}};
	for (const auto &stop : stops)
	{
		effects.advance(stop.first);
		CHECK(effects.cameraName() == stop.second);
	}
	CHECK(effects.cameraMoves() == 10);
	CHECK(effects.fadeStarts() == 5);
}
