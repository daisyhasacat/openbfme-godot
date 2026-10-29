// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0,
// with BFME2 setFade/updateFades from Open-BFME-2 (addresses cited below).
#include "GameClient/ShellSceneEffects.h"
#include "GameClient/MapUtil.h"
#include "Common/NameKeyGenerator.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Team.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/RwGrammar.h"
#include "Common/Thing/ThingFactory.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include <cmath>
#include <set>

namespace
{
// BFME2 ScriptEngine.cpp setFade (0x203777), updateFades (0x2036CC):
// update before script evaluation, zero-increase set calls update immediately,
// inclusive phase ends, and the decrease divides by (framesDecrease + 1).
struct AddFade
{
	float minimum = 0, maximum = 0, value = 0;
	int increase = 0, hold = 0, decrease = 0, frame = 0;
	bool active = false;
	void update()
	{
		if (!active) return;
		int f = ++frame;
		if (f <= increase) value = minimum + float(f) / float(increase) * (maximum - minimum);
		else if ((f -= increase) <= hold) value = maximum;
		else if ((f -= hold) <= decrease) value = maximum + float(f) / float(decrease + 1) * (minimum - maximum);
		else { active = false; value = 0; }
	}
	void set(const ScriptClientRequest &request)
	{
		minimum = request.params[0].realValue; maximum = request.params[1].realValue;
		increase = request.params[2].intValue; hold = request.params[3].intValue; decrease = request.params[4].intValue;
		frame = 0; active = true;
		if (increase == 0) update();
	}
};

bool validate(const std::vector<ScriptItem> &items, std::string *error)
{
	const std::set<std::string> conditions = {"CONDITION_TRUE", "CONDITION_FALSE", "COUNTER", "TIMER_EXPIRED"};
	const std::set<std::string> actions = {"CAMERA_FADE_ADD", "SET_MILLISECOND_TIMER", "ENABLE_SCRIPT", "DISABLE_SCRIPT",
		"MOVE_CAMERA_TO", "SET_CAMERA_CLIP_DEPTH_MULTIPLIER", "TERRAIN_RENDER_DISABLE", "HIDE_UI", "TOGGLE_AVI_CAPTURE", "NO_OP"};
	for (const auto &item : items)
	{
		if (item.group && !validate(item.group->items, error)) return false;
		if (!item.script) continue;
		const auto &script = *item.script;
		if (script.delayEvaluationSeconds != 0 || script.actionsFireSequentially || script.isSubroutine)
		{ *error = "shell effect script requires unsupported scheduling: " + script.name; return false; }
		for (const auto &term : script.orConditions) for (const auto &c : term.conditions)
			if (!conditions.count(c.internalName)) { *error = "unsupported shell effect condition: " + c.internalName; return false; }
		for (const auto *list : {&script.actions, &script.falseActions}) for (const auto &a : *list)
		{
			if (!actions.count(a.internalName)) { *error = "unsupported shell effect action: " + a.internalName; return false; }
			if (a.internalName == "CAMERA_FADE_ADD" && (a.params.size() != 5 || a.params[2].intValue < 0 || a.params[3].intValue < 0 || a.params[4].intValue < 0))
			{ *error = "invalid shell CAMERA_FADE_ADD parameters"; return false; }
		}
	}
	return true;
}
}

struct ShellSceneEffects::State
{
	LoadedMap map;
	NameKeyGenerator keys;
	RwGrammar grammar{RwBinaryData::embedded()};
	ModuleFactory modules{keys, grammar};
	ThingFactory things{keys, modules, grammar};
	PlayerTemplateStore templates{keys};
	TeamFactory teams;
	PlayerList players{keys, templates, teams};
	GameLogic logic{things, modules, players, RandomAlgorithm::ZH_CarryChain};
	AddFade fade;
	unsigned tick = 0, starts = 0;
	float fraction = 0;
};

ShellSceneEffects::ShellSceneEffects() = default;
ShellSceneEffects::~ShellSceneEffects() = default;

bool ShellSceneEffects::load(LoadedMap &&map, std::string *error)
{
	m.reset();
	if (error) error->clear();
	std::string failure;
	for (const auto &list : map.playerScripts.lists)
		if (!validate(list.items, &failure)) { if (error) *error = failure; return false; }
	auto state = std::make_unique<State>();
	state->map = std::move(map);
	std::vector<ScriptEngine::SideScripts> sides;
	for (size_t i = 0; i < state->map.playerScripts.lists.size(); ++i)
	{
		ScriptEngine::SideScripts side;
		if (i < state->map.sides.sides.size()) side.sideName = state->map.sides.sides[i].dict.getAsciiString("playerName");
		side.lists.push_back(&state->map.playerScripts.lists[i]);
		sides.push_back(std::move(side));
	}
	state->logic.scriptEngine().newGame(sides, nullptr, nullptr, false);
	m = std::move(state);
	return true;
}

void ShellSceneEffects::advance(double seconds)
{
	if (!m || !std::isfinite(seconds) || seconds < 0) return;
	// Script timers and fades advance at the source's logic rate, independently
	// of render cadence. Catch up every tick so ENABLE_SCRIPT and timer edges survive stalls.
	const double frame = seconds * LOGICFRAMES_PER_SECOND;
	const auto target = (unsigned long long)std::floor(frame);
	m->fraction = (float)(frame - std::floor(frame));
	while (m->tick <= target)
	{
		m->fade.update();
		auto &engine = m->logic.scriptEngine();
		engine.update();
		for (const auto &r : engine.takeClientRequests())
			if (r.action == "CAMERA_FADE_ADD") { m->fade.set(r); ++m->starts; }
		++m->tick;
	}
}
float ShellSceneEffects::fadeAdd() const { return m ? m->fade.value : 0; }
float ShellSceneEffects::renderFadeAdd() const
{
	if (!m) return 0;
	// Sample only the current fade's next value. Never evaluate future scripts:
	// timer edges and replacement fades still occur on their original ticks.
	AddFade next = m->fade;
	next.update();
	return m->fade.value + m->fraction * (next.value - m->fade.value);
}
unsigned ShellSceneEffects::fadeStarts() const { return m ? m->starts : 0; }
unsigned ShellSceneEffects::ticks() const { return m ? m->tick : 0; }
