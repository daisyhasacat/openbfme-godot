// OpenBFME. GPL-3.0.
// Port of the Open-BFME-2 ProductionPrerequisite donor and ThingTemplateParsePrerequisite* parser siblings.
// RotWK 2.01 S-207: parser RW 0x74040D / 0x740483 / 0x7404E0 / 0x74057B; count 0x8F99B8; satisfaction 0x8F9ADD; formatter 0x8F9BBF.

#pragma once

#include "Common/Science.h"

#include <string>
#include <vector>

class GameLogic;
class GameTextSource;
class INI;
class Player;
class ThingTemplate;
class UpgradeTemplate;

class ProductionPrerequisite
{
public:
	enum class RowKind
	{
		Object,
		Science,
		PlayerUpgrade
	};

	static ProductionPrerequisite parse(INI *ini, RowKind kind);
	static void parseBlock(INI *ini, std::vector<ProductionPrerequisite> &out);

	bool isSatisfied(const Player *player, const GameLogic &logic) const;
	std::u16string getRequiresList(const Player *player, const GameLogic &logic, const GameTextSource *text) const;

private:
	struct Unit
	{
		std::string name;
		bool orWithPrevious = false;
	};

	int countUnits(const Player *player, const GameLogic &logic, int counts[32]) const;
	const ThingTemplate *resolveUnit(const std::string &name, const GameLogic &logic) const;

	std::vector<Unit> m_units;
	std::vector<ScienceType> m_sciences;
	std::vector<const UpgradeTemplate *> m_upgrades;
};
