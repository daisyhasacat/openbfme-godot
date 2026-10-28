// OpenBFME. GPL-3.0. See LivingWorldTerritorySelection.h and docs/TERRITORY_MAP.md.
#include "GameClient/LivingWorldTerritorySelection.h"
#include "Common/AsciiString.h"
#include <algorithm>
namespace {
bool same(const std::string &a,const std::string &b) { return AsciiStringUtil::compareNoCase(a,b)==0; }
bool has(const std::vector<std::string> &names,const std::string &name) {
 return std::any_of(names.begin(),names.end(),[&](const std::string &n){return same(n,name);});
}
}
void LivingWorldTerritorySelection::configure(const LivingWorldRegionCampaign &regions,const LivingWorldScenario::Scenario &scenario,
 const std::string &key,const std::array<LivingWorldTerritoryPlayer,8> &players)
{
 const bool reset = key != m_key;
 if (reset) { m_regions=regions; m_scenario=scenario; m_key=key; m_starts.fill(std::string()); }
 const auto previous=m_players;
 m_players=players;
 for (int i=0;i<8;++i) {
  if (!players[i].occupied) { m_starts[i].clear(); continue; }
  if ((reset || !previous[i].occupied) && (size_t)i<m_scenario.defaultStartSpots.size()) {
   const std::string start=canonical(m_scenario.defaultStartSpots[(size_t)i]);
   if (allowed(start,i) && holder(start)<0) m_starts[i]=start;
  }
  if (!m_starts[i].empty() && !allowed(m_starts[i],i)) m_starts[i].clear();
 }
}
std::vector<std::string> LivingWorldTerritorySelection::group(const std::string &region) const
{
 for (const auto &set:m_scenario.ownershipSets) if (has(set.regions,region) || same(set.startRegion,region)) {
  auto names=set.regions;
  if (!set.startRegion.empty() && !has(names,set.startRegion)) names.push_back(set.startRegion);
  return names;
 }
 for (const auto &r:m_regions.regions) if (same(r.name,region)) return {r.name};
 return {};
}
std::string LivingWorldTerritorySelection::canonical(const std::string &region) const
{
 for (const auto &set:m_scenario.ownershipSets) if (has(set.regions,region) || same(set.startRegion,region)) {
  if (!set.startRegion.empty()) return set.startRegion;
 }
 for (const auto &r:m_regions.regions) if (same(r.name,region)) return r.name;
 return {};
}
bool LivingWorldTerritorySelection::allowed(const std::string &region,int slot) const
{
 const std::string start=canonical(region);
 if (start.empty() || has(m_scenario.disableRegions,region) || has(m_scenario.disableRegions,start)
     || has(m_scenario.disallowStartInRegions,region) || has(m_scenario.disallowStartInRegions,start)) return false;
 if (slot>=0 && slot<8) for (const auto &restriction:m_scenario.startingRestrictions) {
  if (!has(restriction.regions,region) && !has(restriction.regions,start)) continue;
  const auto &player=m_players[(size_t)slot];
  // Client validation of the parsed faction/team restrictions; random factions
  // stay unresolved rather than consuming gameplay RNG in the preview.
  if (!restriction.factions.empty() && !player.faction.empty() && !has(restriction.factions,player.faction)) return false;
  if (!restriction.teams.empty() && std::find(restriction.teams.begin(),restriction.teams.end(),player.team)==restriction.teams.end()) return false;
 }
 return true;
}
int LivingWorldTerritorySelection::holder(const std::string &region) const
{
 const auto start=canonical(region);
 if (start.empty()) return -1;
 for (int i=0;i<8;++i) if (!m_starts[i].empty() && same(m_starts[i],start)) return i;
 return -1;
}
bool LivingWorldTerritorySelection::pick(const std::string &region,bool left)
{
 if (!allowed(region)) return false;
 const std::string start=canonical(region);
 const int owner=holder(start);
 if (owner>=0 && !m_players[(size_t)owner].selectable) return false;
 int next=-1;
 if (owner<0 || left) for (int i=owner+1;i<8;++i) {
  if (m_players[i].occupied && m_players[i].selectable && m_starts[i].empty() && allowed(region,i)) {next=i;break;}
 }
 if (owner>=0) m_starts[(size_t)owner].clear();
 if (next>=0) m_starts[(size_t)next]=start;
 return owner>=0 || next>=0;
}
