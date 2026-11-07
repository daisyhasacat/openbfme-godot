// OpenBFME. GPL-3.0. See GameClient/TaintMask.h.

#include "GameClient/TaintMask.h"

#include "GameClient/LogicSnapshot.h"

#include <algorithm>

namespace TaintMask
{
bool interpolate(const std::vector<std::uint8_t> &target, std::vector<std::uint8_t> &displayed, std::uint32_t elapsedMs)
{
	bool changed = displayed.size() != target.size();
	if (changed) displayed.assign(target.size(), 0x80);
	const int step = (int)std::min((float)elapsedMs * 0.064f, 128.0f);
	for (size_t i = 0; i < target.size(); ++i)
	{
		const int start = displayed[i], final = target[i];
		const int next = final < start ? std::max(final, start - step) : std::min(final, start + step);
		changed = changed || next != start;
		displayed[i] = (std::uint8_t)next;
	}
	return changed;
}

// W3DTaint::setTaintLevel BFME2 0x00073CC0: legacy tint only for mode <= 0.
void texel(int x, int y, int level, const TaintColors &colors, std::uint8_t rgba[4])
{
	float red = 1.0f, green = 1.0f, blue = 1.0f;
	const int shroudLevel = 255; // S-6111: the port's shroud has its own texture
	if (!colors.shaderTerrain && level != 0x80)
	{
		const float *colour = level < 0x80 ? colors.low : colors.high;
		const float fraction = level < 0x80 ? (float)(0x80 - level) * 0.0078125f : (float)(level - 0x80) * 0.007874016f;
		const float base = 1.0f - fraction;
		const float redAdjustment = (x % 5 == 0 && y % 2 == 0) ? 0.03f : -0.02f;
		const float greenAdjustment = (x % 3 == 0 && y % 5 == 0) ? -0.02f : 0.03f;
		const float blueAdjustment = (x % 2 == 0 && y % 3 == 0) ? -0.03f : 0.02f;
		red = base + fraction * (colour[0] + redAdjustment);
		green = base + fraction * (colour[1] + greenAdjustment);
		blue = base + fraction * (colour[2] + blueAdjustment);
	}
	rgba[0] = (std::uint8_t)((unsigned)(red * (float)shroudLevel) & 0xFFu);
	rgba[1] = (std::uint8_t)((unsigned)(green * (float)shroudLevel) & 0xFFu);
	rgba[2] = (std::uint8_t)((unsigned)(blue * (float)shroudLevel) & 0xFFu);
	rgba[3] = (std::uint8_t)level;
}

void build(const TaintView &view, const TaintColors &colors, std::vector<std::uint8_t> &rgba, int &width, int &height)
{
	width = view.countX + 2;
	height = view.countY + 2;
	rgba.assign((size_t)width * (size_t)height * 4u, 0);
	std::uint8_t neutral[4];
	texel(0, 0, 0x80, colors, neutral);
	for (size_t i = 0; i < (size_t)width * (size_t)height; ++i)
	{
		for (int k = 0; k < 4; ++k)
		{
			rgba[i * 4 + (size_t)k] = neutral[k];
		}
	}
	for (int y = 0; y < view.countY; ++y)
	{
		for (int x = 0; x < view.countX; ++x)
		{
			const int level = view.kinds[(size_t)y * (size_t)view.countX + (size_t)x];
			texel(x, y, level, colors, &rgba[(((size_t)y + 1) * (size_t)width + (size_t)x + 1) * 4]);
		}
	}
}

bool anyPainted(const TaintView &view)
{
	for (std::uint8_t k : view.kinds)
	{
		if (k != 0x80)
		{
			return true;
		}
	}
	return false;
}
} // namespace TaintMask
