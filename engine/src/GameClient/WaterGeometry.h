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
#include <vector>

namespace WaterGeometry
{
// Triangulates a simple polygon (either winding, non-convex allowed). Returns false for degenerate input
// (fewer than 3 points, zero area, or a polygon the ear search cannot finish). indices are triangle triples.
bool triangulate(const std::vector<Point2F> &polygon, std::vector<std::uint32_t> &indices);

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
