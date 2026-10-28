// OpenBFME. GPL-3.0. Client-side strategic lobby territory selection.
// BFME2 AptMapPreview.cpp: AllowsStartInRegion, GetRegionsInOwnershipSet,
// UpdateStrategicStartSpots, OnMouseoverRegionChanged_internal; Rva004FD6F9.cpp
// excludes DisallowStartInRegions and Rva004FD448.cpp expands ownership groups.
// Selection uses names until strategic region-ID serialization is ported (S-5301).
#pragma once
#include "GameClient/LivingWorldCampaign.h"
#include "GameClient/LivingWorldMap.h"
#include <array>
struct LivingWorldTerritoryPlayer {
 bool occupied = false, selectable = false;
 int team = -1;
 std::string faction;
 std::uint32_t color = 0x1000000; // outside RGB: unresolved/random colour
};
class LivingWorldTerritorySelection {
public:
 void configure(const LivingWorldRegionCampaign &regions, const LivingWorldScenario::Scenario &scenario,
                const std::string &key, const std::array<LivingWorldTerritoryPlayer,8> &players);
 void updatePlayers(const std::array<LivingWorldTerritoryPlayer,8> &players) {configure(m_regions,m_scenario,m_key,players);}
 const LivingWorldRegionCampaign &regions() const { return m_regions; }
 const std::array<std::string,8> &starts() const { return m_starts; }
 const std::array<LivingWorldTerritoryPlayer,8> &players() const { return m_players; }
 std::vector<std::string> group(const std::string &region) const;
 std::string canonical(const std::string &region) const;
 bool allowed(const std::string &region, int slot = -1) const;
 int holder(const std::string &region) const;
 bool pick(const std::string &region, bool left);
private:
 LivingWorldRegionCampaign m_regions;
 LivingWorldScenario::Scenario m_scenario;
 std::string m_key;
 std::array<LivingWorldTerritoryPlayer,8> m_players{};
 std::array<std::string,8> m_starts{};
};
