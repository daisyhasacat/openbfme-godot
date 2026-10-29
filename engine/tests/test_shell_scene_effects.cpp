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
