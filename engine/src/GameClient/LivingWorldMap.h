// OpenBFME. GPL-3.0.
//
// Lane WOTR-1: the Living World map data the War of the Ring lobby draws its territories from: LivingWorldMapInfo (Data\INI\LivingWorld.ini, TheLivingWorldManager),
// LivingWorldRegionEffects (Data\INI\LivingWorldRegionEffects.ini) and LivingWorldRegionCampaign with its Region blocks (Data\INI\Campaigns\RiskCampaign.ini
// including Common\LivingWorldRegions.inc).
//
// TARGET FACTS (RotWK game.dat, the field tables read from .rdata, rows {name, parser, user data, offset}):
//   * LivingWorldMapInfo, table RW 0xBFA898 (92 rows): MapObject, NumWorldTiles, CloudBorderSubObject, TextLayerSubObject, Center, Extent, AptCenter, AptZoom,
//     AptPitch (the lobby's camera), the lights (Ambient, SunDir, SunRGB, Accent1/2 Dir / RGB), the faction banners and ants, the army, cloud, camera and
//     scroll settings, ShadowColor (RGBA), AddShadowSubObject (append) and EyeTower (RW 0x7FB4D5, a sub-block: not ported, S-5303);
//   * LivingWorldRegionEffects <name>, table RW 0xDA6648: RegionObject (RW 0x7EFB84: the W3D object with the regions' pick boxes), NeutralRegionColor,
//     RegionBorderColor, ShellStartPositionColor, ShouldShowRegionBorders; the effect blocks BordersEffect, MouseoverEffectFlareup, MouseoutEffectFlareup,
//     ConqueredEffectEvenglow, ConqueredEffectFlareup, FilledOwnershipEffect, HomeRegionHighlight, RegionSelectionEffect, UnifiedEffect (their names are
//     the globals RW 0xDE88A4 .. 0xDE88C4, initialised at RW 0xBC4CCD ..), each with the table RW 0xC78E18 {Opacity, ColorIntensityControlPoint (table
//     RW 0xC7AB18 {Intensity, Time (RW 0x42EE37: seconds times 1000, truncated), EaseIn, EaseOut}), ControlPoint (not ported, S-5303), Geometry (a name
//     appended, RW 0x8E8577), LoadInShell}. INFERENCE (S-5303): the effect blocks are dispatched by those names; the dispatching code was not read;
//   * LivingWorldRegionCampaign <name>, table RW 0xBFA2D0: Region (RW 0x6103E2: the name token, a 0x1D4-byte region (ctor RW 0x7F4FE1), its table, appended),
//     RegionBonusArmy / Resource / Legendary, HeroOnly / Small / MediumArmyCommandPoints, ArmyPlacementPos (a Coord2D appended, RW 0x42F2D2),
//     ArmyRetreatRounds, ArmyCarryoverPoints, RegionConqueredSound, RegionEffectsManagerName, ConcurrentRegionBonus (RW 0x610873, table RW 0xBFA6B0);
//   * Region, table RW 0xC4C8F0 (50 rows): MapName, DisplayName, DisplayDescription, ConqueredNotice, the movies, SkirmishStillImage, SkirmishVoiceTrack /
//     SkirmishMusicTrack (audio events, RW 0x73B217), SubObject (the region's sub-object in the RegionObject / LivingMap models), StrategicValue,
//     ConnectsTo (RW 0x7F4A9D: each further token a connection to that region), Connection (RW 0x7F4B0E: a block {Region, DetourPoint}, table RW
//     0xC4D46C), RegionPortrait, ArmyPlacementPos, UnpackCamps, CustomCenterPoint, CenterPoint, the bonuses, the campaign-map flags, UIPopupPoint,
//     DifficultyText, OwnedByPlayer, HeroArmySpot / GarrisonArmySpot / BuildingSpot (Coord2D appended), CPLimit, AllyCPLimit, RestrictBuildings (RW
//     0x7F3BF3: table RW 0xC4D378 {Buildings: indexes into {NONE, FORTRESS, ARMORY, RESOURCE, BARRACKS} (RW 0xC1A690), NumberAllowed}),
//     CreateAutoFort, FortressPortrait / DisplayName / DisplayDescription.
// NOT PORTED (stop S-5303; a data file that uses one fails its load with the field's name): EyeTower, ControlPoint, Region's RegionBonus, MapMatchPlayerWorldToMap,
// OnEnterGiveUpgrade, OnVictoryGiveUpgrade, OnVictoryRemoveUpgrade. INFERENCE (S-5303): UnifiedEvaEvent / LostEvaEvent (RW 0x5DE588, TheEva's lookup RW
// 0x5DE0D8) keep the event's name unchecked.
#pragma once

#include "Common/INIDataTypes.h"

#include <map>
#include <string>
#include <vector>

class INI;
class INIBlockRegistry;

struct LivingWorldMapInfo
{
	bool defined = false;
	std::string mapObject;                 ///< +0x00 LivingMap
	int numWorldTiles = 0;                 ///< +0x04
	Coord2D center, extent;                ///< +0x08, +0x10
	Coord2D aptCenter;                     ///< +0x18: the lobby view's centre
	float aptZoom = 0.0f, aptPitch = 0.0f; ///< +0x20 (0 .. 1, 1 fully zoomed out), +0x24 (radians, 0 straight down)
	Coord3D cloudPos;
	std::string embersPartSys;
	RGBColor ambient{}, sunRGB{}, accent1RGB{}, accent2RGB{};
	Coord2D sunDir, accent1Dir, accent2Dir;
	std::string menBanner, elvesBanner, dwarvesBanner, isengardBanner, mordorBanner, wildBanner, neutralBanner, angmarBanner;
	std::string menAnts, elvesAnts, dwarvesAnts, isengardAnts, mordorAnts, wildAnts, neutralAnts, angmarAnts;
	std::string battleMarker, palantirMarker, regionAwardDisputeMarker;
	float bannerScaleSpeed = 0, bannerMaxScale = 0, bannerTiltAngle = 0, bannerHeight = 0, armyHeight = 0, beaconHeight = 0, defaultArmyMoveSpeed = 0,
		  heroArmyIconDiameter = 0;
	unsigned numPointsPerArmyLine = 0;
	float armyLineHeightBias = 0, armyLineWidth = 0;
	RGBColor armyLineColorAttacking{}, armyLineColorNeutral{}, armyLineColorAllied{};
	std::string armyLineTextureName;
	std::string animRays, animRaysPartSys;
	Coord3D animRaysPartSysOffset;
	RGBColor animRaysColor{};
	float animRaysColorScale = 0;
	int animRaysEffectShells = 0, animRaysEffectDiameter = 0, animRaysEffectLifetime = 0;
	std::string animRaysCreateSound, battleMarkerCreatedSound, enterMapSound;
	std::string animCloud, animCloudPartSys;
	int numAnimClouds = 0, animCloudLifetime = 0;
	Coord3D animCloudRegionMin, animCloudRegionMax;
	std::string mordorCloud, cloudBorderSubObject, textLayerSubObject;
	std::vector<std::string> addShadowSubObjects;
	Coord3D cloudGrowthPos;
	int cloudGrowthRate = 0;
	float cloudInitialSize = 0, cloudInitialOpacity = 0, cloudGrowthSize = 0;
	float cameraBoundX = 0, cameraBoundY = 0, mouseWheelZoomPerTick = 0, mouseWheelZoomDampenFactor = 0, autoScrollSpeed = 0, maxAutoScrollTime = 0;
	int clickScrollThreshold = 0;
	RGBAColorInt shadowColor{};
	int armySelectedFadeInStart = 0, armySelectedFadeInEnd = 0, armySelectedFadeOutStart = 0, armySelectedFadeOutEnd = 0, armyHilightedFadeInTime = 0,
		armyHilightedFadeOutTime = 0;
	bool enableMapShadows = false;
};

struct LivingWorldRegionEffect
{
	float opacity = 0.0f;
	struct ControlPoint
	{
		float intensity = 0, easeIn = 0, easeOut = 0;
		int timeMs = 0; ///< RW 0x42EE37: the seconds times 1000, truncated
	};
	std::vector<ControlPoint> intensityPoints;
	std::vector<std::string> geometry; ///< the W3D objects the effect draws (RW 0x8E8577)
	bool loadInShell = true;
	bool defined = false;
};

struct LivingWorldRegionEffects
{
	std::string name;
	std::string regionObject;             ///< + 0x1C: the W3D object with the regions' pick boxes
	RGBColor neutralRegionColor{}, regionBorderColor{}, shellStartPositionColor{};
	bool shouldShowRegionBorders = false;
	// BordersEffect, MouseoverEffectFlareup, MouseoutEffectFlareup, ConqueredEffectEvenglow, ConqueredEffectFlareup, FilledOwnershipEffect,
	// HomeRegionHighlight, RegionSelectionEffect, UnifiedEffect
	std::map<std::string, LivingWorldRegionEffect> effects;
};

struct LivingWorldRegion
{
	std::string name;                      ///< the block's token (Amon_Sul)
	std::string mapName, displayName, displayDescription, conqueredNotice, movieNameFirstTime, movieNameRepeat, skirmishStillImage;
	std::string skirmishVoiceTrack, skirmishMusicTrack;
	std::vector<std::string> delayCarryoverSpawningOf;
	std::string subObject;                 ///< + 0x54
	int strategicValue = 0;
	struct Connection
	{
		std::string region;
		std::vector<Coord2D> detourPoints;
	};
	std::vector<Connection> connections;   ///< + 0x60: ConnectsTo's names and the Connection blocks, in order
	std::string onWinActSubroutine, regionPortrait;
	std::vector<Coord2D> armyPlacementPos;
	bool unpackCamps = false, customCenterPoint = false;
	Coord2D centerPoint;                   ///< + 0x80
	int resourceBonus = 0, armyBonus = 0, legendaryBonus = 0, attackBonus = 0, defenseBonus = 0, experienceBonus = 0, fertileTerritoryBonus = 0;
	bool endOfCampaign = false, mapIsCampaignMap = false, loseCampaignIfLoseMap = false, customUIPopupPoint = false;
	int displayActNum = 0;
	Coord2D uiPopupPoint;
	std::string difficultyText, ownedByPlayer;
	std::vector<Coord2D> heroArmySpots, garrisonArmySpots, buildingSpots;
	int cpLimit = 0, allyCPLimit = 0;
	struct BuildingRestriction
	{
		std::vector<int> buildings; ///< indexes into NONE, FORTRESS, ARMORY, RESOURCE, BARRACKS
		int numberAllowed = 0;
	};
	std::vector<BuildingRestriction> restrictBuildings;
	bool createAutoFort = false;
	std::string fortressPortrait, fortressDisplayName, fortressDisplayDescription;
};

struct LivingWorldConcurrentRegionBonus
{
	std::string territory, effectName;
	std::vector<std::string> regions;
	int resourceBonus = 0, armyBonus = 0, legendaryBonus = 0, attackBonus = 0, defenseBonus = 0, experienceBonus = 0;
	int extraStartResourcesBonus = 0, buildingDiscountBonus = 0, freeInnUnitsBonus = 0, discountedSeigeUnitsBonus = 0, discountedHeroUnitsBonus = 0,
		freeBuilderBonus = 0, discountedBarracksUnitsBonus = 0;
	std::string unifiedEvaEvent, lostEvaEvent; ///< INFERENCE (S-5303): names, not checked against TheEva
	Coord2D lookAtCenter;
	float lookAtHeading = 0, lookAtZoom = 0;
};

struct LivingWorldRegionCampaign
{
	std::string name;
	std::vector<LivingWorldRegion> regions;
	std::string regionBonusArmy, regionBonusResource, regionBonusLegendary;
	int heroOnlyArmyCommandPoints = 0, smallArmyCommandPoints = 0, mediumArmyCommandPoints = 0, armyCarryoverPoints = 0, armyRetreatRounds = 0;
	std::vector<Coord2D> armyPlacementPos;
	std::string regionConqueredSound, regionEffectsManagerName;
	std::vector<LivingWorldConcurrentRegionBonus> concurrentRegionBonuses;
};

class LivingWorldMapStore
{
public:
	void registerBlocks(INIBlockRegistry &registry);
	static const std::vector<std::string> &blockKeywords(); ///< LivingWorldMapInfo, LivingWorldRegionEffects, LivingWorldRegionCampaign
	const LivingWorldMapInfo &mapInfo() const { return m_mapInfo; }
	const LivingWorldRegionEffects *findRegionEffects(const std::string &name) const; ///< case-insensitive
	const LivingWorldRegionCampaign *findRegionCampaign(const std::string &name) const;
	const std::vector<LivingWorldRegionCampaign> &regionCampaigns() const { return m_regionCampaigns; }
	static std::vector<std::string> stopLines();

	void parseMapInfo(INI *ini);
	void parseRegionEffects(INI *ini);
	void parseRegionCampaign(INI *ini);

private:
	LivingWorldMapInfo m_mapInfo;
	std::vector<LivingWorldRegionEffects> m_regionEffects;
	std::vector<LivingWorldRegionCampaign> m_regionCampaigns;
};

// the store the blocks fill (RetailObjectWorld installs its own); nullptr when none
extern thread_local LivingWorldMapStore *TheLivingWorldMapStore;
