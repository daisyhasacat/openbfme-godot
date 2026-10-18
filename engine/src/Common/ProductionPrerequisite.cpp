// OpenBFME. GPL-3.0. Ported against the privately supplied modified image (S-001 caveat).
// Donor: Code/GameEngine/Source/Common/RTS/ProductionPrerequisiteIsSatisfied_ProductionPrerequisite_isSatisfied.cpp.

#include "Common/ProductionPrerequisite.h"

#include "Common/BuildAssistant.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/Player.h"
#include "Common/Upgrade.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"

#include <algorithm>
#include <stdexcept>

namespace
{
struct ParseState
{
	std::vector<ProductionPrerequisite> *out = nullptr;
};

void parseObjectRow(INI *ini, void *instance, void *, const void *)
{
	auto &state = *static_cast<ParseState *>(instance);
	state.out->push_back(ProductionPrerequisite::parse(ini, ProductionPrerequisite::RowKind::Object));
}

void parseScienceRow(INI *ini, void *instance, void *, const void *)
{
	auto &state = *static_cast<ParseState *>(instance);
	state.out->push_back(ProductionPrerequisite::parse(ini, ProductionPrerequisite::RowKind::Science));
}

void parseUpgradeRow(INI *ini, void *instance, void *, const void *)
{
	auto &state = *static_cast<ParseState *>(instance);
	state.out->push_back(ProductionPrerequisite::parse(ini, ProductionPrerequisite::RowKind::PlayerUpgrade));
}

const FieldParse kPrerequisiteFields[] = {
	{ "Object", parseObjectRow, nullptr, 0 },
	{ "Science", parseScienceRow, nullptr, 0 },
	{ "PlayerUpgrade", parseUpgradeRow, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 },
};

std::u16string displayName(const ThingTemplate &unit, const GameTextSource *text)
{
	const FieldValue *value = unit.getFinalOverride()->findField("DisplayName");
	std::string label;
	if (const auto *string = value ? std::get_if<std::string>(value) : nullptr)
	{
		label = *string;
	}
	else if (const RawTokens *raw = value ? std::get_if<RawTokens>(value) : nullptr; raw && !raw->tokens.empty())
	{
		label = raw->tokens.front();
	}
	// RW 0x8F9BBF reads the template's localized DisplayName (+ 0x30) as it is: a template without one gives an empty name (no error in retail)
	return label.empty() ? std::u16string() : fetchOrMissing(text, label);
}
} // namespace

ProductionPrerequisite ProductionPrerequisite::parse(INI *ini, RowKind kind)
{
	ProductionPrerequisite out;
	if (kind == RowKind::Object)
	{
		const char *token = ini->getNextToken();
		bool orWithPrevious = false;
		for (;;)
		{
			out.m_units.push_back(Unit{ token, orWithPrevious });
			token = ini->getNextTokenOrNull();
			if (!token)
			{
				break;
			}
			orWithPrevious = true;
		}
		return out;
	}

	const char *token = ini->getNextToken();
	if (kind == RowKind::Science)
	{
		if (!TheScienceStore)
		{
			throw INIException(3, "TheScienceStore==NULL");
		}
		out.m_sciences.push_back(TheScienceStore->friend_lookupScience(token));
		return out;
	}

	if (!TheUpgradeCenter)
	{
		throw INIException(3, "TheUpgradeCenter==NULL");
	}
	const UpgradeTemplate *upgrade = TheUpgradeCenter->findUpgrade(token);
	if (!upgrade)
	{
		throw INIException(3, "Upgrade %s not known!", token);
	}
	out.m_upgrades.push_back(upgrade);
	return out;
}

void ProductionPrerequisite::parseBlock(INI *ini, std::vector<ProductionPrerequisite> &out)
{
	ParseState state{ &out };
	ini->initFromINI(&state, kPrerequisiteFields);
}

const ThingTemplate *ProductionPrerequisite::resolveUnit(const std::string &name, const GameLogic &logic) const
{
	const ThingTemplate *unit = logic.things().findTemplate(name);
	if (!unit)
	{
		throw std::runtime_error("ProductionPrerequisite: unknown object '" + name + "'");
	}
	return unit->getFinalOverride();
}

int ProductionPrerequisite::countUnits(const Player *player, const GameLogic &logic, int counts[32]) const
{
	const int count = std::min<int>((int)m_units.size(), 32);
	std::fill(counts, counts + count, 0);
	const ThingTemplate *templates[32] = {};
	for (int i = 0; i < count; ++i)
	{
		templates[i] = resolveUnit(m_units[(size_t)i].name, logic);
	}
	if (!player)
	{
		return count;
	}
	for (const Object *object = logic.getFirstObject(); object; object = object->getNextObject())
	{
		if (object->getControllingPlayer() != player || object->isUnderConstruction())
		{
			continue;
		}
		for (int i = 0; i < count; ++i)
		{
			if (BuildAssistant::isEquivalentTo(object->getTemplate(), templates[i]))
			{
				++counts[i];
				break;
			}
		}
	}
	return count;
}

bool ProductionPrerequisite::isSatisfied(const Player *player, const GameLogic &logic) const
{
	if (!player)
	{
		return false;
	}
	for (ScienceType science : m_sciences)
	{
		if (!player->hasScience(science))
		{
			return false;
		}
	}
	for (const UpgradeTemplate *upgrade : m_upgrades)
	{
		if (!upgrade || upgrade->getUpgradeType() != UPGRADE_TYPE_PLAYER || !player->hasUpgradeComplete(upgrade))
		{
			return false;
		}
	}
	int counts[32] = {};
	const int count = countUnits(player, logic, counts);
	for (int i = 1; i < count; ++i)
	{
		if (m_units[(size_t)i].orWithPrevious)
		{
			counts[i] += counts[i - 1];
			counts[i - 1] = -1;
		}
	}
	for (int i = 0; i < count; ++i)
	{
		if (counts[i] != -1 && counts[i] == 0)
		{
			return false;
		}
	}
	return true;
}

std::u16string ProductionPrerequisite::getRequiresList(const Player *player, const GameLogic &logic, const GameTextSource *text) const
{
	if (!player)
	{
		return {};
	}
	std::u16string requiresList;
	int counts[32] = {};
	const int count = countUnits(player, logic, counts);
	bool orRequirements[32] = {};
	for (int i = 1; i < count; ++i)
	{
		if (m_units[(size_t)i].orWithPrevious)
		{
			orRequirements[i] = true;
			counts[i] += counts[i - 1];
			counts[i - 1] = -1;
		}
	}

	std::u16string unitName;
	bool firstRequirement = true;
	for (int i = 0; i < count; ++i)
	{
		if (counts[i] == 0)
		{
			if (orRequirements[i])
			{
				unitName = displayName(*resolveUnit(m_units[(size_t)i - 1].name, logic), text);
				unitName += u" ";
				unitName += fetchOrMissing(text, "CONTROLBAR:OrRequirement");
				unitName += u" ";
				requiresList += unitName;
			}
			unitName = displayName(*resolveUnit(m_units[(size_t)i].name, logic), text);
			if (firstRequirement)
			{
				firstRequirement = false;
			}
			else
			{
				unitName += u"\n";
			}
			requiresList += unitName;
		}
	}

	bool hasSciences = true;
	for (ScienceType science : m_sciences)
	{
		if (!player->hasScience(science))
		{
			hasSciences = false;
		}
	}
	if (!hasSciences)
	{
		if (firstRequirement)
		{
			firstRequirement = false;
		}
		else
		{
			unitName += u"\n";
		}
		requiresList += fetchOrMissing(text, "CONTROLBAR:GeneralsPromotion");
	}

	bool hasUpgrades = true;
	for (const UpgradeTemplate *upgrade : m_upgrades)
	{
		if (!upgrade || !player->hasUpgradeComplete(upgrade))
		{
			hasUpgrades = false;
		}
	}
	if (!hasUpgrades)
	{
		if (firstRequirement)
		{
			firstRequirement = false;
		}
		else
		{
			unitName += u"\n";
		}
		requiresList += fetchOrMissing(text, "CONTROLBAR:UpgradeRequired");
	}
	return requiresList;
}
