// OpenBFME. GPL-3.0.
// See GameClient/LivingWorldCampaign.h (lane WOTR-1).

#include "GameClient/LivingWorldCampaign.h"

#include "Common/AsciiString.h"
#include "Common/INI.h"
#include "Common/INIException.h"

#include <algorithm>
#include <cstddef>

thread_local LivingWorldCampaignManager *TheLivingWorldCampaignManager = nullptr;

namespace
{
using namespace LivingWorldScenario;

// Rva004FCB23Parse (INIThinFieldParsers2.cpp): the int list (INI::Rva004E8EFF_ParseDisabledSlots), every entry minus one
void parseTeams(INI *ini, void *, void *store, const void *)
{
	std::vector<int> &teams = *static_cast<std::vector<int> *>(store);
	teams.clear();
	for (const char *token = ini->getNextTokenOrNull(); token; token = ini->getNextTokenOrNull())
	{
		teams.push_back(ini->scanInt(token) - 1);
	}
}

// parseAsciiStringSetForPositionInfo: a set of names (sorted, no duplicates)
void parseNameSet(INI *ini, void *instance, void *store, const void *userData)
{
	INI::parseAsciiStringVector(ini, instance, store, userData);
	std::vector<std::string> &names = *static_cast<std::vector<std::string> *>(store);
	std::sort(names.begin(), names.end());
	names.erase(std::unique(names.begin(), names.end()), names.end());
}

// the fields RotWK's data does not use: a load that meets one fails with its name (stop S-5300)
void parseNotPorted(INI *ini, void *, void *, const void *userData)
{
	(void)ini;
	throw INIException(8, "LivingWorldCampaign field '%s' is not ported (stop S-5300)", static_cast<const char *>(userData));
}

// SpawnBuildings / SpawnArmies (Rva0059E436Parse / Rva0059E511Parse): a list appended to the ownership set
const FieldParse kSpawnBuildingsParse[] = {
	{ "Buildings", INI::parseAsciiStringVector, nullptr, (int)offsetof(SpawnList, names) },
	{ "Region", INI::parseAsciiString, nullptr, (int)offsetof(SpawnList, region) },
	{ nullptr, nullptr, nullptr, 0 }
};
const FieldParse kSpawnArmiesParse[] = {
	{ "Armies", INI::parseAsciiStringVector, nullptr, (int)offsetof(SpawnList, names) },
	{ "Region", INI::parseAsciiString, nullptr, (int)offsetof(SpawnList, region) },
	{ nullptr, nullptr, nullptr, 0 }
};
template <const FieldParse *Table>
void parseSpawnList(INI *ini, void *, void *store, const void *)
{
	std::vector<SpawnList> &lists = *static_cast<std::vector<SpawnList> *>(store);
	lists.emplace_back();
	ini->initFromINI(&lists.back(), Table);
}

// OwnershipSet (Rva0059E6D3Parse)
const FieldParse kOwnershipSetParse[] = {
	{ "Regions", INI::parseAsciiStringVector, nullptr, (int)offsetof(OwnershipSet, regions) },
	{ "SpawnBuildings", parseSpawnList<kSpawnBuildingsParse>, nullptr, (int)offsetof(OwnershipSet, spawnBuildings) },
	{ "SpawnArmies", parseSpawnList<kSpawnArmiesParse>, nullptr, (int)offsetof(OwnershipSet, spawnArmies) },
	{ "StartRegion", INI::parseAsciiString, nullptr, (int)offsetof(OwnershipSet, startRegion) },
	{ nullptr, nullptr, nullptr, 0 }
};
const FieldParse kPlayerDefeatParse[] = {
	{ "Teams", parseTeams, nullptr, (int)offsetof(PlayerDefeatCondition, teams) },
	{ "LoseIfCapitalLost", INI::parseBool, nullptr, (int)offsetof(PlayerDefeatCondition, loseIfCapitalLost) },
	{ "NumControlledRegionsLessOrEqualTo", INI::parseInt, nullptr, (int)offsetof(PlayerDefeatCondition, numControlledRegionsLessOrEqualTo) },
	{ nullptr, nullptr, nullptr, 0 }
};
const FieldParse kTeamDefeatParse[] = {
	{ "Teams", parseTeams, nullptr, (int)offsetof(TeamDefeatCondition, teams) },
	{ "NumControlledRegionsLessOrEqualTo", INI::parseInt, nullptr, (int)offsetof(TeamDefeatCondition, numControlledRegionsLessOrEqualTo) },
	{ nullptr, nullptr, nullptr, 0 }
};
const FieldParse kTeamVictoryParse[] = {
	{ "Teams", parseTeams, nullptr, (int)offsetof(TeamVictoryCondition, teams) },
	{ "NumControlledRegionsGreaterOrEqualTo", INI::parseInt, nullptr, (int)offsetof(TeamVictoryCondition, numControlledRegionsGreaterOrEqualTo) },
	{ "ControlledRegions", INI::parseAsciiStringVector, nullptr, (int)offsetof(TeamVictoryCondition, controlledRegions) },
	{ "ControlledRegionsHeldForTurns", INI::parseInt, nullptr, (int)offsetof(TeamVictoryCondition, controlledRegionsHeldForTurns) },
	{ nullptr, nullptr, nullptr, 0 }
};
const FieldParse kStartingRestrictionParse[] = {
	{ "Regions", INI::parseAsciiStringVector, nullptr, (int)offsetof(StartingRestriction, regions) },
	{ "Factions", parseNameSet, nullptr, (int)offsetof(StartingRestriction, factions) },
	{ "Teams", parseTeams, nullptr, (int)offsetof(StartingRestriction, teams) },
	{ nullptr, nullptr, nullptr, 0 }
};
// a sub-block appended to its list, then its fields
template <class T, const FieldParse *Table>
void parseAppended(INI *ini, void *, void *store, const void *)
{
	std::vector<T> &list = *static_cast<std::vector<T> *>(store);
	list.emplace_back();
	ini->initFromINI(&list.back(), Table);
}

// the Scenario table, retail 0x00863870 (18 entries)
const FieldParse kScenarioParse[] = {
	{ "DisplayName", INI::parseAsciiString, nullptr, (int)offsetof(Scenario, displayName) },
	{ "DisplayDescription", INI::parseAsciiString, nullptr, (int)offsetof(Scenario, displayDescription) },
	{ "DisplayFiction", INI::parseAsciiString, nullptr, (int)offsetof(Scenario, displayFiction) },
	{ "DisplayObjectives", INI::parseAsciiString, nullptr, (int)offsetof(Scenario, displayObjectives) },
	{ "DisplayGameType", INI::parseAsciiString, nullptr, (int)offsetof(Scenario, displayGameType) },
	{ "DisplayVictoriousText", INI::parseAsciiString, nullptr, (int)offsetof(Scenario, displayVictoriousText) },
	{ "DisplayDefeatedText", INI::parseAsciiString, nullptr, (int)offsetof(Scenario, displayDefeatedText) },
	{ "RegionCampaign", INI::parseAsciiString, nullptr, (int)offsetof(Scenario, regionCampaign) },
	{ "DisableRegions", INI::parseAsciiStringVector, nullptr, (int)offsetof(Scenario, disableRegions) },
	{ "DisallowStartInRegions", INI::parseAsciiStringVector, nullptr, (int)offsetof(Scenario, disallowStartInRegions) },
	{ "DefaultStartSpots", INI::parseAsciiStringVector, nullptr, (int)offsetof(Scenario, defaultStartSpots) },
	{ "PlayerDefeatCondition", parseAppended<PlayerDefeatCondition, kPlayerDefeatParse>, nullptr, (int)offsetof(Scenario, playerDefeatConditions) },
	{ "TeamDefeatCondition", parseAppended<TeamDefeatCondition, kTeamDefeatParse>, nullptr, (int)offsetof(Scenario, teamDefeatConditions) },
	{ "TeamVictoryCondition", parseAppended<TeamVictoryCondition, kTeamVictoryParse>, nullptr, (int)offsetof(Scenario, teamVictoryConditions) },
	{ "OwnershipSet", parseAppended<OwnershipSet, kOwnershipSetParse>, nullptr, (int)offsetof(Scenario, ownershipSets) },
	{ "StartingRestriction", parseAppended<StartingRestriction, kStartingRestrictionParse>, nullptr, (int)offsetof(Scenario, startingRestrictions) },
	{ "MaxPlayers", INI::parseInt, nullptr, (int)offsetof(Scenario, maxPlayers) },
	{ "MinPlayers", INI::parseInt, nullptr, (int)offsetof(Scenario, minPlayers) },
	{ "DisabledFactions", INI::parseAsciiStringVector, nullptr, (int)offsetof(Scenario, disabledFactions) }, // RotWK
	{ "HistoricalScenario", INI::parseBool, nullptr, (int)offsetof(Scenario, historicalScenario) },
	{ "UseMpRulesVictoryCondition", INI::parseBool, nullptr, (int)offsetof(Scenario, useMpRulesVictoryCondition) },
	{ nullptr, nullptr, nullptr, 0 }
};

// RotWK's LivingWorldVictoryType table (RW 0xC7A710)
const FieldParse kVictoryTypeParse[] = {
	{ "DisplayObjectives", INI::parseAsciiString, nullptr, (int)offsetof(VictoryType, displayObjectives) },
	{ "DisplayGameType", INI::parseAsciiString, nullptr, (int)offsetof(VictoryType, displayGameType) },
	{ "DisplayVictoriousText", INI::parseAsciiString, nullptr, (int)offsetof(VictoryType, displayVictoriousText) },
	{ "DisplayDefeatedText", INI::parseAsciiString, nullptr, (int)offsetof(VictoryType, displayDefeatedText) },
	{ "PlayerDefeatCondition", parseAppended<PlayerDefeatCondition, kPlayerDefeatParse>, nullptr, (int)offsetof(VictoryType, playerDefeatConditions) },
	{ "TeamDefeatCondition", parseAppended<TeamDefeatCondition, kTeamDefeatParse>, nullptr, (int)offsetof(VictoryType, teamDefeatConditions) },
	{ "TeamVictoryCondition", parseAppended<TeamVictoryCondition, kTeamVictoryParse>, nullptr, (int)offsetof(VictoryType, teamVictoryConditions) },
	{ nullptr, nullptr, nullptr, 0 }
};

// Scenario::ParseINI (retail 0x004FD94E): a new scenario parsed, then it replaces the campaign's (replaceScenario 0x004FC8B3)
void parseScenario(INI *ini, void *, void *store, const void *)
{
	auto scenario = std::make_shared<Scenario>();
	ini->initFromINI(scenario.get(), kScenarioParse);
	*static_cast<std::shared_ptr<Scenario> *>(store) = scenario;
}

// SpawnArmy (LivingWorldCampaignObjects.cpp)
const FieldParse kSpawnArmyParse[] = {
	{ "Icon", INI::parseAsciiString, nullptr, (int)offsetof(LivingWorldSpawnArmy, icon) },
	{ "Banner", INI::parseAsciiString, nullptr, (int)offsetof(LivingWorldSpawnArmy, banner) },
	{ "Position", INI::parseCoord2D, nullptr, (int)offsetof(LivingWorldSpawnArmy, position) },
	{ "InitialRegion", INI::parseAsciiString, nullptr, (int)offsetof(LivingWorldSpawnArmy, initialRegion) },
	{ "PlayerArmy", INI::parseAsciiString, nullptr, (int)offsetof(LivingWorldSpawnArmy, playerArmy) },
	{ "PalantirMovie", INI::parseAsciiString, nullptr, (int)offsetof(LivingWorldSpawnArmy, palantirMovie) },
	{ "IconSize", INI::parseAsciiString, nullptr, (int)offsetof(LivingWorldSpawnArmy, iconSize) },
	{ "SpawnForTemplates", INI::parseAsciiStringVector, nullptr, (int)offsetof(LivingWorldSpawnArmy, spawnForTemplates) },
	{ "ScriptingName", INI::parseAsciiString, nullptr, (int)offsetof(LivingWorldSpawnArmy, scriptingName) },
	{ "TooltipStringTag", INI::parseAsciiString, nullptr, (int)offsetof(LivingWorldSpawnArmy, tooltipStringTag) },
	{ "IsCity", INI::parseBool, nullptr, (int)offsetof(LivingWorldSpawnArmy, isCity) },
	{ "HeroTemplateName", INI::parseAsciiString, nullptr, (int)offsetof(LivingWorldSpawnArmy, heroTemplateName) },
	{ "MoveSpeed", INI::parseReal, nullptr, (int)offsetof(LivingWorldSpawnArmy, moveSpeed) },
	{ "BuildTime", INI::parseInt, nullptr, (int)offsetof(LivingWorldSpawnArmy, buildTime) },
	{ "ConstructButtonImage", INI::parseAsciiString, nullptr, (int)offsetof(LivingWorldSpawnArmy, constructButtonImage) },
	{ "ConstructButtonTitle", INI::parseAsciiString, nullptr, (int)offsetof(LivingWorldSpawnArmy, constructButtonTitle) },
	{ "ConstructButtonHelp", INI::parseAsciiString, nullptr, (int)offsetof(LivingWorldSpawnArmy, constructButtonHelp) },
	{ "SpawnAtActStart", INI::parseBool, nullptr, (int)offsetof(LivingWorldSpawnArmy, spawnAtActStart) },
	{ nullptr, nullptr, nullptr, 0 }
};

// EyeTowerPoints (ParseEyeTowerPointData.cpp, table retail VA 0x00C62034): LookPoint appends a Coord2D
void parseLookPoint(INI *ini, void *, void *store, const void *)
{
	Coord2D point;
	INI::parseCoord2D(ini, nullptr, &point, nullptr);
	static_cast<std::vector<Coord2D> *>(store)->push_back(point);
}
const FieldParse kEyeTowerParse[] = {
	{ "LookPoint", parseLookPoint, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 }
};
void parseEyeTowerPoints(INI *ini, void *, void *store, const void *)
{
	ini->initFromINI(store, kEyeTowerParse);
}

// the Act table (LivingWorldCampaignAct.cpp)
const FieldParse kActParse[] = {
	{ "EnableRegion", parseNotPorted, "EnableRegion", 0 },
	{ "ForceBattle", parseNotPorted, "ForceBattle", 0 },
	{ "SpawnArmy", parseAppended<LivingWorldSpawnArmy, kSpawnArmyParse>, nullptr, (int)offsetof(LivingWorldCampaignAct, spawnArmies) },
	{ "MoveArmy", parseNotPorted, "MoveArmy", 0 },
	{ "SpawnBuilding", parseNotPorted, "SpawnBuilding", 0 },
	{ "CallActSubroutine", INI::parseAsciiStringVectorAppend, nullptr, (int)offsetof(LivingWorldCampaignAct, callActSubroutine) },
	{ "JumpToAct", INI::parseAsciiString, nullptr, (int)offsetof(LivingWorldCampaignAct, jumpToAct) },
	{ "MoveCamera", parseNotPorted, "MoveCamera", 0 },
	{ "SplineCamera", parseNotPorted, "SplineCamera", 0 },
	{ "WorldText", parseNotPorted, "WorldText", 0 },
	{ "AudioEvent", parseNotPorted, "AudioEvent", 0 },
	{ "EndAct", INI::parseBool, nullptr, (int)offsetof(LivingWorldCampaignAct, endAct) },
	{ "UpdateAnimObject", parseNotPorted, "UpdateAnimObject", 0 },
	{ "EyeTowerPoints", parseEyeTowerPoints, nullptr, (int)offsetof(LivingWorldCampaignAct, eyeTowerPoints) },
	{ "SetPlayerControlOfArmy", parseNotPorted, "SetPlayerControlOfArmy", 0 },
	{ nullptr, nullptr, nullptr, 0 }
};

// ParseLivingWorldCampaignAct: the act's name token (none throws), then its fields
void parseAct(INI *ini, void *, void *store, const void *)
{
	const char *token = ini->getNextTokenOrNull();
	if (!token || !*token)
	{
		throw INIException(3, "ParseLivingWorldCampaignAct::No act name specified.");
	}
	std::vector<LivingWorldCampaignAct> &acts = *static_cast<std::vector<LivingWorldCampaignAct> *>(store);
	acts.emplace_back();
	acts.back().name = token;
	ini->initFromINI(&acts.back(), kActParse);
}

// the campaign table (RotWK RW 0xC7F8C8; BFME2 LivingWorldCampaign.cpp without LivingWorldVictoryType)
const FieldParse kCampaignParse[] = {
	{ "AddPlayer", parseNotPorted, "AddPlayer", 0 },
	{ "LocalPlayer", INI::parseAsciiString, nullptr, (int)offsetof(LivingWorldCampaign, localPlayer) },
	{ "Act", parseAct, nullptr, (int)offsetof(LivingWorldCampaign, acts) },
	{ "SecondsPerReinforcement", INI::parseInt, nullptr, (int)offsetof(LivingWorldCampaign, secondsPerReinforcement) },
	{ "IsScriptedCampaign", INI::parseBool, nullptr, (int)offsetof(LivingWorldCampaign, isScriptedCampaign) },
	{ "IsEvilCampaign", INI::parseBool, nullptr, (int)offsetof(LivingWorldCampaign, isEvilCampaign) },
	{ "ForceAdvanceTurnPhase", INI::parseBool, nullptr, (int)offsetof(LivingWorldCampaign, forceAdvanceTurnPhase) },
	{ "ScriptHolder", INI::parseAsciiString, nullptr, (int)offsetof(LivingWorldCampaign, scriptHolder) },
	{ "LivingWorldVictoryType", parseAppended<VictoryType, kVictoryTypeParse>, nullptr, (int)offsetof(LivingWorldCampaign, victoryTypes) }, // RotWK RW 0x9028F5
	{ "Scenario", parseScenario, nullptr, (int)offsetof(LivingWorldCampaign, scenario) },
	{ "Tutorial", parseNotPorted, "Tutorial", 0 },
	{ "StartingCashRTS", INI::parseUnsignedInt, nullptr, (int)offsetof(LivingWorldCampaign, startingCashRTS) },
	{ "StartingCashRTSWithFort", INI::parseUnsignedInt, nullptr, (int)offsetof(LivingWorldCampaign, startingCashRTSWithFort) },
	{ "InitialRevivalCostMultiplier", INI::parseReal, nullptr, (int)offsetof(LivingWorldCampaign, initialRevivalCostMultiplier) },
	{ "InitialRevivalTimeMultiplier", INI::parseReal, nullptr, (int)offsetof(LivingWorldCampaign, initialRevivalTimeMultiplier) },
	{ nullptr, nullptr, nullptr, 0 }
};
} // namespace

void LivingWorldCampaignManager::registerBlock(INIBlockRegistry &registry)
{
	registry.registerBlock("LivingWorldCampaign", [](INI *ini) {
		if (!TheLivingWorldCampaignManager)
		{
			throw INIException(8, "LivingWorldCampaign block: TheLivingWorldCampaignManager is not installed");
		}
		TheLivingWorldCampaignManager->parseLivingWorldCampaign(ini);
	});
}

void LivingWorldCampaignManager::parseLivingWorldCampaign(INI *ini)
{
	// the campaign constructed with its name (ctor retail 0x0052CF0F), appended, then its fields
	LivingWorldCampaign c;
	c.name = ini->getNextToken();
	m_campaigns.push_back(c);
	ini->initFromINI(&m_campaigns.back(), kCampaignParse);
}

std::vector<int> LivingWorldCampaignManager::scenarioCampaignIndexes() const
{
	std::vector<int> out;
	for (std::size_t i = 0; i < m_campaigns.size(); ++i)
	{
		if (m_campaigns[i].scenario)
		{
			out.push_back((int)i);
		}
	}
	return out;
}

std::vector<std::string> LivingWorldCampaignManager::stopLines()
{
	return {
		"[S-5300] LivingWorldCampaign: AddPlayer, Tutorial and the scripted Act commands (EnableRegion, ForceBattle, MoveArmy, SpawnBuilding, MoveCamera, "
		"SplineCamera, WorldText, AudioEvent, UpdateAnimObject, SetPlayerControlOfArmy) are not ported; RotWK's RiskCampaign.ini uses none of them and a "
		"file that does fails its load with the field's name",
	};
}
