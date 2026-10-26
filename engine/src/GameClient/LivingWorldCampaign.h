// OpenBFME. GPL-3.0.
//
// Lane WOTR-1: TheLivingWorldCampaignManager's campaigns, the War of the Ring scenarios. The subsystem legend loads Data\INI\Campaigns\RiskCampaign.ini
// (LoadSubsystem TheLivingWorldCampaignManager); each LivingWorldCampaign block is one scenario of the War of the Ring lobby's StrategicScenarioType combo.
//
// TARGET FACTS (RotWK game.dat, the field tables read from .rdata: each row {name, parser, user data, offset}): the campaign table RW 0xC7F8C8 is BFME2's
// plus LivingWorldVictoryType (RW 0x9028F5: a 0x5C-byte victory type (RW 0x9019DF) filled from the table RW 0xC7A710 {DisplayObjectives, DisplayGameType,
// DisplayVictoriousText, DisplayDefeatedText, PlayerDefeatCondition, TeamDefeatCondition, TeamVictoryCondition} and appended to the campaign's list at
// + 0x24; no name token; RotWK's LivingWorldVictoryTypes.inc says their order is the rules tab's VictoryType order); the Scenario table RW 0xC7A578 (parser
// RW 0x902874: a 0xC4-byte scenario, ctor RW 0x901DD5: MaxPlayers 6, MinPlayers 2, HistoricalScenario No, UseMpRulesVictoryCondition Yes; then
// replaceScenario RW 0x9010CD) is BFME2's plus DisabledFactions (string vector), HistoricalScenario and UseMpRulesVictoryCondition (bools). The
// condition, OwnershipSet (RW 0xC88760), SpawnBuildings / SpawnArmies (RW 0xC88640 / 0xC886A8), StartingRestriction (RW 0xC887D0), Act (RW 0xC84090),
// SpawnArmy (RW 0xC78430) and EyeTowerPoints (RW 0xC78D14) tables have BFME2's rows.
// DONOR (Open-BFME-2, the BFME2 1.06 decompile, for the parsers' behaviour):
//   * LivingWorldCampaign (GameLogic/System/LivingWorld/LivingWorldCampaign.cpp, ctor Common/LivingWorldCampaign.cpp retail 0x0052CF0F): the field table
//       AddPlayer            LivingWorldPlayer::ParseLivingWorldPlayer
//       LocalPlayer          parseAsciiString       (+0x30)
//       Act                  ParseLivingWorldCampaignAct (LivingWorldCampaignAct.cpp: the act's name token, then its own table)
//       SecondsPerReinforcement parseInt            (+0x38, ctor 180)
//       IsScriptedCampaign   parseBool              (+0x4C)
//       IsEvilCampaign       parseBool              (+0x4D)
//       ForceAdvanceTurnPhase parseBool             (+0x4E)
//       ScriptHolder         parseAsciiString       (+0x34)
//       Scenario             LivingWorldScenario::Scenario::ParseINI (a new scenario replaces the campaign's, LivingWorldScenario.cpp replaceScenario)
//       Tutorial             Rva003F9258Parse
//       StartingCashRTS      parseUnsignedInt       (+0x3C, ctor 7500)
//       StartingCashRTSWithFort parseUnsignedInt    (+0x40, ctor 2500)
//       InitialRevivalCostMultiplier parseReal      (+0x44, ctor 2.0)
//       InitialRevivalTimeMultiplier parseReal      (+0x48, ctor 1.0)
//   * Scenario (LivingWorldScenario.cpp, table retail 0x00863870; ctor Rva004FD0DACtor.cpp: MaxPlayers 6, MinPlayers 2): DisplayName, DisplayDescription,
//     DisplayFiction, DisplayObjectives, DisplayGameType, DisplayVictoriousText, DisplayDefeatedText, RegionCampaign (strings); DisableRegions,
//     DisallowStartInRegions, DefaultStartSpots (string vectors); MaxPlayers, MinPlayers (ints); the sub-blocks PlayerDefeatCondition {Teams,
//     LoseIfCapitalLost, NumControlledRegionsLessOrEqualTo}, TeamDefeatCondition {Teams, NumControlledRegionsLessOrEqualTo}, TeamVictoryCondition {Teams,
//     NumControlledRegionsGreaterOrEqualTo, ControlledRegions, ControlledRegionsHeldForTurns}, OwnershipSet {Regions, SpawnBuildings {Buildings, Region},
//     SpawnArmies {Armies, Region}, StartRegion}, StartingRestriction {Regions, Factions, Teams}. Teams (Rva004FCB23Parse, INIThinFieldParsers2.cpp) is an
//     int list read 1-based and stored 0-based.
//   * Act (LivingWorldCampaignAct.cpp): SpawnArmy (LivingWorldCampaignObjects.cpp: Icon, Banner, Position, InitialRegion, PlayerArmy, PalantirMovie,
//     IconSize, SpawnForTemplates, ScriptingName, TooltipStringTag, IsCity, HeroTemplateName, MoveSpeed, BuildTime, ConstructButtonImage /Title /Help,
//     SpawnAtActStart), EyeTowerPoints (ParseEyeTowerPointData.cpp: LookPoint, a Coord2D appended), CallActSubroutine (append), JumpToAct, EndAct.
// NOT PORTED (stop S-5300, reported when data uses them, never skipped silently): AddPlayer, Tutorial and the scripted Act commands EnableRegion,
// ForceBattle, MoveArmy, SpawnBuilding, MoveCamera, SplineCamera, WorldText, AudioEvent, UpdateAnimObject, SetPlayerControlOfArmy. RotWK's RiskCampaign.ini
// uses none of them (its WOTRTutorial.inc include is commented out); a data file that does fails its load with the field's name.
#pragma once

#include "Common/INIDataTypes.h"

#include <memory>
#include <string>
#include <vector>

class INI;
class INIBlockRegistry;

namespace LivingWorldScenario
{
struct PlayerDefeatCondition
{
	std::vector<int> teams;          ///< +0x04, 0-based
	int numControlledRegionsLessOrEqualTo = 0; ///< +0x10
	bool loseIfCapitalLost = false;  ///< +0x14
};
struct TeamDefeatCondition
{
	std::vector<int> teams;          ///< +0x04, 0-based
	int numControlledRegionsLessOrEqualTo = 0; ///< +0x10
};
struct TeamVictoryCondition
{
	std::vector<int> teams;                     ///< +0x04, 0-based
	int numControlledRegionsGreaterOrEqualTo = 0; ///< +0x10
	std::vector<std::string> controlledRegions; ///< +0x14
	int controlledRegionsHeldForTurns = 0;      ///< +0x20
};
struct SpawnList
{
	std::vector<std::string> names; ///< +0x04 Buildings / Armies
	std::string region;             ///< +0x10
};
struct OwnershipSet
{
	std::vector<std::string> regions;   ///< +0x1C
	std::vector<SpawnList> spawnBuildings;
	std::vector<SpawnList> spawnArmies;
	std::string startRegion;            ///< +0x28
};
struct StartingRestriction
{
	std::vector<std::string> regions;  ///< +0x08
	std::vector<std::string> factions; ///< +0x04 (a set in retail: duplicates dropped, sorted)
	std::vector<int> teams;            ///< +0x14, 0-based
};
// RotWK's LivingWorldVictoryType (RW 0x9028F5, table RW 0xC7A710)
struct VictoryType
{
	std::string displayObjectives;     ///< +0x04
	std::string displayGameType;       ///< +0x08
	std::string displayVictoriousText; ///< +0x0C
	std::string displayDefeatedText;   ///< +0x10
	std::vector<PlayerDefeatCondition> playerDefeatConditions;
	std::vector<TeamDefeatCondition> teamDefeatConditions;
	std::vector<TeamVictoryCondition> teamVictoryConditions;
};
struct Scenario
{
	std::string displayName;        ///< +0x04 (a game text label, LWScenario:...)
	std::string displayDescription; ///< +0x08
	std::string displayFiction;     ///< +0x0C
	std::string displayObjectives;  ///< +0x10
	std::string displayGameType;    ///< +0x14
	std::string displayVictoriousText; ///< +0x18
	std::string displayDefeatedText;   ///< +0x1C
	int maxPlayers = 6;             ///< +0x20 (ctor)
	int minPlayers = 2;             ///< +0x24 (ctor)
	std::string regionCampaign;     ///< +0x28
	std::vector<std::string> disableRegions;         ///< +0x2C
	std::vector<std::string> disallowStartInRegions; ///< +0x38
	std::vector<std::string> defaultStartSpots;      ///< +0x44
	std::vector<PlayerDefeatCondition> playerDefeatConditions;
	std::vector<TeamDefeatCondition> teamDefeatConditions;
	std::vector<TeamVictoryCondition> teamVictoryConditions;
	std::vector<OwnershipSet> ownershipSets;
	std::vector<StartingRestriction> startingRestrictions;
	std::vector<std::string> disabledFactions; ///< RotWK + 0x40 DisabledFactions
	bool historicalScenario = false;           ///< RotWK + 0xC0 (ctor RW 0x901DD5)
	bool useMpRulesVictoryCondition = true;    ///< RotWK + 0xC1 (ctor: Yes): the rules tab's VictoryType picks the victory type
};
} // namespace LivingWorldScenario

struct LivingWorldSpawnArmy
{
	std::string icon, banner;              ///< +0x04, +0x08
	std::string constructButtonImage, constructButtonTitle, constructButtonHelp; ///< +0x0C .. +0x14
	std::string scriptingName;             ///< +0x18
	std::string tooltipStringTag;          ///< +0x1C
	Coord2D position;                      ///< +0x20
	std::string initialRegion;             ///< +0x28
	std::string playerArmy;                ///< +0x2C
	std::string palantirMovie;             ///< +0x30
	std::string iconSize;                  ///< +0x34
	std::vector<std::string> spawnForTemplates; ///< +0x38
	float moveSpeed = 0.0f;                ///< +0x44
	int buildTime = 0;                     ///< +0x48
	std::string heroTemplateName;          ///< +0x50
	bool isCity = false;                   ///< +0x54
	bool spawnAtActStart = true;           ///< +0x55 (LivingWorldDefaultArmies.inc: "Default is Yes")
};

struct LivingWorldCampaignAct
{
	std::string name;
	std::vector<LivingWorldSpawnArmy> spawnArmies;
	std::vector<Coord2D> eyeTowerPoints;   ///< EyeTowerPoints' LookPoint entries, in order
	std::vector<std::string> callActSubroutine; ///< +0x44
	std::string jumpToAct;                 ///< +0x50
	bool endAct = false;                   ///< +0xB4
};

struct LivingWorldCampaign
{
	std::string name;                      ///< +0x04 (the block's token)
	std::vector<LivingWorldCampaignAct> acts;
	std::string localPlayer;               ///< +0x30
	std::string scriptHolder;              ///< +0x34
	int secondsPerReinforcement = 180;     ///< +0x38 (ctor 0xB4)
	unsigned startingCashRTS = 7500;       ///< +0x3C (ctor 0x1D4C)
	unsigned startingCashRTSWithFort = 2500; ///< +0x40 (ctor 0x9C4)
	float initialRevivalCostMultiplier = 2.0f; ///< +0x44
	float initialRevivalTimeMultiplier = 1.0f; ///< +0x48
	bool isScriptedCampaign = false;       ///< +0x4C
	bool isEvilCampaign = false;           ///< +0x4D
	bool forceAdvanceTurnPhase = false;    ///< +0x4E
	std::shared_ptr<LivingWorldScenario::Scenario> scenario; ///< the Scenario sub-block (null: a campaign without one is not a lobby scenario)
	std::vector<LivingWorldScenario::VictoryType> victoryTypes; ///< RotWK + 0x24: LivingWorldVictoryType, in file order
};

class LivingWorldCampaignManager
{
public:
	void registerBlock(INIBlockRegistry &registry);
	void parseLivingWorldCampaign(INI *ini);
	const std::vector<LivingWorldCampaign> &campaigns() const { return m_campaigns; }
	// the campaigns with a Scenario, in definition order: what AptMapPreview::PopulateStrategicScenario lists (BFME2 0x0057D4F3 over the manager's
	// labelled campaigns, rva003B92B9); each entry's index is its index in campaigns() (the combo's item data, GameInfo's campaign)
	std::vector<int> scenarioCampaignIndexes() const;
	static std::vector<std::string> stopLines();

private:
	std::vector<LivingWorldCampaign> m_campaigns;
};

// the manager the LivingWorldCampaign block fills (RetailObjectWorld installs its own); nullptr when none
extern thread_local LivingWorldCampaignManager *TheLivingWorldCampaignManager;
