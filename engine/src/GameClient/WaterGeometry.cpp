// OpenBFME. GPL-3.0.
// See GameClient/WaterGeometry.h.

#include "GameClient/WaterGeometry.h"
#include "Common/AsciiString.h"
#include "Libraries/WWVegas/WW3D2/meshmdl.h"

#include <algorithm>
#include <cmath>

namespace
{

float cross(const Point2F &o, const Point2F &a, const Point2F &b)
{
	return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

bool pointInTriangle(const Point2F &p, const Point2F &a, const Point2F &b, const Point2F &c)
{
	float d1 = cross(a, b, p), d2 = cross(b, c, p), d3 = cross(c, a, p);
	bool neg = d1 < 0 || d2 < 0 || d3 < 0;
	bool pos = d1 > 0 || d2 > 0 || d3 > 0;
	return !(neg && pos);
}

} // namespace

float WaterGeometry::signedArea(const std::vector<Point2F> &p)
{
	double a = 0;
	for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++)
	{
		a += (double)p[j].x * p[i].y - (double)p[i].x * p[j].y;
	}
	return (float)(a * 0.5);
}

bool WaterGeometry::triangulate(const std::vector<Point2F> &polygon, std::vector<std::uint32_t> &indices)
{
	indices.clear();
	const size_t n = polygon.size();
	if (n < 3)
	{
		return false;
	}
	float area = signedArea(polygon);
	if (std::fabs(area) < 1e-6f)
	{
		return false;
	}
	std::vector<std::uint32_t> v(n);
	for (size_t i = 0; i < n; ++i)
	{
		v[i] = (std::uint32_t)(area > 0 ? i : n - 1 - i); // counter-clockwise order
	}
	size_t guard = 0;
	while (v.size() > 3 && guard++ < n * n + 8)
	{
		bool clipped = false;
		for (size_t i = 0; i < v.size(); ++i)
		{
			const Point2F &a = polygon[v[(i + v.size() - 1) % v.size()]];
			const Point2F &b = polygon[v[i]];
			const Point2F &c = polygon[v[(i + 1) % v.size()]];
			if (cross(a, b, c) <= 0)
			{
				continue; // reflex or collinear
			}
			bool ear = true;
			for (size_t k = 0; k < v.size() && ear; ++k)
			{
				const std::uint32_t id = v[k];
				if (id == v[(i + v.size() - 1) % v.size()] || id == v[i] || id == v[(i + 1) % v.size()])
				{
					continue;
				}
				const Point2F &p = polygon[id];
				if ((p.x == a.x && p.y == a.y) || (p.x == b.x && p.y == b.y) || (p.x == c.x && p.y == c.y))
				{
					continue; // duplicated vertex positions do not block
				}
				if (pointInTriangle(p, a, b, c))
				{
					ear = false;
				}
			}
			if (!ear)
			{
				continue;
			}
			indices.push_back(v[(i + v.size() - 1) % v.size()]);
			indices.push_back(v[i]);
			indices.push_back(v[(i + 1) % v.size()]);
			v.erase(v.begin() + (long)i);
			clipped = true;
			break;
		}
		if (!clipped)
		{
			// no ear found (self-intersecting or collinear leftovers): drop collinear vertices once, else give up
			bool removed = false;
			for (size_t i = 0; i < v.size(); ++i)
			{
				const Point2F &a = polygon[v[(i + v.size() - 1) % v.size()]];
				const Point2F &b = polygon[v[i]];
				const Point2F &c = polygon[v[(i + 1) % v.size()]];
				if (std::fabs(cross(a, b, c)) < 1e-6f)
				{
					v.erase(v.begin() + (long)i);
					removed = true;
					break;
				}
			}
			if (!removed)
			{
				indices.clear();
				return false;
			}
		}
	}
	if (v.size() == 3)
	{
		indices.push_back(v[0]);
		indices.push_back(v[1]);
		indices.push_back(v[2]);
	}
	return !indices.empty();
}

bool WaterGeometry::buildRiverStrip(const RiverArea &river, RiverStrip &out)
{
	out = RiverStrip();
	if (river.lines.size() < 2) return false;
	auto distance = [](float x0, float y0, float x1, float y1) {
		return std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
	};
	const auto &first = river.lines.front();
	const auto &last = river.lines.back();
	const float endLen = distance(first.x0, first.y0, first.x1, first.y1);
	if (endLen == 0.0f) return false;
	// BFME2 W3DWaterDrawRiverWater.cpp, RVA 0x100A0B: walk the boundary
	// perimeter, divide by two, subtract the first end, then divide by its
	// width and the rectangle count. No hardcoded world-space tile length.
	float perimeter = endLen;
	for (size_t i = 1; i < river.lines.size(); ++i)
	{
		const auto &a = river.lines[i - 1];
		const auto &b = river.lines[i];
		perimeter += distance(a.x1, a.y1, b.x1, b.y1);
	}
	perimeter += distance(last.x0, last.y0, last.x1, last.y1);
	for (size_t i = river.lines.size() - 1; i > 0; --i)
	{
		const auto &a = river.lines[i];
		const auto &b = river.lines[i - 1];
		perimeter += distance(a.x0, a.y0, b.x0, b.y0);
	}
	const float vScale = ((perimeter / 2.0f - endLen) / endLen) / float(river.lines.size() - 1);
	for (size_t i = 0; i < river.lines.size(); ++i)
	{
		const auto &l = river.lines[i];
		const float z = (float)river.waterHeight;
		const float v = vScale * float(i);
		out.position.insert(out.position.end(), { l.x0, l.y0, z, l.x1, l.y1, z });
		out.uv0.insert(out.uv0.end(), { 0.0f, v, 0.5f, v });
		out.uv1.insert(out.uv1.end(), { 0.0f, v, 1.0f, v });
		if (i > 0)
		{
			const std::uint32_t a = (std::uint32_t)(2 * (i - 1)), b = a + 1, c = a + 2, d = a + 3;
			out.index.insert(out.index.end(), { a, b, d, a, d, c });
		}
	}
	return true;
}

// Target data: watershader.fxo parameters and ps blob 8 / vs blob 9, decoded
// with tools/render/{fx_params,d3d9_disasm}. No executable provenance claim.
bool WaterGeometry::readStandingMaterial(const MeshShaderMaterialDef &source, StandingMaterial &out)
{
	if (AsciiStringUtil::compareNoCase(source.TypeName, "WaterShader.FX") != 0) return false;
	out = StandingMaterial{};
	for (const auto &p : source.Properties)
	{
		const std::string n = AsciiStringUtil::lowered(p.Name);
		if (n == "materialcolordiffuse") std::copy(p.Float, p.Float + 4, out.diffuse);
		else if (n == "dimmingfactor") out.dimming = p.Float[0];
		else if (n == "bumpscale") out.bumpScale = p.Float[0];
		else if (n == "specularexponent") out.specularExponent = p.Float[0];
		else if (n == "windangle") out.windAngle = p.Float[0];
		else if (n == "windspeed") out.windSpeed = p.Float[0];
		else if (n == "waveangle") out.waveAngle = p.Float[0];
		else if (n == "wavespeed") out.waveSpeed = p.Float[0];
		else if (n == "bumpuvscale") std::copy(p.Float, p.Float + 2, out.bumpUV);
		else if (n == "envuvscale") std::copy(p.Float, p.Float + 2, out.envUV);
		else if (n == "waveuvscale") std::copy(p.Float, p.Float + 2, out.waveUV);
		else if (n == "blendusingterrainalpha") out.blendTerrainAlpha = p.Bool;
		else if (n == "waterbumptexture") out.bumpTexture = p.StringValue;
		else if (n == "waterenvtexture") out.envTexture = p.StringValue;
		else if (n == "wavetexture") out.waveTexture = p.StringValue;
	}
	return true;
}
