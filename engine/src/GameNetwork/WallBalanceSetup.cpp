// OpenBFME. GPL-3.0.
// See GameNetwork/WallBalanceSetup.h.

#include "GameNetwork/WallBalanceSetup.h"

#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

namespace
{
WallBalance s_local;

std::string bitsOf(float f)
{
	std::uint32_t u;
	std::memcpy(&u, &f, sizeof(u));
	char b[16];
	std::snprintf(b, sizeof(b), "%08X", u);
	return b;
}

bool floatOf(const std::string &s, float &out)
{
	if (s.size() != 8)
	{
		return false;
	}
	std::uint32_t u = 0;
	for (char c : s)
	{
		u <<= 4;
		if (c >= '0' && c <= '9')
			u |= (std::uint32_t)(c - '0');
		else if (c >= 'A' && c <= 'F')
			u |= (std::uint32_t)(c - 'A' + 10);
		else
			return false;
	}
	std::memcpy(&out, &u, sizeof(out));
	return true;
}

bool intOf(const std::string &s, int lo, int hi, int &out)
{
	if (s.empty() || s.size() > 9)
	{
		return false;
	}
	int v = 0;
	for (char c : s)
	{
		if (c < '0' || c > '9')
		{
			return false;
		}
		v = v * 10 + (c - '0');
	}
	if (v < lo || v > hi)
	{
		return false;
	}
	out = v;
	return true;
}

// a map's entries "<a>:<b>" joined by ","; the names are template / faction names (no ';', ',' or ':' in them: refused when written)
bool mapText(const std::map<std::string, std::string> &m, std::string &out)
{
	for (const auto &kv : m)
	{
		for (const std::string *s : { &kv.first, &kv.second })
		{
			if (s->empty() || s->find_first_of(";,:=") != std::string::npos)
			{
				return false;
			}
		}
		out += (out.empty() ? "" : ",") + kv.first + ":" + kv.second;
	}
	return true;
}

bool mapOf(const std::string &s, std::map<std::string, std::string> &out)
{
	out.clear();
	if (s.empty())
	{
		return true;
	}
	std::stringstream in(s);
	std::string item;
	while (std::getline(in, item, ','))
	{
		const size_t colon = item.find(':');
		if (colon == std::string::npos || colon == 0 || colon + 1 >= item.size() || item.find(':', colon + 1) != std::string::npos)
		{
			return false;
		}
		out[item.substr(0, colon)] = item.substr(colon + 1);
	}
	return true;
}

// the fields in their fixed order
struct Field
{
	const char *key;
	enum Kind
	{
		BOOL,
		INT2,
		FLOAT,
		MAP
	} kind;
	bool WallBalance::*b;
	int WallBalance::*i;
	float *(*f)(WallBalance &);
	std::map<std::string, std::string> WallBalance::*m;
};

#define WB_BOOL(k, name) Field{ k, Field::BOOL, &WallBalance::name, nullptr, nullptr, nullptr }
#define WB_INT(k, name) Field{ k, Field::INT2, nullptr, &WallBalance::name, nullptr, nullptr }
#define WB_FLOAT(k, expr) Field{ k, Field::FLOAT, nullptr, nullptr, [](WallBalance &w) -> float * { return &(expr); }, nullptr }
#define WB_MAP(k, name) Field{ k, Field::MAP, nullptr, nullptr, nullptr, &WallBalance::name }

const std::vector<Field> &fields()
{
	static const std::vector<Field> f = {
		WB_BOOL("intermediate_hubs", intermediateHubs),
		WB_BOOL("share_arrows", shareArrows),
		WB_BOOL("trebuchet_wall_targets", trebuchetWallTargets),
		WB_BOOL("starting_walls", startingWalls),
		WB_BOOL("require_enclosed", requireEnclosed),
		WB_BOOL("gate_gap", gateGap),
		WB_BOOL("disable_upgrade_separation", disableUpgradeSeparation),
		WB_FLOAT("enclosed_income", w.enclosedIncome),
		WB_FLOAT("starting_radius", w.startingRadius),
		WB_FLOAT("starting_spacing", w.startingSpacing),
		WB_INT("wall_damage", wallDamage),
		WB_INT("fortress_damage", fortressDamage),
		WB_FLOAT("wall_health", w.health[0]),
		WB_FLOAT("hub_health", w.health[1]),
		WB_FLOAT("gate_health", w.health[2]),
		WB_FLOAT("wall_cost", w.cost[0]),
		WB_FLOAT("hub_cost", w.cost[1]),
		WB_FLOAT("outer_hub_cost", w.cost[2]),
		WB_FLOAT("expansion_cost", w.cost[3]),
		WB_FLOAT("fortress_range", w.fortressRange),
		WB_FLOAT("hub_range", w.hubRange),
		WB_MAP("starting_hubs", startingHubs),
		WB_MAP("starting_gates", startingGates),
	};
	return f;
}

const char *kHeader = "wallbalance 1";
} // namespace

std::string WallBalanceSetup::toText(const WallBalance &in)
{
	if (!in.enabled)
	{
		return "";
	}
	WallBalance w = in;
	std::string out = kHeader;
	for (const Field &f : fields())
	{
		std::string v;
		switch (f.kind)
		{
		case Field::BOOL:
			v = w.*f.b ? "1" : "0";
			break;
		case Field::INT2:
			v = std::to_string(w.*f.i);
			break;
		case Field::FLOAT:
			v = bitsOf(*f.f(w));
			break;
		case Field::MAP:
			if (!mapText(w.*f.m, v))
			{
				v.clear(); // a name the line cannot hold: the map is not shared (only offline starting-wall jobs use it)
			}
			break;
		}
		out += std::string(";") + f.key + "=" + v;
	}
	return out;
}

bool WallBalanceSetup::fromText(const std::string &text, WallBalance &out, std::string *error)
{
	auto fail = [&](const std::string &why) {
		if (error)
		{
			*error = "wall balance setup: " + why;
		}
		return false;
	};
	WallBalance w;
	if (text.empty())
	{
		out = w;
		return true;
	}
	std::vector<std::string> parts;
	{
		std::stringstream in(text);
		std::string p;
		while (std::getline(in, p, ';'))
		{
			parts.push_back(p);
		}
	}
	if (parts.empty() || parts[0] != kHeader)
	{
		return fail("unknown version \"" + (parts.empty() ? std::string() : parts[0]) + "\"");
	}
	const std::vector<Field> &fs = fields();
	if (parts.size() != fs.size() + 1)
	{
		return fail(std::to_string(parts.size() - 1) + " fields, this build reads " + std::to_string(fs.size()));
	}
	w.enabled = true;
	for (size_t i = 0; i < fs.size(); ++i)
	{
		const Field &f = fs[i];
		const std::string &p = parts[i + 1];
		const std::string key = std::string(f.key) + "=";
		if (p.compare(0, key.size(), key) != 0)
		{
			return fail("field " + std::to_string(i + 1) + " is not " + f.key);
		}
		const std::string v = p.substr(key.size());
		bool ok = false;
		int n = 0;
		switch (f.kind)
		{
		case Field::BOOL:
			ok = v == "0" || v == "1";
			if (ok)
				w.*f.b = v == "1";
			break;
		case Field::INT2:
			ok = intOf(v, 0, 2, n);
			if (ok)
				w.*f.i = n;
			break;
		case Field::FLOAT:
			ok = floatOf(v, *f.f(w));
			break;
		case Field::MAP:
			ok = mapOf(v, w.*f.m);
			break;
		}
		if (!ok)
		{
			return fail(std::string(f.key) + " has the value \"" + v + "\"");
		}
	}
	out = w;
	return true;
}

void WallBalanceSetup::setLocal(const WallBalance &w)
{
	s_local = w;
	s_local.enclosureCache.reset();
}

const WallBalance &WallBalanceSetup::local() { return s_local; }

std::string WallBalanceSetup::describe(const WallBalance &w)
{
	if (!w.enabled)
	{
		return "off";
	}
	std::string s = "on";
	if (w.startingWalls)
		s += ", starting walls";
	if (w.requireEnclosed)
		s += ", buildings inside walls";
	if (w.shareArrows)
		s += ", towers share arrow upgrades";
	return s;
}
