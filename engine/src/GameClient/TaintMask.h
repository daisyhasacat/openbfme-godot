// OpenBFME. GPL-3.0.
//
// The terrain's taint mask (stop S-6111): what W3DTaint keeps per cell of TheTaintManager and hands the terrain shader as TaintMaskTexture.
//
// DONOR FACTS (BFME2 1.06 decompile, Open-BFME-2 Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DTaint.cpp):
//   * setTaintLevel (0x00073CC0, x, y, level): nothing without GameData TaintOn; on the mode <= 0 path a level below 0x80 takes the low colour (GlobalData
//     + 0xBEC, TaintColor) and one above takes the high colour (+ 0xBF8, ElvenWoodColor): fraction = (0x80 - level) / 128 (below) or (level - 0x80) * (1 / 127)
//     (above), channel = (1 - fraction) + fraction * (colour + adjustment) with the adjustments red 0.03 when x % 5 == 0 and y % 2 == 0 (else -0.02), green
//     -0.02 when x % 3 == 0 and y % 5 == 0 (else 0.03), blue -0.03 when x % 2 == 0 and y % 3 == 0 (else 0.02); neutral is (1, 1, 1); each channel times
//     the shroud level, truncated to an unsigned and masked to a byte; the texel is A = level, R, G, B;
//   * the texture is the cells plus a one-texel border (init: numCells + 2, the cells copied at texel (1, 1));
// TARGET SHADER FACTS: RotWK terrain.fxo technique Default, ps_2_0 (blob 35) with vs blob 60: the mask at (world.xy + Taint[1].xy) * Taint[2].xy; Taint[0].x != 0 enables it;
//     w = mask.a: albedo = mask.rgb * lerp(base, w <= 0.5 ? TaintLow : TaintHigh, 2 * |w - 0.5|), TaintLow / TaintHigh sampled at the composed terrain patch texcoord;
//     the textures are TSTaintMed.tga and TSElvenMed.tga (RotWK's names at RW 0x7DBCD4 / 0x7DBCE4).
// DONOR: shader terrain uses white mask RGB; only mode <= 0 uses the GameData tint (W3DTaint.cpp).
// W3DTerrainBackgroundDoTesselatedUpdate.cpp maps t0 across 16 cells (HeightMapInitHeightDataRva000E1A39.cpp), with V inverted.
// INFERENCE (S-6111): the shroud level is 255 (separate shroud), FireManager colour is white; presentation retains elapsed intervals below 16 ms to avoid zero steps above 64 Hz; border texels are neutral (retail fills them with TaintAlpha), the mask UV maps world (origin - cellSize) to texel 0's corner as the shroud's does.

#pragma once

#include <cstdint>
#include <vector>

struct TaintView;

struct TaintColors
{
	bool shaderTerrain = false;        ///< BFME2 W3DTaint: mode > 0 leaves RGB white (FireManager/shroud handled separately)
	bool on = false;                    ///< GameData TaintOn
	float low[3] = { 1.0f, 1.0f, 1.0f };  ///< GameData TaintColor (0 .. 1)
	float high[3] = { 1.0f, 1.0f, 1.0f }; ///< GameData ElvenWoodColor (0 .. 1)
};

namespace TaintMask
{
// BFME2 W3DTaintInterpolate.cpp (0x741C0): advance displayed bytes toward
// logic targets by min(int(elapsedMs * 0.064f), 128), without changing logic.
bool interpolate(const std::vector<std::uint8_t> &target, std::vector<std::uint8_t> &displayed, std::uint32_t elapsedMs);
// setTaintLevel's texel of cell (x, y) at `level`: rgba[0..3] = R, G, B, A (A = the level)
void texel(int x, int y, int level, const TaintColors &colors, std::uint8_t rgba[4]);
// the mask texture: (countX + 2) x (countY + 2) RGBA8 texels, the cells at texel (1, 1), the border neutral
void build(const TaintView &view, const TaintColors &colors, std::vector<std::uint8_t> &rgba, int &width, int &height);
// whether any cell of the view is not neutral (the shader can skip the mask while nothing is painted)
bool anyPainted(const TaintView &view);
}
