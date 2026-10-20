// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameClient/TerrainRoads.h.

#include "GameClient/TerrainRoads.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace
{

std::string lower(std::string s)
{
	for (char &c : s)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return s;
}

std::string trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && std::isspace((unsigned char)s[a])) ++a;
	while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
	return s.substr(a, b - a);
}

struct V2
{
	float x, y;
};

} // namespace

const RoadType *RoadTypeIndex::find(const std::string &name) const
{
	auto it = byName.find(lower(name));
	return it == byName.end() ? nullptr : &it->second;
}

void TerrainRoads::scanText(const std::string &text, RoadTypeIndex &out)
{
	size_t pos = 0;
	bool inBlock = false;
	RoadType cur;
	while (pos <= text.size())
	{
		size_t nl = text.find('\n', pos);
		if (nl == std::string::npos)
		{
			nl = text.size();
		}
		std::string line = text.substr(pos, nl - pos);
		pos = nl + 1;
		size_t semi = line.find(';');
		if (semi != std::string::npos)
		{
			line.erase(semi);
		}
		line = trim(line);
		if (line.empty())
		{
			continue;
		}
		if (!inBlock)
		{
			const bool road = line.size() > 5 && lower(line.substr(0, 4)) == "road" && std::isspace((unsigned char)line[4]);
			const bool bridge = line.size() > 7 && lower(line.substr(0, 6)) == "bridge" && std::isspace((unsigned char)line[6]);
			if (road || bridge)
			{
				cur = RoadType();
				cur.isBridge = bridge;
				cur.name = trim(line.substr(road ? 4 : 6));
				size_t sp = cur.name.find_first_of(" \t");
				if (sp != std::string::npos)
				{
					cur.name.erase(sp);
				}
				inBlock = true;
			}
			continue;
		}
		if (lower(line) == "end")
		{
			out.byName[lower(cur.name)] = cur;
			inBlock = false;
			continue;
		}
		size_t eq = line.find('=');
		if (eq == std::string::npos)
		{
			continue;
		}
		std::string key = lower(trim(line.substr(0, eq)));
		std::string val = trim(line.substr(eq + 1));
		if (key == "texture")
		{
			cur.texture = val;
		}
		else if (key == "roadwidth")
		{
			cur.roadWidth = (float)std::atof(val.c_str());
		}
		else if (key == "roadwidthintexture")
		{
			cur.roadWidthInTexture = (float)std::atof(val.c_str());
		}
	}
}

bool TerrainRoads::load(ArchiveFileSystem &fs, RoadTypeIndex &out, std::string *error)
{
	out = RoadTypeIndex();
	std::vector<std::uint8_t> bytes;
	if (!fs.readFile("data\\ini\\roads.ini", bytes, error))
	{
		return false;
	}
	scanText(std::string(bytes.begin(), bytes.end()), out);
	if (out.byName.empty())
	{
		if (error)
		{
			*error = "data\\ini\\roads.ini holds no Road blocks";
		}
		return false;
	}
	return true;
}

float TerrainRoads::getMaxCellHeight(const WorldHeightMap &map, float x, float y)
{
	int iX = (int)(x / MAP_XY_FACTOR) + map.m_borderSize;
	int iY = (int)(y / MAP_XY_FACTOR) + map.m_borderSize;
	if (iX < 0) iX = 0;
	if (iY < 0) iY = 0;
	if (iX >= map.m_width - 1) iX = map.m_width - 2;
	if (iY >= map.m_height - 1) iY = map.m_height - 2;
	int h0 = map.getHeight(iX, iY), h1 = map.getHeight(iX + 1, iY), h2 = map.getHeight(iX + 1, iY + 1), h3 = map.getHeight(iX, iY + 1);
	return (float)std::max(std::max(h0, h1), std::max(h2, h3)) * MAP_HEIGHT_SCALE;
}

// ZH W3DRoadBuffer::addMapObjects + preloadRoadSegment + loadFloat4PtSection (straight SEGMENT only).
void TerrainRoads::buildStrips(const std::vector<MapObject> &objects, const RoadTypeIndex &types, const WorldHeightMap &map,
	float liftWorldUnits, std::vector<RoadStrip> &out, RoadBuildReport &report)
{
	out.clear();
	report = RoadBuildReport();
	std::vector<std::pair<V2, V2>> seen; // for the identical-segment test (per road type name)
	std::vector<std::string> seenName;
	struct Segment { V2 a, b, normal, dir; float scale, half; int flagsA, flagsB; };
	std::vector<Segment> segments;
	for (size_t i = 0; i < objects.size(); ++i)
	{
		const MapObject &a = objects[i];
		if (!a.getFlag(FLAG_ROAD_POINT1))
		{
			if (a.getFlag(FLAG_ROAD_POINT2))
			{
				++report.orphanPoints; // a POINT2 whose predecessor is not a POINT1 (checked below)
			}
			continue;
		}
		if (i + 1 >= objects.size() || !objects[i + 1].getFlag(FLAG_ROAD_POINT2))
		{
			++report.orphanPoints;
			continue;
		}
		const MapObject &b = objects[i + 1];
		++i; // a pair consumes both (ZH: pMapObj = pMapObj2)
		++report.pairs;

		V2 loc1 = { a.m_location.x, a.m_location.y };
		V2 loc2 = { b.m_location.x, b.m_location.y };
		if (loc1.x == loc2.x && loc1.y == loc2.y)
		{
			loc2.x += 0.25f;
			++report.nudgedZeroLength;
		}
		const RoadType *road = types.find(a.m_objectName);
		if (!road || road->roadWidth <= 0.0f)
		{
			++report.unknownRoadTypes;
			if (std::find(report.unknownNames.begin(), report.unknownNames.end(), a.m_objectName) == report.unknownNames.end())
			{
				report.unknownNames.push_back(a.m_objectName);
			}
			continue;
		}
		bool dup = false;
		for (size_t s = 0; s < seen.size(); ++s)
		{
			if (seenName[s] == lower(a.m_objectName) && ((seen[s].first.x == loc1.x && seen[s].first.y == loc1.y && seen[s].second.x == loc2.x && seen[s].second.y == loc2.y)
				|| (seen[s].first.x == loc2.x && seen[s].first.y == loc2.y && seen[s].second.x == loc1.x && seen[s].second.y == loc1.y)))
			{
				dup = true;
				break;
			}
		}
		if (dup)
		{
			++report.duplicateSegments;
			continue;
		}
		seen.push_back({ loc1, loc2 });
		seenName.push_back(lower(a.m_objectName));

		const float scale = road->roadWidth;
		const float widthInTexture = road->roadWidthInTexture > 0.0f ? road->roadWidthInTexture : 1.0f;
		V2 roadVector = { loc2.x - loc1.x, loc2.y - loc1.y };
		const float len = std::sqrt(roadVector.x * roadVector.x + roadVector.y * roadVector.y);
		V2 roadNormal = { -roadVector.y, roadVector.x };
		const float nl = std::sqrt(roadNormal.x * roadNormal.x + roadNormal.y * roadNormal.y);
		roadNormal.x /= nl;
		roadNormal.y /= nl;
		const float half = scale * widthInTexture / 2.0f;
		const V2 hn = { roadNormal.x * half, roadNormal.y * half };
		const V2 bottom1 = { loc1.x - hn.x, loc1.y - hn.y }, top1 = { loc1.x + hn.x, loc1.y + hn.y };
		const V2 bottom2 = { loc2.x - hn.x, loc2.y - hn.y }, top2 = { loc2.x + hn.x, loc2.y + hn.y };
		const V2 dir = { roadVector.x / len, roadVector.y / len };

		int uCount = (int)(len / MAP_XY_FACTOR) + 1;
		if (uCount < 2) uCount = 2;
		int vCount = (int)(2 * half / MAP_XY_FACTOR) + 1;
		if (vCount < 2) vCount = 2;
		if (vCount > 100) vCount = 100;

		RoadStrip strip;
		strip.roadName = a.m_objectName;
		strip.texture = road->texture;
		const float uOffset = 0.0f, vOffset = 85.0f / 512.0f;
		for (int c = 0; c < uCount; ++c)
		{
			const float f = (float)c / (float)(uCount - 1);
			const V2 pb = { bottom1.x + (bottom2.x - bottom1.x) * f, bottom1.y + (bottom2.y - bottom1.y) * f };
			const V2 pt = { top1.x + (top2.x - top1.x) * f, top1.y + (top2.y - top1.y) * f };
			float maxZ = -1e30f;
			for (int j = 0; j < vCount; ++j)
			{
				const float jf = (float)j / (float)(vCount - 1);
				maxZ = std::max(maxZ, getMaxCellHeight(map, pb.x + (pt.x - pb.x) * jf, pb.y + (pt.y - pb.y) * jf));
			}
			const V2 pts[2] = { pb, pt };
			for (int k = 0; k < 2; ++k)
			{
				const V2 cur = { pts[k].x - loc1.x, pts[k].y - loc1.y };
				const float U = dir.x * cur.x + dir.y * cur.y;
				const float V = roadNormal.x * cur.x + roadNormal.y * cur.y;
				strip.position.push_back(pts[k].x);
				strip.position.push_back(pts[k].y);
				strip.position.push_back(maxZ + liftWorldUnits);
				strip.uv.push_back(uOffset + U / (scale * 4));
				strip.uv.push_back(vOffset - V / (scale * 4));
			}
			if (c > 0)
			{
				const std::uint32_t p0 = (std::uint32_t)(2 * (c - 1)), p1 = p0 + 1, c0 = p0 + 2, c1 = c0 + 1;
				// ZH: (prev[j+1], prev[j], cur[k]) then (prev[j+1], cur[k], cur[k+1]) for the collapsed pair;
				// emitted here with Godot's clockwise front face under (x, z, -y): (p0,p1,c1),(p0,c1,c0)
				strip.index.insert(strip.index.end(), { p0, p1, c1, p0, c1, c0 });
			}
		}
		report.vertices += strip.position.size() / 3;
		report.triangles += strip.index.size() / 3;
		out.push_back(std::move(strip));
		segments.push_back({loc1, loc2, roadNormal, dir, scale, half, a.m_flags, b.m_flags});
	}
	// BFME1 W3DRoadBuffer::miter / insertCurveSegmentAt: degree-two shallow
	// corners (<0.9 * 30 degrees), or explicitly ANGLED corners, intersect
	// the two offset edges. Match endpoints independently of map object order.
	// Rounded corners use the donor atlas patches below; multi-way junctions remain S-036.
	auto equal = [](V2 a, V2 b) { return a.x == b.x && a.y == b.y; };
	struct Ends { V2 bottom[2], top[2]; };
	struct CurvePatch { V2 loc, dir; float scale, half; bool tight; size_t source; };
	std::vector<CurvePatch> curves;
	std::vector<Ends> ends;
	for (const auto &seg : segments)
	{
		Ends e;
		const V2 p[] = {seg.a, seg.b};
		for (int k = 0; k < 2; ++k)
		{
			e.bottom[k] = {p[k].x - seg.normal.x * seg.half, p[k].y - seg.normal.y * seg.half};
			e.top[k] = {p[k].x + seg.normal.x * seg.half, p[k].y + seg.normal.y * seg.half};
		}
		ends.push_back(e);
	}
	for (size_t i = 0; i < segments.size(); ++i)
		for (int ei = 0; ei < 2; ++ei)
		{
			const auto &a = segments[i];
			const V2 node = ei ? a.b : a.a;
			size_t match = segments.size();
			int ej = 0, degree = 1;
			for (size_t j = 0; j < segments.size(); ++j)
			{
				if (j == i || lower(out[i].roadName) != lower(out[j].roadName)) continue;
				for (int e = 0; e < 2; ++e)
					if (equal(node, e ? segments[j].b : segments[j].a)) { ++degree; match = j; ej = e; }
			}
			if (degree != 2 || match <= i) continue;
			const auto &b = segments[match];
			const float orientation = ei == ej ? -1.0f : 1.0f;
			const float dot = std::clamp((a.dir.x * b.dir.x + a.dir.y * b.dir.y) * orientation, -1.0f, 1.0f);
			const int flags = (ei ? a.flagsB : a.flagsA) | (ej ? b.flagsB : b.flagsA);
			if (!(flags & FLAG_ROAD_CORNER_ANGLED) && std::acos(dot) >= 0.9f * 3.14159265358979323846f / 6.0f)
			{
				// BFME1 insertCurveSegmentAt: radius 1.5 widths, or 0.5
				// for TIGHT; if trimming consumes either leg, use miter.
				const float angle = std::acos(dot);
				const bool tight = (flags & FLAG_ROAD_CORNER_TIGHT) != 0;
				const float radius = a.scale * (tight ? 0.5f : 1.5f);
				const float trim = radius * std::tan(angle * 0.5f);
				const V2 incoming = {a.dir.x * (ei ? 1.0f : -1.0f), a.dir.y * (ei ? 1.0f : -1.0f)};
				const V2 outgoing = {b.dir.x * (ej ? -1.0f : 1.0f), b.dir.y * (ej ? -1.0f : 1.0f)};
				const V2 farA = {(ends[i].bottom[1-ei].x+ends[i].top[1-ei].x)*0.5f,(ends[i].bottom[1-ei].y+ends[i].top[1-ei].y)*0.5f};
				const V2 farB = {(ends[match].bottom[1-ej].x+ends[match].top[1-ej].x)*0.5f,(ends[match].bottom[1-ej].y+ends[match].top[1-ej].y)*0.5f};
				const float lengthA = std::hypot(node.x-farA.x,node.y-farA.y);
				const float lengthB = std::hypot(node.x-farB.x,node.y-farB.y);
				if (lengthA-trim > 0.5f && lengthB-trim > 0.5f && dot > -1.0f)
				{
					const V2 start = {node.x-incoming.x*trim,node.y-incoming.y*trim};
					const V2 finish = {node.x+outgoing.x*trim,node.y+outgoing.y*trim};
					const float turn = incoming.x*outgoing.y-incoming.y*outgoing.x > 0 ? 1.0f : -1.0f;
					const V2 center = {start.x-incoming.y*turn*radius,start.y+incoming.x*turn*radius};
					ends[i].bottom[ei] = {start.x-a.normal.x*a.half,start.y-a.normal.y*a.half};
					ends[i].top[ei] = {start.x+a.normal.x*a.half,start.y+a.normal.y*a.half};
					ends[match].bottom[ej] = {finish.x-b.normal.x*b.half,finish.y-b.normal.y*b.half};
					ends[match].top[ej] = {finish.x+b.normal.x*b.half,finish.y+b.normal.y*b.half};
					const V2 origin = turn < 0 ? start : finish;
					const float base = std::atan2(origin.y-center.y,origin.x-center.x);
					const float step = 3.14159265358979323846f/6.0f;
					for (int n = 1; n <= int(std::ceil(angle/step)); ++n)
					{
						const float theta = base-std::min(float(n)*step,angle);
						const V2 radial = {std::cos(theta),std::sin(theta)};
						curves.push_back({{center.x+radius*radial.x,center.y+radius*radial.y},{-radial.y,radial.x},a.scale,a.half,tight,i});
					}
					++report.roundedJoins;
					continue;
				}
			}
			const float det = a.dir.x * b.dir.y - a.dir.y * b.dir.x;
			if (det == 0.0f) continue;
			for (int side = 0; side < 2; ++side)
			{
				V2 &p = side ? ends[i].top[ei] : ends[i].bottom[ei];
				V2 &q = (side == (orientation > 0.0f ? 1 : 0)) ? ends[match].top[ej] : ends[match].bottom[ej];
				const float t = ((q.x - p.x) * b.dir.y - (q.y - p.y) * b.dir.x) / det;
				p = {p.x + t * a.dir.x, p.y + t * a.dir.y};
				q = p;
			}
			++report.miteredJoins;
		}
	// Rebuild columns between the joined corners, retaining donor projected UVs
	// and width-wise terrain maxima. Both sides now meet at identical XY points.
	for (size_t i = 0; i < out.size(); ++i)
	{
		auto &strip = out[i];
		const auto &seg = segments[i];
		const auto &e = ends[i];
		const size_t columns = strip.position.size() / 6;
		const int samples = std::clamp(int(2 * seg.half / MAP_XY_FACTOR) + 1, 2, 100);
		for (size_t c = 0; c < columns; ++c)
		{
			const float f = float(c) / float(columns - 1);
			V2 p[2] = {{e.bottom[0].x + (e.bottom[1].x-e.bottom[0].x)*f, e.bottom[0].y + (e.bottom[1].y-e.bottom[0].y)*f},
				{e.top[0].x + (e.top[1].x-e.top[0].x)*f, e.top[0].y + (e.top[1].y-e.top[0].y)*f}};
			float z = -1e30f;
			for (int j = 0; j < samples; ++j)
			{
				const float v = float(j) / float(samples - 1);
				z = std::max(z, getMaxCellHeight(map, p[0].x + (p[1].x-p[0].x)*v, p[0].y + (p[1].y-p[0].y)*v));
			}
			for (int k = 0; k < 2; ++k)
			{
				const size_t v = c*2+k;
				strip.position[v*3] = p[k].x; strip.position[v*3+1] = p[k].y; strip.position[v*3+2] = z + liftWorldUnits;
				const V2 origin = {(e.bottom[0].x+e.top[0].x)*0.5f,(e.bottom[0].y+e.top[0].y)*0.5f};
				const V2 delta = {p[k].x-origin.x, p[k].y-origin.y};
				strip.uv[v*2] = (delta.x*seg.dir.x + delta.y*seg.dir.y)/(seg.scale*4);
				strip.uv[v*2+1] = 85.0f/512.0f - (delta.x*seg.normal.x+delta.y*seg.normal.y)/(seg.scale*4);
			}
		}
	}
	// BFME1 loadCurve: sample the dedicated 30-degree curve artwork in
	// the road atlas (255/512 normal, 425/512 tight), not the straight row.
	for (const auto &curve : curves)
	{
		const V2 d = {curve.dir.x*curve.scale,curve.dir.y*curve.scale};
		const V2 n = {-curve.dir.y*curve.half,curve.dir.x*curve.half};
		const float reach = curve.tight ? 0.5f : 1.0f;
		const float flare = curve.tight ? 0.2f : 0.4f;
		const float extra = curve.tight ? 0.1f : 0.2f;
		V2 bl = {curve.loc.x-n.x*(1+extra)-d.x*0.02f,curve.loc.y-n.y*(1+extra)-d.y*0.02f};
		V2 tl = {curve.loc.x+n.x-d.x*0.02f,curve.loc.y+n.y-d.y*0.02f};
		V2 br = {curve.loc.x-n.x+d.x*(reach+0.1f)+n.x*flare,curve.loc.y-n.y+d.y*(reach+0.1f)+n.y*flare};
		V2 tr = {curve.loc.x+n.x+d.x*(reach-0.4f)+n.x*flare,curve.loc.y+n.y+d.y*(reach-0.4f)+n.y*flare};
		RoadStrip strip;
		strip.roadName = out[curve.source].roadName;
		strip.texture = out[curve.source].texture;
		const int columns = std::max(2,int(curve.scale/MAP_XY_FACTOR)+1);
		const int samples = std::clamp(int(curve.half*2/MAP_XY_FACTOR)+1,2,100);
		for (int c=0;c<columns;++c)
		{
			const float f=float(c)/float(columns-1);
			V2 p[2]={{bl.x+(br.x-bl.x)*f,bl.y+(br.y-bl.y)*f},{tl.x+(tr.x-tl.x)*f,tl.y+(tr.y-tl.y)*f}};
			float z=-1e30f;
			for(int j=0;j<samples;++j) {const float t=float(j)/float(samples-1); z=std::max(z,getMaxCellHeight(map,p[0].x+(p[1].x-p[0].x)*t,p[0].y+(p[1].y-p[0].y)*t));}
			for(int k=0;k<2;++k)
			{
				strip.position.insert(strip.position.end(),{p[k].x,p[k].y,z+liftWorldUnits});
				const V2 delta={p[k].x-curve.loc.x,p[k].y-curve.loc.y};
				strip.uv.push_back(4.0f/512.0f+(delta.x*curve.dir.x+delta.y*curve.dir.y)/(curve.scale*4));
				strip.uv.push_back((curve.tight?425.0f:255.0f)/512.0f-(-delta.x*curve.dir.y+delta.y*curve.dir.x)/(curve.scale*4));
			}
			if(c) {const std::uint32_t a=2*(c-1);strip.index.insert(strip.index.end(),{a,a+1,a+3,a,a+3,a+2});}
		}
		report.vertices+=strip.position.size()/3;
		report.triangles+=strip.index.size()/3;
		out.push_back(std::move(strip));
	}
	report.strips = out.size();
}
