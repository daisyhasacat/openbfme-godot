// OpenBFME. GPL-3.0.
//
// Lane PLATFORMICON-1 (OpenBFME, not retail): a small mapped image drawn just left of a movie's text field, for the rows a movie makes itself (the
// players screen's Status page, PlayerTribute.apt: each row's name is a text field bound to the record "APT:_levelN.<row>_field0"). A screen registers the
// record it fills (WindowManager::setTextBadge); the device appends, to each frame's gadget draw list, one Image command per text command of the render
// list that shows such a record. The icon follows the field: hidden when the field's clip is (the render list leaves it out), moved when it moves.

#pragma once

#include "GameClient/GUI/GadgetDrawList.h"
#include "Libraries/Source/Apt/AptRenderList.h"

#include <cmath>
#include <map>
#include <string>

struct TextBadge
{
	std::string image; ///< the mapped image's name
	int size = 16;     ///< its side in stage pixels
	std::string help;  ///< "Playing on Linux" (no help box shows it in the menus yet)
	int reserve = 0;   ///< stage pixels at the field's left the string must stay clear of (AptCanvasInputs::textBadges narrows the field's box by it)
	int dx = 0;        ///< stage pixels added to the icon's x: a movie whose name field starts at (or past) its table's border moves the icon out to the gutter
};
using TextBadgeMap = std::map<std::string, TextBadge>; // by record label ("APT:_level3.OuterFrame.Pages.StatusPage.Table.0_field0")

// the record label of a text field's raw text ("$_level3.x_field0&dropShadow" -> "APT:_level3.x_field0"); "" when the text is not a record
inline std::string textBadgeLabel(const std::string &raw)
{
	if (raw.size() < 2 || raw[0] != '$')
	{
		return std::string();
	}
	std::string label = raw.substr(1);
	const std::size_t shadow = label.find('&');
	if (shadow != std::string::npos)
	{
		label.resize(shadow);
	}
	return label.find(':') == std::string::npos ? "APT:" + label : label;
}

inline void appendTextBadges(const AptRenderList &list, const TextBadgeMap &badges, GadgetDrawList &out)
{
	if (badges.empty())
	{
		return;
	}
	for (const AptRenderCommand &c : list.commands)
	{
		if (c.kind != AptRenderCommand::Kind::Text || c.color.mul[3] <= 0.0f)
		{
			continue; // not a field, or a clip faded out
		}
		const auto it = badges.find(textBadgeLabel(c.text));
		if (it == badges.end())
		{
			continue;
		}
		// the field's box in stage pixels: its top-left corner and its height (the matrix may scale)
		float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		c.matrix.apply(c.bounds[0], c.bounds[1], x0, y0);
		c.matrix.apply(c.bounds[2], c.bounds[3], x1, y1);
		const float top = y0 < y1 ? y0 : y1, bottom = y0 < y1 ? y1 : y0, left = x0 < x1 ? x0 : x1;
		GadgetDrawCommand g;
		g.kind = GadgetDrawCommand::Kind::Image;
		g.window = c.path;
		g.image = it->second.image;
		g.x1 = (int)std::lround(left) - 2 + it->second.dx;
		g.x0 = g.x1 - it->second.size;
		g.y0 = (int)std::lround((top + bottom) * 0.5f) - it->second.size / 2;
		g.y1 = g.y0 + it->second.size;
		g.color = 0xFFFFFFFFu;
		out.commands.push_back(std::move(g));
	}
}
