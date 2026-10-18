// OpenBFME unit tests. GPL-3.0.
// Lane PLATFORMICON-1: helpers to measure where a movie's clips are in stage pixels (the 1024 x 768 menu stage), so a test can assert that an icon overlaps
// no other part of a screen.
#pragma once

#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptButtonInst.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"

#include <algorithm>
#include <string>
#include <vector>

namespace platformlayout
{
struct Rect
{
	float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	bool overlaps(const Rect &o) const { return x0 < o.x1 && o.x0 < x1 && y0 < o.y1 && o.y0 < y1; }
};

// the clip at the dot path under a level's root ("OuterFrame.Pages.StatusPage.Table"); null when a name is missing
inline AptCharacterInst *clipAtPath(Apt &apt, int level, const std::string &path)
{
	AptCharacterInst *c = apt.level(level);
	std::size_t at = 0;
	while (c && at <= path.size())
	{
		const std::size_t dot = path.find('.', at);
		const std::string name = path.substr(at, dot == std::string::npos ? std::string::npos : dot - at);
		AptSpriteInst *sp = c->asSprite();
		c = sp ? sp->childByName(name) : nullptr;
		if (dot == std::string::npos)
		{
			break;
		}
		at = dot + 1;
	}
	return c;
}

// every clip below `c` named `name`
inline void collectNamed(AptCharacterInst *c, const std::string &name, std::vector<AptCharacterInst *> &out)
{
	AptSpriteInst *sp = c ? c->asSprite() : nullptr;
	if (!sp)
	{
		return;
	}
	for (AptCharacterInst *child : sp->children())
	{
		if (child && child->instName() == name)
		{
			out.push_back(child);
		}
		collectNamed(child, name, out);
	}
}

// every visible button below `c` (the Kick buttons, the Ready boxes' hit areas ...)
inline void collectButtons(AptCharacterInst *c, std::vector<AptCharacterInst *> &out)
{
	AptSpriteInst *sp = c ? c->asSprite() : nullptr;
	if (!sp)
	{
		return;
	}
	for (AptCharacterInst *child : sp->children())
	{
		if (child && child->asButton() && child->globallyVisible())
		{
			out.push_back(child);
		}
		collectButtons(child, out);
	}
}

// the content of a clip in stage pixels; false when it has none
inline bool stageRect(AptCharacterInst *c, Rect &out)
{
	float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	if (!c || !c->contentBounds(x0, y0, x1, y1))
	{
		return false;
	}
	const AptMatrix g = c->globalMatrix();
	float ax, ay, bx, by;
	g.apply(x0, y0, ax, ay);
	g.apply(x1, y1, bx, by);
	out = Rect{ std::min(ax, bx), std::min(ay, by), std::max(ax, bx), std::max(ay, by) };
	return true;
}
} // namespace platformlayout
