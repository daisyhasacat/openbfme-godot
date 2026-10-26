// OpenBFME. GPL-3.0. Living World territory data, synthetic grammar and mounted retail corpus.
#include "doctest.h"
#include "StartTestUtil.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "GameClient/LivingWorldMap.h"

namespace {
std::string parseMap(LivingWorldMapStore &store, const std::string &text)
{
 INIEnvironment env; store.registerBlocks(env.blocks);
 auto *previous = TheLivingWorldMapStore;
 TheLivingWorldMapStore = &store;
 std::string error;
 try {
  INI ini(env);
  ini.loadMemory("Data\\INI\\territory-test.ini", std::vector<std::uint8_t>(text.begin(),text.end()), INI_LOAD_OVERWRITE);
 } catch (const INIException &e) { error = e.what(); }
 TheLivingWorldMapStore = previous;
 return error;
}
}
TEST_CASE("wotr1 map: camera, regions, connections, restrictions and named effects")
{
 LivingWorldMapStore store;
 REQUIRE(parseMap(store,
  "LivingWorldMapInfo\n MapObject = TestMap\n AptCenter = X:12 Y:34\n AptZoom = 0.6\n AptPitch = 0.3\n AddShadowSubObject = A\n AddShadowSubObject = B\n ShadowColor = R:60 G:60 B:80 A:120\nEnd\n"
  "LivingWorldRegionEffects TestEffects\n RegionObject = TestRegions\n ShouldShowRegionBorders = Yes\n BordersEffect\n Opacity = 0.5\n Geometry = Border\n ColorIntensityControlPoint\n Intensity = 0.8\n Time = 1.25\n EaseIn = 0.1\n End\n End\nEnd\n"
  "LivingWorldRegionCampaign TestCampaign\n RegionEffectsManagerName = TestEffects\n Region A\n SubObject = REGION_A\n DisplayName = Region:A\n ConnectsTo = B C\n Connection\n Region = D\n DetourPoint = X:5 Y:7\n End\n RestrictBuildings\n Buildings = FORTRESS RESOURCE\n NumberAllowed = 2\n End\n ArmyPlacementPos = X:1 Y:2\n End\n ConcurrentRegionBonus\n Territory = TestTerritory\n Regions = A B\n ResourceBonus = 10\n End\nEnd\n").empty());
 CHECK(store.mapInfo().defined);
 CHECK(store.mapInfo().mapObject == "TestMap");
 CHECK(store.mapInfo().aptCenter.x == 12);
 CHECK(store.mapInfo().aptCenter.y == 34);
 CHECK(store.mapInfo().shadowColor.alpha == 120);
 CHECK(store.mapInfo().addShadowSubObjects == std::vector<std::string>{"A","B"});
 const auto *effects = store.findRegionEffects("testeffects"); REQUIRE(effects);
 CHECK(effects->shouldShowRegionBorders);
 const auto &border = effects->effects.at("BordersEffect");
 CHECK(border.opacity == 0.5f);
 REQUIRE(border.intensityPoints.size() == 1);
 CHECK(border.intensityPoints[0].timeMs == 1250);
 CHECK(border.geometry == std::vector<std::string>{"Border"});
 const auto *campaign = store.findRegionCampaign("TESTCAMPAIGN"); REQUIRE(campaign);
 REQUIRE(campaign->regions.size() == 1);
 const auto &region = campaign->regions[0];
 CHECK(region.name == "A");
 REQUIRE(region.connections.size() == 3);
 CHECK(region.connections[0].region == "B");
 CHECK(region.connections[2].region == "D");
 CHECK(region.connections[2].detourPoints[0].y == 7);
 CHECK(region.restrictBuildings[0].buildings == std::vector<int>{1,3});
 CHECK(region.restrictBuildings[0].numberAllowed == 2);
 CHECK(campaign->concurrentRegionBonuses[0].regions == std::vector<std::string>{"A","B"});
 CHECK(store.findRegionCampaign("absent") == nullptr);
}
TEST_CASE("wotr1 map: unsupported and unknown fields fail with their names")
{
 for (const auto &entry : std::vector<std::pair<std::string,std::string>>{
  {"LivingWorldMapInfo\n EyeTower\nEnd\n","EyeTower"},
  {"LivingWorldRegionEffects X\n BordersEffect\n ControlPoint\nEnd\nEnd\n","ControlPoint"},
  {"LivingWorldRegionCampaign X\n Region A\n RegionBonus\nEnd\nEnd\n","RegionBonus"},
  {"LivingWorldMapInfo\n UnknownField = 1\nEnd\n","UnknownField"}}) {
  LivingWorldMapStore store; const auto error = parseMap(store,entry.first);
  CHECK_MESSAGE(error.find(entry.second) != std::string::npos,error);
 }
 for (const char *field : {"MapMatchPlayerWorldToMap","OnEnterGiveUpgrade","OnVictoryGiveUpgrade","OnVictoryRemoveUpgrade"}) {
  LivingWorldMapStore store;
  const auto error = parseMap(store,std::string("LivingWorldRegionCampaign X\n Region A\n ")+field+" = X\n End\nEnd\n");
  CHECK_MESSAGE(error.find(field) != std::string::npos,error);
  CHECK(error.find("stop S-5303") != std::string::npos);
 }
 CHECK(LivingWorldMapStore::stopLines().size() == 1);
}
TEST_CASE("wotr1 map: duplicate named blocks cannot silently shadow new data")
{
 LivingWorldMapStore store;
 REQUIRE(parseMap(store,"LivingWorldRegionEffects Named\nEnd\nLivingWorldRegionCampaign Campaign\nEnd\n").empty());
 CHECK(parseMap(store,"LivingWorldRegionEffects NAMED\nEnd\n").find("stop S-5303") != std::string::npos);
 CHECK(parseMap(store,"LivingWorldRegionCampaign CAMPAIGN\nEnd\n").find("stop S-5303") != std::string::npos);
}
TEST_CASE("wotr1 retail map: mounted territory data and effect references resolve")
{
 OPENBFME_REQUIRE_START(session);
 const auto &store = session->world->livingWorldMap();
 REQUIRE(store.mapInfo().defined);
 CHECK(store.mapInfo().mapObject == "LivingMap");
 REQUIRE_FALSE(store.regionCampaigns().empty());
 for (const auto &campaign : store.regionCampaigns()) {
  INFO(campaign.name);
  REQUIRE_FALSE(campaign.regions.empty());
  CHECK(store.findRegionEffects(campaign.regionEffectsManagerName) != nullptr);
  for (const auto &region : campaign.regions) {
   INFO(region.name);
   CHECK_FALSE(region.subObject.empty());
   for (const auto &connection : region.connections) {
    bool found = false;
    for (const auto &other : campaign.regions) if (other.name == connection.region) found = true;
    CHECK_MESSAGE(found,connection.region);
   }
  }
 }
}
