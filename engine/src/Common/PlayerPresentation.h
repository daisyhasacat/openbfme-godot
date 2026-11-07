// OpenBFME. GPL-3.0. Presentation names, never simulation/player-index remapping.
#pragma once
#include <string>

namespace PlayerPresentation
{
inline std::string key(int id, const std::string &name, const std::string &faction)
{
    if (id == 0) return "NeutralFaction";
    if (name == "PlyrCreeps" || name == "CreepFaction" || faction == "Creeps") return "CreepFaction";
    return name.empty() ? "Player_" + std::to_string(id) : name;
}
inline std::string label(int id, const std::string &name, const std::string &faction)
{
    const auto value = key(id, name, faction);
    if (value == "NeutralFaction" || value == "CreepFaction" || faction.empty()) return value;
    return value + " (" + faction + ")";
}
}
