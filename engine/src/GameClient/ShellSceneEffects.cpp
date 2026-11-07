// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0,
// with BFME2 setFade/updateFades from Open-BFME-2 (addresses cited below).
#include "GameClient/ShellSceneEffects.h"
#include "GameClient/MapUtil.h"
#include "GameClient/ScriptCameraDirector.h"
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
#include <algorithm>
#include <functional>
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
	double seconds = 0, cameraStart = 0, cameraDuration = 0;
	NamedCamera cameraFrom, cameraTo;
	bool hasCamera = false;
	unsigned moves = 0;
	ParabolicEase cameraEase;

	NamedCamera sampleCamera(double at) const
	{
		if (cameraDuration <= 0) return cameraTo;
		const float t = cameraEase((float)((at - cameraStart) / cameraDuration));
		NamedCamera result = cameraTo;
		auto lerp = [t](float a, float b) { return a + (b-a)*t; };
		result.position = {lerp(cameraFrom.position.x,cameraTo.position.x), lerp(cameraFrom.position.y,cameraTo.position.y), 0};
		for (int i = 0; i < 6; ++i) result.values[i] = lerp(cameraFrom.values[i],cameraTo.values[i]);
		// W3DViewMoveAlongWaypointPath: shortest yaw arc. Its path initializer
		// (0x86B2C) sets averaging to one. Two extrapolated control points make
		// this two-marker path exactly linear in eased distance.
		float yaw = cameraFrom.values[2], delta = cameraTo.values[2]-yaw;
		constexpr float pi = 3.14159265359f;
		if (delta > pi) yaw += 2*pi;
		if (delta < -pi) yaw -= 2*pi;
		result.values[2] = lerp(yaw,cameraTo.values[2]);
		while (result.values[2] > pi) result.values[2] -= 2*pi;
		while (result.values[2] < -pi) result.values[2] += 2*pi;
		return result;
	}
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
	for (const auto &list : map.playerScripts.lists)
	{
		std::function<bool(const std::vector<ScriptItem>&)> check = [&](const auto &items) {
			for (const auto &item : items)
			{
				if (item.group && !check(item.group->items)) return false;
				if (!item.script) continue;
				for (const auto *actions : {&item.script->actions, &item.script->falseActions}) for (const auto &a : *actions)
				{
					if (a.internalName != "MOVE_CAMERA_TO") continue;
					const auto found = std::find_if(map.chunks.namedCameras.begin(), map.chunks.namedCameras.end(), [&](const auto &c) { return !a.params.empty() && c.name == a.params[0].stringValue; });
					if (a.params.size() != 5 || found == map.chunks.namedCameras.end())
					{ if (error) *error = "shell MOVE_CAMERA_TO has no named camera: " + (a.params.empty() ? std::string() : a.params[0].stringValue); return false; }
				}
			}
			return true;
		};
		if (!check(list.items)) return false;
	}
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
	state->logic.scriptEngine().newGame(sides, nullptr, &state->map.chunks.namedCameras, false);
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
	m->seconds = seconds;
	while (m->tick <= target)
	{
		m->fade.update();
		auto &engine = m->logic.scriptEngine();
		engine.update();
		for (const auto &r : engine.takeClientRequests())
			if (r.action == "CAMERA_FADE_ADD") { m->fade.set(r); ++m->starts; }
			else if (r.action == "MOVE_CAMERA_TO")
			{
				const auto found = std::find_if(m->map.chunks.namedCameras.begin(), m->map.chunks.namedCameras.end(), [&](const auto &c) { return c.name == r.params[0].stringValue; });
				if (found == m->map.chunks.namedCameras.end()) continue; // rejected during load
				const double at = double(m->tick) / LOGICFRAMES_PER_SECOND;
				m->cameraFrom = m->hasCamera ? m->sampleCamera(at) : *found;
				m->cameraTo = *found;
				m->cameraStart = at;
				m->cameraDuration = r.params[1].realValue;
				m->cameraEase.setEaseTimes(m->cameraDuration > 0 ? r.params[3].realValue / m->cameraDuration : 0,
					m->cameraDuration > 0 ? r.params[4].realValue / m->cameraDuration : 0);
				m->hasCamera = true;
				++m->moves;
			}
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

bool ShellSceneEffects::camera(NamedCamera &out) const
{
	if (!m || !m->hasCamera) return false;
	out = m->sampleCamera(m->seconds);
	return true;
}
unsigned ShellSceneEffects::cameraMoves() const { return m ? m->moves : 0; }
std::string ShellSceneEffects::cameraName() const { return m && m->hasCamera ? m->cameraTo.name : std::string(); }

void ShellSceneEffects::cameraPose(const NamedCamera &camera, const Coord3D &offset, Coord3D &eye, Coord3D &target)
{
	// BFME2 named-camera move 0x8A2EF sends serialized Pitch to slot 61
	// (0x88FF6), which updates View+0x70: descriptor fxPitch, NOT the
	// ordinary pitch rotation at View+0x2C. W3DCamTransform 0x102F83 uses
	// it to adjust the look-at height. The supported shell script graph
	// never changes that ordinary rotation (constructor value zero).
	const float pitch = camera.values[0], yaw = camera.values[2], zoom = camera.values[3], ground = camera.values[5];
	const float x = offset.x*zoom, y = offset.y*zoom, z = offset.z*zoom;
	const float scale = 1.0f - ground/z;
	eye = {camera.position.x + (std::cos(yaw)*x - std::sin(yaw)*y)*scale,
		camera.position.y + (std::sin(yaw)*x + std::cos(yaw)*y)*scale, ground + z*scale};
	target = {camera.position.x, camera.position.y, ground};
	if (pitch <= 1.0f) target.z = eye.z - (eye.z-target.z)*pitch;
	else
	{
		eye.x = (eye.x-target.x)/pitch + target.x;
		eye.y = (eye.y-target.y)/pitch + target.y;
	}
}
