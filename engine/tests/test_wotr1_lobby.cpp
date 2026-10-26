// OpenBFME unit tests: lane WOTR-1, TheLivingWorldCampaignManager (GameClient/LivingWorldCampaign.h): the LivingWorldCampaign / Scenario / Act grammar of the
// BFME2 decompile's tables, the fields RotWK's data does not use (stop S-5300), and the War of the Ring scenarios the retail RiskCampaign.ini gives the
// strategic lobby. GPL-3.0.
#include "doctest.h"

#include "StartTestUtil.h"

#include "Common/INI.h"
#include "Common/INIException.h"
#include "GameClient/GUI/Skirmish/WorldSkirmishSetupSource.h"
#include "GameClient/LivingWorldCampaign.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <string>
#include <vector>

namespace
{
struct Parsed
{
	LivingWorldCampaignManager manager;
	std::string error;
};

void parse(Parsed &out, const std::string &text)
{
	INIEnvironment env;
	out.manager.registerBlock(env.blocks);
	LivingWorldCampaignManager *previous = TheLivingWorldCampaignManager;
	TheLivingWorldCampaignManager = &out.manager;
	try
	{
		INI ini(env);
		std::vector<std::uint8_t> bytes(text.begin(), text.end());
		ini.loadMemory("Data\\INI\\Campaigns\\wotr1.ini", bytes, INI_LOAD_OVERWRITE);
	}
	catch (const INIException &e)
	{
		out.error = e.what();
	}
	TheLivingWorldCampaignManager = previous;
}
} // namespace

TEST_CASE("wotr1 LivingWorldCampaign: the campaign, Scenario and Act tables of the BFME2 decompile, the constructors' defaults, Teams 0-based")
{
	Parsed p;
	parse(p,
		"LivingWorldCampaign WOTR1_A\n"
		"  IsEvilCampaign = Yes\n"
		"  StartingCashRTS = 6000\n"
		"  Scenario\n"
		"    DisplayName = LWScenario:Name\n"
		"    DisplayDescription = LWScenario:Desc\n"
		"    DisplayGameType = LWScenario:Type\n"
		"    MaxPlayers = 4\n"
		"    DefaultStartSpots = Rohan Mordor Arnor Erebor\n"
		"    DisallowStartInRegions = Harad Rhun\n"
		"    PlayerDefeatCondition\n"
		"      Teams = 1 2\n"
		"      LoseIfCapitalLost = Yes\n"
		"      NumControlledRegionsLessOrEqualTo = 0\n"
		"    End\n"
		"    TeamVictoryCondition\n"
		"      Teams = 2\n"
		"      ControlledRegions = Gondor Rohan\n"
		"      ControlledRegionsHeldForTurns = 3\n"
		"    End\n"
		"    OwnershipSet\n"
		"      Regions = Rohan Isengard\n"
		"      StartRegion = Rohan\n"
		"      SpawnArmies\n"
		"        Armies = HeroArmy1 GarrisonArmy1\n"
		"        Region = Rohan\n"
		"      End\n"
		"      SpawnBuildings\n"
		"        Buildings = LWB_MenFortress\n"
		"        Region = Rohan\n"
		"      End\n"
		"    End\n"
		"    StartingRestriction\n"
		"      Regions = Mordor\n"
		"      Factions = PlayerMordor PlayerAngmar PlayerMordor\n"
		"      Teams = 1\n"
		"    End\n"
		"  End\n"
		"  Act One\n"
		"    SpawnArmy\n"
		"      ScriptingName = HeroArmy1\n"
		"      SpawnForTemplates = PlayerMen\n"
		"      HeroTemplateName = GondorAragornMP\n"
		"      MoveSpeed = 15.0\n"
		"    End\n"
		"    EyeTowerPoints\n"
		"      LookPoint = X:436 Y:687\n"
		"      LookPoint = X:481 Y:287\n"
		"    End\n"
		"  End\n"
		"End\n"
		"LivingWorldCampaign WOTR1_NO_SCENARIO\n"
		"End\n");
	REQUIRE_MESSAGE(p.error.empty(), p.error);
	const auto &campaigns = p.manager.campaigns();
	REQUIRE(campaigns.size() == 2);
	const LivingWorldCampaign &a = campaigns[0];
	CHECK(a.name == "WOTR1_A");
	CHECK(a.isEvilCampaign);
	CHECK(a.startingCashRTS == 6000u);
	// the constructor's defaults (retail 0x0052CF0F): 180, 7500, 2500, 2.0, 1.0
	CHECK(a.secondsPerReinforcement == 180);
	CHECK(a.startingCashRTSWithFort == 2500u);
	CHECK(a.initialRevivalCostMultiplier == 2.0f);
	CHECK(a.initialRevivalTimeMultiplier == 1.0f);
	CHECK(campaigns[1].startingCashRTS == 7500u);
	REQUIRE(a.scenario);
	const LivingWorldScenario::Scenario &s = *a.scenario;
	CHECK(s.displayName == "LWScenario:Name");
	CHECK(s.displayDescription == "LWScenario:Desc");
	CHECK(s.maxPlayers == 4);
	CHECK(s.minPlayers == 2); // ctor (Rva004FD0DACtor.cpp)
	CHECK(s.defaultStartSpots == std::vector<std::string>{ "Rohan", "Mordor", "Arnor", "Erebor" });
	CHECK(s.disallowStartInRegions == std::vector<std::string>{ "Harad", "Rhun" });
	REQUIRE(s.playerDefeatConditions.size() == 1);
	CHECK(s.playerDefeatConditions[0].teams == std::vector<int>{ 0, 1 }); // Rva004FCB23Parse: read 1-based, kept 0-based
	CHECK(s.playerDefeatConditions[0].loseIfCapitalLost);
	REQUIRE(s.teamVictoryConditions.size() == 1);
	CHECK(s.teamVictoryConditions[0].teams == std::vector<int>{ 1 });
	CHECK(s.teamVictoryConditions[0].controlledRegionsHeldForTurns == 3);
	REQUIRE(s.ownershipSets.size() == 1);
	CHECK(s.ownershipSets[0].startRegion == "Rohan");
	REQUIRE(s.ownershipSets[0].spawnArmies.size() == 1);
	CHECK(s.ownershipSets[0].spawnArmies[0].names == std::vector<std::string>{ "HeroArmy1", "GarrisonArmy1" });
	REQUIRE(s.ownershipSets[0].spawnBuildings.size() == 1);
	CHECK(s.ownershipSets[0].spawnBuildings[0].names == std::vector<std::string>{ "LWB_MenFortress" });
	REQUIRE(s.startingRestrictions.size() == 1);
	CHECK(s.startingRestrictions[0].factions == std::vector<std::string>{ "PlayerAngmar", "PlayerMordor" }); // a set: sorted, no duplicate
	REQUIRE(a.acts.size() == 1);
	CHECK(a.acts[0].name == "One");
	REQUIRE(a.acts[0].spawnArmies.size() == 1);
	CHECK(a.acts[0].spawnArmies[0].heroTemplateName == "GondorAragornMP");
	CHECK(a.acts[0].spawnArmies[0].moveSpeed == 15.0f);
	CHECK(a.acts[0].spawnArmies[0].spawnAtActStart);
	REQUIRE(a.acts[0].eyeTowerPoints.size() == 2);
	CHECK(a.acts[0].eyeTowerPoints[1].x == 481.0f);
	CHECK(a.acts[0].eyeTowerPoints[1].y == 287.0f);
	// only the campaigns with a Scenario are lobby scenarios (BFME2 PopulateStrategicScenario's labelled campaigns)
	CHECK(p.manager.scenarioCampaignIndexes() == std::vector<int>{ 0 });
}

TEST_CASE("wotr1 LivingWorldCampaign: a field RotWK's data does not use fails the load with its name (stop S-5300); an Act needs a name")
{
	for (const char *field : { "AddPlayer", "Tutorial" })
	{
		Parsed p;
		parse(p, std::string("LivingWorldCampaign X\n  ") + field + "\n  End\nEnd\n");
		CHECK_MESSAGE(p.error.find(field) != std::string::npos, field << ": " << p.error);
		CHECK(p.error.find("S-5300") != std::string::npos);
	}
	{
		Parsed p;
		parse(p, "LivingWorldCampaign X\n  Act One\n    MoveCamera\n    End\n  End\nEnd\n");
		CHECK(p.error.find("MoveCamera") != std::string::npos);
	}
	{
		Parsed p;
		parse(p, "LivingWorldCampaign X\n  Act\n  End\nEnd\n");
		CHECK(p.error.find("No act name specified") != std::string::npos);
	}
	CHECK(LivingWorldCampaignManager::stopLines().size() == 1);
}

TEST_CASE("wotr1 retail: RiskCampaign.ini gives the strategic lobby its fifteen scenarios in include order")
{
	OPENBFME_REQUIRE_START(s);
	const LivingWorldCampaignManager &m = s->world->livingWorldCampaigns();
	// RiskCampaign.ini includes 044, 045, 001 .. 011, 042, 043 (its WOTRTutorial.inc include is commented out)
	std::vector<std::string> names;
	for (int index : m.scenarioCampaignIndexes())
	{
		names.push_back(m.campaigns()[(std::size_t)index].name);
	}
	const std::vector<std::string> expected = { "WOTRScenario044", "WOTRScenario045", "WOTRScenario001", "WOTRScenario002", "WOTRScenario003",
		"WOTRScenario004", "WOTRScenario005", "WOTRScenario006", "WOTRScenario007", "WOTRScenario008", "WOTRScenario009", "WOTRScenario010",
		"WOTRScenario011", "WOTRScenario042", "WOTRScenario043" };
	CHECK(names == expected);
	for (int index : m.scenarioCampaignIndexes())
	{
		const LivingWorldCampaign &c = m.campaigns()[(std::size_t)index];
		INFO(c.name);
		CHECK(c.scenario->displayName == "LWScenario:" + c.name);
		CHECK(c.scenario->displayDescription == "LWScenario:" + c.name + "Description");
		CHECK((c.scenario->maxPlayers >= 2 && c.scenario->maxPlayers <= 6)); // tolerance: each scenario sets its own MaxPlayers within the lobby's six slots
		CHECK(!c.acts.empty());
	}
	// the setup source the lobby reads has the same list
	WorldSkirmishSetupSource source;
	std::string error;
	REQUIRE_MESSAGE(source.load(*s->world, s->settings, *s->mount->fs, &error), error);
	REQUIRE(source.strategicScenarios().size() == expected.size());
	CHECK(source.strategicScenarios()[0].campaignName == "WOTRScenario044");
	CHECK(source.strategicScenarios()[0].campaignIndex == m.scenarioCampaignIndexes()[0]);
}
