// OpenBFME. GPL-3.0. See MiscAudio.h.

#include "Common/Audio/MiscAudio.h"

#include "Common/AsciiString.h"
#include "Common/INIException.h"

#include <cstdint>
#include <deque>

namespace
{
struct ParseContext
{
	const AudioEventInfoStore *store;
	std::vector<std::string> *names;
	std::vector<std::string> *bfme1Names; // community PR #36: the slots past names' (BFME1 data only)
};
std::string &slotOf(ParseContext *ctx, size_t slot)
{
	return slot < ctx->names->size() ? (*ctx->names)[slot] : (*ctx->bfme1Names)[slot - ctx->names->size()];
}

// RW 0x73B217 -> 0x73AA94
void parseAudioEventName(INI *ini, void *instance, void *, const void *userData)
{
	ParseContext *ctx = static_cast<ParseContext *>(instance);
	const size_t slot = (size_t)(uintptr_t)userData;
	const std::string name = ini->getNextToken();
	if (AsciiStringUtil::compareNoCase(name, "NoSound") == 0)
	{
		slotOf(ctx, slot).clear();
		return;
	}
	if (!ctx->store->contains(name))
	{
		throw INIException(3, "Invalid Sound '%s'", name.c_str());
	}
	slotOf(ctx, slot) = name;
}
} // namespace

// RW table 0xBF5450 (token order of the binary).
const std::vector<std::string> &MiscAudio::fieldNames()
{
	static const std::vector<std::string> names = {
		"BattleCrySound", "CrateSalvage", "RadarNotifyHarvesterUnderAttackSound", "RadarNotifyStructureUnderAttackSound", "RadarNotifyInfiltrationSound", "RadarNotifyOnlineSound", "RadarNotifyOfflineSound", "GenericRadarEvent",
		"BeaconPlacedSound", "BeaconPlacementFailed", "DefectorTimerTickSound", "DefectorTimerDingSound", "AllCheerSound", "NoCanDoSound", "StealthDiscoveredSound", "StealthNeutralizedSound",
		"MoneyDepositSound", "MoneyWithdrawSound", "BuildingDisabled", "BuildingReenabled", "VehicleDisabled", "VehicleReenabled", "SplatterVehiclePilotsBrain", "CrateHeal", "CrateShroud",
		"CrateFreeUnit", "CrateMoney", "UnitPromoted", "RepairSparks", "EnterCloseCombat", "ExitCloseCombat", "IncomingChatNotification", "PrivateMessageNotification", "BuddyMessageNotification",
		"EnabledHotKeyPressed", "DisabledHotKeyPressed", "DisabledButtonClicked", "LowLODShellMusic", "HighLODShellMusic", "ScoreScreenMusic", "ShellMapLoadMusic", "FullScreenSubMenuMusic",
		"SaveFileLoadMusic", "CreditsMusic", "VolumeSampleMusic", "VolumeSampleSoundFX", "VolumeSampleVoice", "VolumeSampleAmbient", "VolumeSampleMovie", "MissionBriefingCharacterClick",
		"ComboBoxClick", "GameSpyCommunicatorOpen", "RIFThingTemplateReloadedSound", "RIFObjectsRefreshedSound", "FastForwardModeOn", "FastForwardModeOff", "RallyPointSet",
		"UnableToSetRallyPoint", "PlanningModeOrderGiven", "BuildingPlacementSound", "BadBuildingPlacementSound", "WallPlacementSound", "TargetObjectWithSpecialPowerSound"
	};
	return names;
}

const std::vector<std::string> &MiscAudio::bfme1FieldNames()
{
	static const std::vector<std::string> names = { "BattleCrySound", "CrateSalvage" }; // Open-BFME-1's MiscAudio rows that RotWK lacks
	return names;
}

const std::string &MiscAudio::get(const std::string &fieldName) const
{
	static const std::string empty;
	const std::vector<std::string> &n = fieldNames();
	for (size_t i = 0; i < n.size(); ++i)
	{
		if (n[i] == fieldName)
		{
			return m_names[i];
		}
	}
	const std::vector<std::string> &b = bfme1FieldNames();
	for (size_t i = 0; i < b.size(); ++i)
	{
		if (b[i] == fieldName)
		{
			return m_bfme1Names[i];
		}
	}
	return empty;
}

bool MiscAudio::has(const std::string &fieldName) const
{
	for (const std::string &n : fieldNames())
	{
		if (n == fieldName)
		{
			return true;
		}
	}
	for (const std::string &n : bfme1FieldNames())
	{
		if (n == fieldName)
		{
			return true;
		}
	}
	return false;
}

void MiscAudio::parse(INI *ini, const AudioEventInfoStore &store)
{
	const std::vector<std::string> &n = fieldNames();
	std::deque<FieldParse> rows;
	std::vector<FieldParse> table;
	for (size_t i = 0; i < n.size(); ++i)
	{
		table.push_back({ n[i].c_str(), parseAudioEventName, (const void *)(uintptr_t)i, 0 });
	}
	// community PR #36: BFME1's rows in BFME1 data only (their slots follow RotWK's: ParseContext writes m_bfme1Names for them)
	const std::vector<std::string> &b = bfme1FieldNames();
	if (ini->bfme1Data())
	{
		for (size_t i = 0; i < b.size(); ++i)
		{
			table.push_back({ b[i].c_str(), parseAudioEventName, (const void *)(uintptr_t)(n.size() + i), 0 });
		}
	}
	table.push_back({ nullptr, nullptr, nullptr, 0 });
	ParseContext ctx = { &store, &m_names, &m_bfme1Names };
	ini->initFromINI(&ctx, table.data());
}
