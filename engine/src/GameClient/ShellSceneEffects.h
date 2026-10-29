// OpenBFME. GPL-3.0.
#pragma once

#include <memory>
#include <string>

struct LoadedMap;
struct NamedCamera;
struct Coord3D;

// Presentation-owned script context: never steps or shares the active game.
// Uses the existing script engine for timer/counter/enable ordering. Only the
// supported shell timing/presentation actions are accepted, not arbitrary gameplay.
class ShellSceneEffects
{
public:
	ShellSceneEffects();
	~ShellSceneEffects();
	bool load(LoadedMap &&map, std::string *error);
	void advance(double seconds);
	float fadeAdd() const;
	// Render-only interpolation; the script clock and tick values remain unchanged.
	float renderFadeAdd() const;
	unsigned fadeStarts() const;
	unsigned ticks() const;
	bool camera(NamedCamera &out) const;
	static void cameraPose(const NamedCamera &camera, const Coord3D &offset, Coord3D &eye, Coord3D &target);
	unsigned cameraMoves() const;
	std::string cameraName() const;
private:
	struct State;
	std::unique_ptr<State> m;
};
