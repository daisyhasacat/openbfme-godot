// OpenBFME. GPL-3.0.
//
// Water mesh geometry (no Godot dependency): ear-clipping triangulation of standing-water polygons and the
// river ribbon built from cross-section lines. Spec maps-and-terrain.md 3.6 ("triangulate each polygon (ear
// clipping; points may be non-convex) at z = waterHeight"; "rivers: a strip from consecutive line pairs, UV v
// along the strip"). River UVs follow BFME2 W3DWaterDrawRiverWater.cpp (RVA 0x100A0B):
// uv0 spans 0..0.5 across, uv1 spans 0..1, both share width-scaled V. The
// exact RotWK boundary generation and animation clock remain unverified (S-035).

#pragma once

#include "GameClient/MapChunks.h"

#include <cstdint>
#include <array>
#include <vector>
#include <functional>

struct MeshShaderMaterialDef;

namespace WaterGeometry
{
// Defaults from retail watershader.fxo. Authored technique indices select the
// supported PCA geometry/normal or bump paths; reflections remain S-035.
struct StandingMaterial
{
	unsigned technique = 0;
	float gerstner[3][4] = {{1, .1f, .1f, .5f}, { .00001f, .00001f, .00001f, .00001f}, { .00001f, .00001f, .00001f, .00001f}};
	float pcaUV[2] = { 1.5f, 3.0f }, pcaWaveUV[2] = { 0.01f, 0.0075f };
	float specular[3] = { 0.8f, 0.8f, 0.8f };
	float reflectionPerturbation = 0.005f;
	std::string pcaTexture[3], pcaNoiseTexture, pcaFrothTexture;
	float diffuse[4] = { 0.15f, 0.3f, 0.35f, 0.5f };
	float dimming = 0.8f, bumpScale = 0.2f, specularExponent = 50.0f;
	float windAngle = 0.0f, windSpeed = 0.02f;
	float waveAngle = 0.2f, waveSpeed = 0.0f;
	float bumpUV[2] = { 7.0f, 7.0f }, envUV[2] = { 0.2f, 0.2f }, waveUV[2] = { 7.0f, 7.0f };
	bool blendTerrainAlpha = true;
	std::string bumpTexture, envTexture, waveTexture;
};
bool readStandingMaterial(const MeshShaderMaterialDef &source, StandingMaterial &out);

// BFME2 donor StandingWaterAreaLoadPCA.cpp (0x3093F6) and
// Rva003093F6BasesSamplers.cpp (0x30822F..0x308509). Presentation only.
using PcaVector = std::array<float, 4>;
using PcaFrame = std::array<PcaVector, 10>; // mean, then X/Y/Z for groups 0..2
struct PcaBasis
{
	float speed = 0.0f;
	std::vector<PcaFrame> frames;
};
bool readPcaBasis(const std::string &text, PcaBasis &out, std::string *error);
PcaFrame samplePcaBasis(const PcaBasis &basis, float clientTime);
// fx_2_0 technique names in file order. Bounds-checked; no baked content indices.
bool readEffectTechniques(const std::vector<std::uint8_t> &bytes, std::vector<std::string> &names, std::string *error);

// Triangulates a simple polygon (either winding, non-convex allowed). Returns false for degenerate input
// (fewer than 3 points, zero area, or a polygon the ear search cannot finish). indices are triangle triples.
bool triangulate(const std::vector<Point2F> &polygon, std::vector<std::uint32_t> &indices);

// BFME2 W3DWater.cpp AllocateAndFillBuffers (0x8304A), high-detail grid.
// Wave query follows W3DWaterStandingWaveQuery.cpp (0x8026B).
struct StandingVertex
{
	Point2F point, nearest = {100000,100000}, direction = {1,0};
	float ground = 0, halfLength = 0;
};
struct StandingGrid
{
	std::vector<StandingVertex> vertices;
	std::vector<std::uint32_t> indices;
	float spacing = 20;
};
bool buildStandingGrid(const StandingWaterArea &area, const std::vector<StandingWaveArea> &waves,
	const std::function<float(float,float)> &ground, StandingGrid &out);

// Signed area (positive = counter-clockwise in a y-up frame).
float signedArea(const std::vector<Point2F> &polygon);

struct RiverStrip
{
	std::vector<float> position; // xyz SAGE, 2 vertices per cross-section line (left, right)
	std::vector<float> uv0;
	std::vector<float> uv1;
	std::vector<std::uint32_t> index;
};

// z = river.waterHeight. BFME2 donor boundary-width repeat scale; shader applies the animated V wobble.
bool buildRiverStrip(const RiverArea &river, RiverStrip &out);
} // namespace WaterGeometry
