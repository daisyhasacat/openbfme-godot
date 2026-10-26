// OpenBFME. GPL-3.0.
// See GameClient/LivingWorldMap.h (lane WOTR-1).

#include "GameClient/LivingWorldMap.h"

#include "Common/AsciiString.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "GameLogic/ContainParseHooks.h"

#include <cstddef>

thread_local LivingWorldMapStore *TheLivingWorldMapStore = nullptr;

namespace
{
// the fields RotWK's data does not use: a load that meets one fails with its name (stop S-5303)
void parseNotPorted(INI *, void *, void *, const void *userData)
{
	throw INIException(8, "Living World field '%s' is not ported (stop S-5303)", static_cast<const char *>(userData));
}

// RW 0x73B217: an audio event of this world; "NoSound" clears, an unknown name throws "Invalid Sound" (as LinearCampaign's LoadScreenMusicTrack)
void parseAudioEvent(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	if (AsciiStringUtil::compareNoCase(name, "NoSound") == 0)
	{
		static_cast<std::string *>(store)->clear();
		return;
	}
	const ContainParseHooks &hooks = TheContainParseHooks();
	if (!hooks.audioEventExists)
	{
		throw INIException(8, "audio event '%s' cannot be resolved: no TheAudio lookup is installed", name.c_str());
	}
	if (!hooks.audioEventExists(name))
	{
		throw INIException(3, "Invalid Sound '%s'", name.c_str());
	}
	*static_cast<std::string *>(store) = name;
}

// RW 0x42F2D2: a Coord2D appended
void parseCoord2DAppend(INI *ini, void *, void *store, const void *)
{
	Coord2D c;
	INI::parseCoord2D(ini, nullptr, &c, nullptr);
	static_cast<std::vector<Coord2D> *>(store)->push_back(c);
}

// RW 0x42EE37: a real in seconds, times 1000 (RW 0xBD4388), truncated
void parseSecondsToMillisInt(INI *ini, void *, void *store, const void *)
{
	const float seconds = ini->scanReal(ini->getNextToken());
	*static_cast<int *>(store) = (int)(seconds * 1000.0f);
}

// RW 0x5DE588: an Eva event name (INFERENCE S-5303: kept unchecked)
void parseEvaEventName(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken();
}

template <class T, const FieldParse *Table>
void parseAppended(INI *ini, void *, void *store, const void *)
{
	std::vector<T> &list = *static_cast<std::vector<T> *>(store);
	list.emplace_back();
	ini->initFromINI(&list.back(), Table);
}

// ---- LivingWorldMapInfo, RW 0xBFA898 -------------------------------------------------------------------------------------------------------------------------------
#define MI(field) (int)offsetof(LivingWorldMapInfo, field)
const FieldParse kMapInfoParse[] = {
	{ "MapObject", INI::parseAsciiString, nullptr, MI(mapObject) },
	{ "NumWorldTiles", INI::parseInt, nullptr, MI(numWorldTiles) },
	{ "CloudBorderSubObject", INI::parseAsciiString, nullptr, MI(cloudBorderSubObject) },
	{ "TextLayerSubObject", INI::parseAsciiString, nullptr, MI(textLayerSubObject) },
	{ "Center", INI::parseCoord2D, nullptr, MI(center) },
	{ "Extent", INI::parseCoord2D, nullptr, MI(extent) },
	{ "AptCenter", INI::parseCoord2D, nullptr, MI(aptCenter) },
	{ "AptZoom", INI::parseReal, nullptr, MI(aptZoom) },
	{ "AptPitch", INI::parseReal, nullptr, MI(aptPitch) },
	{ "EmbersPartSys", INI::parseAsciiString, nullptr, MI(embersPartSys) },
	{ "CloudPos", INI::parseCoord3D, nullptr, MI(cloudPos) },
	{ "Ambient", INI::parseRGBColor, nullptr, MI(ambient) },
	{ "SunDir", INI::parseCoord2D, nullptr, MI(sunDir) },
	{ "SunRGB", INI::parseRGBColor, nullptr, MI(sunRGB) },
	{ "Accent1Dir", INI::parseCoord2D, nullptr, MI(accent1Dir) },
	{ "Accent1RGB", INI::parseRGBColor, nullptr, MI(accent1RGB) },
	{ "Accent2Dir", INI::parseCoord2D, nullptr, MI(accent2Dir) },
	{ "Accent2RGB", INI::parseRGBColor, nullptr, MI(accent2RGB) },
	{ "MenBanner", INI::parseAsciiString, nullptr, MI(menBanner) },
	{ "ElvesBanner", INI::parseAsciiString, nullptr, MI(elvesBanner) },
	{ "DwarvesBanner", INI::parseAsciiString, nullptr, MI(dwarvesBanner) },
	{ "IsengardBanner", INI::parseAsciiString, nullptr, MI(isengardBanner) },
	{ "MordorBanner", INI::parseAsciiString, nullptr, MI(mordorBanner) },
	{ "WildBanner", INI::parseAsciiString, nullptr, MI(wildBanner) },
	{ "NeutralBanner", INI::parseAsciiString, nullptr, MI(neutralBanner) },
	{ "AngmarBanner", INI::parseAsciiString, nullptr, MI(angmarBanner) },
	{ "MenAnts", INI::parseAsciiString, nullptr, MI(menAnts) },
	{ "ElvesAnts", INI::parseAsciiString, nullptr, MI(elvesAnts) },
	{ "DwarvesAnts", INI::parseAsciiString, nullptr, MI(dwarvesAnts) },
	{ "IsengardAnts", INI::parseAsciiString, nullptr, MI(isengardAnts) },
	{ "MordorAnts", INI::parseAsciiString, nullptr, MI(mordorAnts) },
	{ "WildAnts", INI::parseAsciiString, nullptr, MI(wildAnts) },
	{ "NeutralAnts", INI::parseAsciiString, nullptr, MI(neutralAnts) },
	{ "AngmarAnts", INI::parseAsciiString, nullptr, MI(angmarAnts) },
	{ "BattleMarker", INI::parseAsciiString, nullptr, MI(battleMarker) },
	{ "PalantirMarker", INI::parseAsciiString, nullptr, MI(palantirMarker) },
	{ "RegionAwardDisputeMarker", INI::parseAsciiString, nullptr, MI(regionAwardDisputeMarker) },
	{ "BattleMarkerCreatedSound", parseAudioEvent, nullptr, MI(battleMarkerCreatedSound) },
	{ "EnterMapSound", parseAudioEvent, nullptr, MI(enterMapSound) },
	{ "BannerScaleSpeed", INI::parseReal, nullptr, MI(bannerScaleSpeed) },
	{ "BannerMaxScale", INI::parseReal, nullptr, MI(bannerMaxScale) },
	{ "BannerTiltAngle", INI::parseReal, nullptr, MI(bannerTiltAngle) },
	{ "BannerHeight", INI::parseReal, nullptr, MI(bannerHeight) },
	{ "ArmyHeight", INI::parseReal, nullptr, MI(armyHeight) },
	{ "BeaconHeight", INI::parseReal, nullptr, MI(beaconHeight) },
	{ "HeroArmyIconDiameter", INI::parseReal, nullptr, MI(heroArmyIconDiameter) },
	{ "NumPointsPerArmyLine", INI::parseUnsignedInt, nullptr, MI(numPointsPerArmyLine) },
	{ "ArmyLineHeightBias", INI::parseReal, nullptr, MI(armyLineHeightBias) },
	{ "ArmyLineWidth", INI::parseReal, nullptr, MI(armyLineWidth) },
	{ "ArmyLineColorAttacking", INI::parseRGBColor, nullptr, MI(armyLineColorAttacking) },
	{ "ArmyLineColorNeutral", INI::parseRGBColor, nullptr, MI(armyLineColorNeutral) },
	{ "ArmyLineColorAllied", INI::parseRGBColor, nullptr, MI(armyLineColorAllied) },
	{ "ArmyLineTextureName", INI::parseAsciiString, nullptr, MI(armyLineTextureName) },
	{ "AnimRays", INI::parseAsciiString, nullptr, MI(animRays) },
	{ "AnimRaysPartSys", INI::parseAsciiString, nullptr, MI(animRaysPartSys) },
	{ "AnimRaysPartSysOffset", INI::parseCoord3D, nullptr, MI(animRaysPartSysOffset) },
	{ "AnimRaysColor", INI::parseRGBColor, nullptr, MI(animRaysColor) },
	{ "AnimRaysColorScale", INI::parseReal, nullptr, MI(animRaysColorScale) },
	{ "AnimRaysEffectShells", INI::parseInt, nullptr, MI(animRaysEffectShells) },
	{ "AnimRaysEffectDiameter", INI::parseInt, nullptr, MI(animRaysEffectDiameter) },
	{ "AnimRaysEffectLifetime", INI::parseInt, nullptr, MI(animRaysEffectLifetime) },
	{ "AnimRaysCreateSound", parseAudioEvent, nullptr, MI(animRaysCreateSound) },
	{ "AnimCloud", INI::parseAsciiString, nullptr, MI(animCloud) },
	{ "AnimCloudPartSys", INI::parseAsciiString, nullptr, MI(animCloudPartSys) },
	{ "NumAnimClouds", INI::parseInt, nullptr, MI(numAnimClouds) },
	{ "AnimCloudLifetime", INI::parseInt, nullptr, MI(animCloudLifetime) },
	{ "AnimCloudRegionMin", INI::parseCoord3D, nullptr, MI(animCloudRegionMin) },
	{ "AnimCloudRegionMax", INI::parseCoord3D, nullptr, MI(animCloudRegionMax) },
	{ "MordorCloud", INI::parseAsciiString, nullptr, MI(mordorCloud) },
	{ "CloudGrowthPos", INI::parseCoord3D, nullptr, MI(cloudGrowthPos) },
	{ "CloudGrowthRate", INI::parseInt, nullptr, MI(cloudGrowthRate) },
	{ "CloudInitialSize", INI::parseReal, nullptr, MI(cloudInitialSize) },
	{ "CloudGrowthSize", INI::parseReal, nullptr, MI(cloudGrowthSize) },
	{ "CloudInitialOpacity", INI::parseReal, nullptr, MI(cloudInitialOpacity) },
	{ "CameraBoundX", INI::parseReal, nullptr, MI(cameraBoundX) },
	{ "CameraBoundY", INI::parseReal, nullptr, MI(cameraBoundY) },
	{ "ClickScrollThreshold", INI::parseInt, nullptr, MI(clickScrollThreshold) },
	{ "MouseWheelZoomPerTick", INI::parseReal, nullptr, MI(mouseWheelZoomPerTick) },
	{ "MouseWheelZoomDampenFactor", INI::parseReal, nullptr, MI(mouseWheelZoomDampenFactor) },
	{ "AutoScrollSpeed", INI::parseReal, nullptr, MI(autoScrollSpeed) },
	{ "MaxAutoScrollTime", INI::parseReal, nullptr, MI(maxAutoScrollTime) },
	{ "ShadowColor", INI::parseRGBAColorInt, nullptr, MI(shadowColor) },
	{ "EnableMapShadows", INI::parseBool, nullptr, MI(enableMapShadows) },
	{ "AddShadowSubObject", INI::parseAsciiStringVectorAppend, nullptr, MI(addShadowSubObjects) },
	{ "DefaultArmyMoveSpeed", INI::parseReal, nullptr, MI(defaultArmyMoveSpeed) },
	{ "ArmySelectedFadeInStart", INI::parseInt, nullptr, MI(armySelectedFadeInStart) },
	{ "ArmySelectedFadeInEnd", INI::parseInt, nullptr, MI(armySelectedFadeInEnd) },
	{ "ArmySelectedFadeOutStart", INI::parseInt, nullptr, MI(armySelectedFadeOutStart) },
	{ "ArmySelectedFadeOutEnd", INI::parseInt, nullptr, MI(armySelectedFadeOutEnd) },
	{ "ArmyHilightedFadeInTime", INI::parseInt, nullptr, MI(armyHilightedFadeInTime) },
	{ "ArmyHilightedFadeOutTime", INI::parseInt, nullptr, MI(armyHilightedFadeOutTime) },
	{ "EyeTower", parseNotPorted, "EyeTower", 0 },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef MI

// ---- LivingWorldRegionEffects --------------------------------------------------------------------------------------------------------------------------------------
const FieldParse kIntensityPointParse[] = { // RW 0xC7AB18
	{ "Intensity", INI::parseReal, nullptr, (int)offsetof(LivingWorldRegionEffect::ControlPoint, intensity) },
	{ "Time", parseSecondsToMillisInt, nullptr, (int)offsetof(LivingWorldRegionEffect::ControlPoint, timeMs) },
	{ "EaseIn", INI::parseReal, nullptr, (int)offsetof(LivingWorldRegionEffect::ControlPoint, easeIn) },
	{ "EaseOut", INI::parseReal, nullptr, (int)offsetof(LivingWorldRegionEffect::ControlPoint, easeOut) },
	{ nullptr, nullptr, nullptr, 0 }
};
const FieldParse kEffectParse[] = { // RW 0xC78E18
	{ "Opacity", INI::parseReal, nullptr, (int)offsetof(LivingWorldRegionEffect, opacity) },
	{ "ColorIntensityControlPoint", parseAppended<LivingWorldRegionEffect::ControlPoint, kIntensityPointParse>, nullptr,
		(int)offsetof(LivingWorldRegionEffect, intensityPoints) },
	{ "ControlPoint", parseNotPorted, "ControlPoint", 0 },
	{ "Geometry", INI::parseAsciiStringVectorAppend, nullptr, (int)offsetof(LivingWorldRegionEffect, geometry) },
	{ "LoadInShell", INI::parseBool, nullptr, (int)offsetof(LivingWorldRegionEffect, loadInShell) },
	{ nullptr, nullptr, nullptr, 0 }
};
// an effect block: the userData names it (the globals RW 0xDE88A4 .. 0xDE88C4)
void parseEffect(INI *ini, void *, void *store, const void *userData)
{
	auto &effects = *static_cast<std::map<std::string, LivingWorldRegionEffect> *>(store);
	LivingWorldRegionEffect &e = effects[static_cast<const char *>(userData)];
	e = LivingWorldRegionEffect();
	e.defined = true;
	ini->initFromINI(&e, kEffectParse);
}
#define RE(field) (int)offsetof(LivingWorldRegionEffects, field)
const FieldParse kRegionEffectsParse[] = { // RW 0xDA6648, then the effect names
	{ "RegionObject", INI::parseAsciiString, nullptr, RE(regionObject) },
	{ "NeutralRegionColor", INI::parseRGBColor, nullptr, RE(neutralRegionColor) },
	{ "RegionBorderColor", INI::parseRGBColor, nullptr, RE(regionBorderColor) },
	{ "ShellStartPositionColor", INI::parseRGBColor, nullptr, RE(shellStartPositionColor) },
	{ "ShouldShowRegionBorders", INI::parseBool, nullptr, RE(shouldShowRegionBorders) },
	{ "BordersEffect", parseEffect, "BordersEffect", RE(effects) },
	{ "MouseoverEffectFlareup", parseEffect, "MouseoverEffectFlareup", RE(effects) },
	{ "MouseoutEffectFlareup", parseEffect, "MouseoutEffectFlareup", RE(effects) },
	{ "ConqueredEffectEvenglow", parseEffect, "ConqueredEffectEvenglow", RE(effects) },
	{ "ConqueredEffectFlareup", parseEffect, "ConqueredEffectFlareup", RE(effects) },
	{ "FilledOwnershipEffect", parseEffect, "FilledOwnershipEffect", RE(effects) },
	{ "HomeRegionHighlight", parseEffect, "HomeRegionHighlight", RE(effects) },
	{ "RegionSelectionEffect", parseEffect, "RegionSelectionEffect", RE(effects) },
	{ "UnifiedEffect", parseEffect, "UnifiedEffect", RE(effects) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef RE

// ---- Region ---------------------------------------------------------------------------------------------------------------------------------------------------------
const FieldParse kConnectionParse[] = { // RW 0xC4D46C
	{ "Region", INI::parseAsciiString, nullptr, (int)offsetof(LivingWorldRegion::Connection, region) },
	{ "DetourPoint", parseCoord2DAppend, nullptr, (int)offsetof(LivingWorldRegion::Connection, detourPoints) },
	{ nullptr, nullptr, nullptr, 0 }
};
// RW 0x7F4A9D: every further token a connection to that region
void parseConnectsTo(INI *ini, void *, void *store, const void *)
{
	auto &connections = *static_cast<std::vector<LivingWorldRegion::Connection> *>(store);
	for (const char *token = ini->getNextTokenOrNull(); token; token = ini->getNextTokenOrNull())
	{
		LivingWorldRegion::Connection c;
		c.region = token;
		connections.push_back(c);
	}
}
// RW 0x73B6D5 with the names RW 0xC1A690: every token an index (INI::scanIndexList)
void parseBuildingKinds(INI *ini, void *, void *store, const void *)
{
	static const char *const kNames[] = { "NONE", "FORTRESS", "ARMORY", "RESOURCE", "BARRACKS", nullptr };
	auto &out = *static_cast<std::vector<int> *>(store);
	for (const char *token = ini->getNextTokenOrNull(); token; token = ini->getNextTokenOrNull())
	{
		out.push_back(INI::scanIndexList(token, kNames));
	}
}
const FieldParse kRestrictBuildingsParse[] = { // RW 0xC4D378
	{ "Buildings", parseBuildingKinds, nullptr, (int)offsetof(LivingWorldRegion::BuildingRestriction, buildings) },
	{ "NumberAllowed", INI::parseInt, nullptr, (int)offsetof(LivingWorldRegion::BuildingRestriction, numberAllowed) },
	{ nullptr, nullptr, nullptr, 0 }
};
#define RG(field) (int)offsetof(LivingWorldRegion, field)
const FieldParse kRegionParse[] = { // RW 0xC4C8F0
	{ "MapName", INI::parseAsciiString, nullptr, RG(mapName) },
	{ "MapMatchPlayerWorldToMap", parseNotPorted, "MapMatchPlayerWorldToMap", 0 },
	{ "DelayCarryoverSpawningOf", INI::parseAsciiStringVectorAppend, nullptr, RG(delayCarryoverSpawningOf) },
	{ "DisplayName", INI::parseAsciiString, nullptr, RG(displayName) },
	{ "DisplayDescription", INI::parseAsciiString, nullptr, RG(displayDescription) },
	{ "ConqueredNotice", INI::parseAsciiString, nullptr, RG(conqueredNotice) },
	{ "MovieNameFirstTime", INI::parseAsciiString, nullptr, RG(movieNameFirstTime) },
	{ "MovieNameRepeat", INI::parseAsciiString, nullptr, RG(movieNameRepeat) },
	{ "SkirmishStillImage", INI::parseAsciiString, nullptr, RG(skirmishStillImage) },
	{ "SkirmishVoiceTrack", parseAudioEvent, nullptr, RG(skirmishVoiceTrack) },
	{ "SkirmishMusicTrack", parseAudioEvent, nullptr, RG(skirmishMusicTrack) },
	{ "SubObject", INI::parseAsciiString, nullptr, RG(subObject) },
	{ "StrategicValue", INI::parseInt, nullptr, RG(strategicValue) },
	{ "RegionBonus", parseNotPorted, "RegionBonus", 0 },
	{ "ConnectsTo", parseConnectsTo, nullptr, RG(connections) },
	{ "Connection", parseAppended<LivingWorldRegion::Connection, kConnectionParse>, nullptr, RG(connections) },
	{ "OnWinActSubroutine", INI::parseAsciiString, nullptr, RG(onWinActSubroutine) },
	{ "RegionPortrait", INI::parseAsciiString, nullptr, RG(regionPortrait) },
	{ "ArmyPlacementPos", parseCoord2DAppend, nullptr, RG(armyPlacementPos) },
	{ "UnpackCamps", INI::parseBool, nullptr, RG(unpackCamps) },
	{ "CustomCenterPoint", INI::parseBool, nullptr, RG(customCenterPoint) },
	{ "CenterPoint", INI::parseCoord2D, nullptr, RG(centerPoint) },
	{ "ArmyBonus", INI::parseInt, nullptr, RG(armyBonus) },
	{ "LegendaryBonus", INI::parseInt, nullptr, RG(legendaryBonus) },
	{ "ResourceBonus", INI::parseInt, nullptr, RG(resourceBonus) },
	{ "AttackBonus", INI::parseInt, nullptr, RG(attackBonus) },
	{ "DefenseBonus", INI::parseInt, nullptr, RG(defenseBonus) },
	{ "ExperienceBonus", INI::parseInt, nullptr, RG(experienceBonus) },
	{ "FertileTerritoryBonus", INI::parseInt, nullptr, RG(fertileTerritoryBonus) },
	{ "EndOfCampaign", INI::parseBool, nullptr, RG(endOfCampaign) },
	{ "MapIsCampaignMap", INI::parseBool, nullptr, RG(mapIsCampaignMap) },
	{ "LoseCampaignIfLoseMap", INI::parseBool, nullptr, RG(loseCampaignIfLoseMap) },
	{ "DisplayActNum", INI::parseInt, nullptr, RG(displayActNum) },
	{ "CustomUIPopupPoint", INI::parseBool, nullptr, RG(customUIPopupPoint) },
	{ "UIPopupPoint", INI::parseCoord2D, nullptr, RG(uiPopupPoint) },
	{ "DifficultyText", INI::parseAsciiString, nullptr, RG(difficultyText) },
	{ "OnEnterGiveUpgrade", parseNotPorted, "OnEnterGiveUpgrade", 0 },
	{ "OnVictoryGiveUpgrade", parseNotPorted, "OnVictoryGiveUpgrade", 0 },
	{ "OnVictoryRemoveUpgrade", parseNotPorted, "OnVictoryRemoveUpgrade", 0 },
	{ "OwnedByPlayer", INI::parseAsciiString, nullptr, RG(ownedByPlayer) },
	{ "HeroArmySpot", parseCoord2DAppend, nullptr, RG(heroArmySpots) },
	{ "GarrisonArmySpot", parseCoord2DAppend, nullptr, RG(garrisonArmySpots) },
	{ "BuildingSpot", parseCoord2DAppend, nullptr, RG(buildingSpots) },
	{ "CPLimit", INI::parseInt, nullptr, RG(cpLimit) },
	{ "AllyCPLimit", INI::parseInt, nullptr, RG(allyCPLimit) },
	{ "RestrictBuildings", parseAppended<LivingWorldRegion::BuildingRestriction, kRestrictBuildingsParse>, nullptr, RG(restrictBuildings) },
	{ "CreateAutoFort", INI::parseBool, nullptr, RG(createAutoFort) },
	{ "FortressPortrait", INI::parseAsciiString, nullptr, RG(fortressPortrait) },
	{ "FortressDisplayName", INI::parseAsciiString, nullptr, RG(fortressDisplayName) },
	{ "FortressDisplayDescription", INI::parseAsciiString, nullptr, RG(fortressDisplayDescription) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef RG

// RW 0x6103E2: the region's name token, the region, its fields, appended
void parseRegion(INI *ini, void *, void *store, const void *)
{
	auto &regions = *static_cast<std::vector<LivingWorldRegion> *>(store);
	LivingWorldRegion r;
	r.name = ini->getNextToken();
	regions.push_back(r);
	ini->initFromINI(&regions.back(), kRegionParse);
}

#define CB(field) (int)offsetof(LivingWorldConcurrentRegionBonus, field)
const FieldParse kConcurrentBonusParse[] = { // RW 0xBFA6B0
	{ "Territory", INI::parseAsciiString, nullptr, CB(territory) },
	{ "EffectName", INI::parseAsciiString, nullptr, CB(effectName) },
	{ "Regions", INI::parseAsciiStringVector, nullptr, CB(regions) },
	{ "LegendaryBonus", INI::parseInt, nullptr, CB(legendaryBonus) },
	{ "ResourceBonus", INI::parseInt, nullptr, CB(resourceBonus) },
	{ "ArmyBonus", INI::parseInt, nullptr, CB(armyBonus) },
	{ "AttackBonus", INI::parseInt, nullptr, CB(attackBonus) },
	{ "DefenseBonus", INI::parseInt, nullptr, CB(defenseBonus) },
	{ "ExperienceBonus", INI::parseInt, nullptr, CB(experienceBonus) },
	{ "UnifiedEvaEvent", parseEvaEventName, nullptr, CB(unifiedEvaEvent) },
	{ "LostEvaEvent", parseEvaEventName, nullptr, CB(lostEvaEvent) },
	{ "LookAtCenter", INI::parseCoord2D, nullptr, CB(lookAtCenter) },
	{ "LookAtHeading", INI::parseReal, nullptr, CB(lookAtHeading) },
	{ "LookAtZoom", INI::parseReal, nullptr, CB(lookAtZoom) },
	{ "ExtraStartResourcesBonus", INI::parseInt, nullptr, CB(extraStartResourcesBonus) },
	{ "BuildingDiscountBonus", INI::parseInt, nullptr, CB(buildingDiscountBonus) },
	{ "FreeInnUnitsBonus", INI::parseInt, nullptr, CB(freeInnUnitsBonus) },
	{ "DiscountedSeigeUnitsBonus", INI::parseInt, nullptr, CB(discountedSeigeUnitsBonus) },
	{ "DiscountedHeroUnitsBonus", INI::parseInt, nullptr, CB(discountedHeroUnitsBonus) },
	{ "FreeBuilderBonus", INI::parseInt, nullptr, CB(freeBuilderBonus) },
	{ "DiscountedBarracksUnitsBonus", INI::parseInt, nullptr, CB(discountedBarracksUnitsBonus) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef CB

#define RC(field) (int)offsetof(LivingWorldRegionCampaign, field)
const FieldParse kRegionCampaignParse[] = { // RW 0xBFA2D0
	{ "Region", parseRegion, nullptr, RC(regions) },
	{ "RegionBonusArmy", INI::parseAsciiString, nullptr, RC(regionBonusArmy) },
	{ "RegionBonusResource", INI::parseAsciiString, nullptr, RC(regionBonusResource) },
	{ "RegionBonusLegendary", INI::parseAsciiString, nullptr, RC(regionBonusLegendary) },
	{ "HeroOnlyArmyCommandPoints", INI::parseInt, nullptr, RC(heroOnlyArmyCommandPoints) },
	{ "SmallArmyCommandPoints", INI::parseInt, nullptr, RC(smallArmyCommandPoints) },
	{ "MediumArmyCommandPoints", INI::parseInt, nullptr, RC(mediumArmyCommandPoints) },
	{ "ArmyPlacementPos", parseCoord2DAppend, nullptr, RC(armyPlacementPos) },
	{ "ArmyRetreatRounds", INI::parseInt, nullptr, RC(armyRetreatRounds) },
	{ "ArmyCarryoverPoints", INI::parseInt, nullptr, RC(armyCarryoverPoints) },
	{ "RegionConqueredSound", INI::parseAsciiString, nullptr, RC(regionConqueredSound) },
	{ "RegionEffectsManagerName", INI::parseAsciiString, nullptr, RC(regionEffectsManagerName) },
	{ "ConcurrentRegionBonus", parseAppended<LivingWorldConcurrentRegionBonus, kConcurrentBonusParse>, nullptr, RC(concurrentRegionBonuses) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef RC

LivingWorldMapStore &store()
{
	if (!TheLivingWorldMapStore)
	{
		throw INIException(8, "Living World block: TheLivingWorldMapStore is not installed");
	}
	return *TheLivingWorldMapStore;
}
} // namespace

const std::vector<std::string> &LivingWorldMapStore::blockKeywords()
{
	static const std::vector<std::string> names = { "LivingWorldMapInfo", "LivingWorldRegionEffects", "LivingWorldRegionCampaign" };
	return names;
}

void LivingWorldMapStore::registerBlocks(INIBlockRegistry &registry)
{
	registry.registerBlock("LivingWorldMapInfo", [](INI *ini) { store().parseMapInfo(ini); });
	registry.registerBlock("LivingWorldRegionEffects", [](INI *ini) { store().parseRegionEffects(ini); });
	registry.registerBlock("LivingWorldRegionCampaign", [](INI *ini) { store().parseRegionCampaign(ini); });
}

void LivingWorldMapStore::parseMapInfo(INI *ini)
{
	// one map; a second block overwrites the fields it names (INFERENCE S-5303: retail's block parser for the single TheLivingWorldManager map info)
	m_mapInfo.defined = true;
	ini->initFromINI(&m_mapInfo, kMapInfoParse);
}

void LivingWorldMapStore::parseRegionEffects(INI *ini)
{
	LivingWorldRegionEffects e;
	e.name = ini->getNextToken();
	if (findRegionEffects(e.name)) throw INIException(8, "duplicate LivingWorldRegionEffects '%s' is not verified (stop S-5303)", e.name.c_str());
	m_regionEffects.push_back(e);
	ini->initFromINI(&m_regionEffects.back(), kRegionEffectsParse);
}

void LivingWorldMapStore::parseRegionCampaign(INI *ini)
{
	LivingWorldRegionCampaign c;
	c.name = ini->getNextToken();
	if (findRegionCampaign(c.name)) throw INIException(8, "duplicate LivingWorldRegionCampaign '%s' is not verified (stop S-5303)", c.name.c_str());
	m_regionCampaigns.push_back(c);
	ini->initFromINI(&m_regionCampaigns.back(), kRegionCampaignParse);
}

const LivingWorldRegionEffects *LivingWorldMapStore::findRegionEffects(const std::string &name) const
{
	for (const LivingWorldRegionEffects &e : m_regionEffects)
	{
		if (AsciiStringUtil::compareNoCase(e.name, name) == 0)
		{
			return &e;
		}
	}
	return nullptr;
}

const LivingWorldRegionCampaign *LivingWorldMapStore::findRegionCampaign(const std::string &name) const
{
	for (const LivingWorldRegionCampaign &c : m_regionCampaigns)
	{
		if (AsciiStringUtil::compareNoCase(c.name, name) == 0)
		{
			return &c;
		}
	}
	return nullptr;
}

std::vector<std::string> LivingWorldMapStore::stopLines()
{
	return {
		"[S-5303] Living World map data: EyeTower, the effect ControlPoint, Region's RegionBonus / MapMatchPlayerWorldToMap / OnEnterGiveUpgrade / "
		"OnVictoryGiveUpgrade / OnVictoryRemoveUpgrade are not ported (RotWK's data uses none; a file that does fails its load with the field's name); "
		"INFERENCE: the region effect blocks are dispatched by their names (the dispatching code was not read) and UnifiedEvaEvent / LostEvaEvent keep "
		"their names unchecked against TheEva; repeated MapInfo blocks overwrite named fields and omitted colour fields are zero-initialized "
		"pending constructor verification; duplicate named effects or region campaigns fail explicitly until overwrite semantics are verified",
	};
}
