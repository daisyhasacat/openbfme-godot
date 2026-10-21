// OpenBFME. GPL-3.0. Optional wall policy, real body damage and navigation topology.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "CombatTestUtil.h"
#include "GameLogic/WallBalance.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/WallSpan.h"
#include "Common/BuildAssistant.h"
#include "GameLogic/Module/ReplaceSelfUpgrade.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Construction.h"
#include "GameLogic/Combat/ObjectWeapons.h"
using namespace combattest;
TEST_CASE("accelerated wall construction skips stagger only for accelerated owners") {
 CombatWorld w(
 "Object FastWall\n KindOf = STRUCTURE IMMOBILE WALL_SEGMENT\n BuildTime = 30\n Body = ActiveBody Body\n MaxHealth = 100\n End\n Behavior = GettingBuiltBehavior Build\n SpawnTimer = 0\n End\nEnd\n");
 auto &production=w.logic->productionSettings();
 production.loaded=true; production.multipleFactory=1; production.multiPlayEntries=8;
 for(int i=0;i<8;++i) { production.multiPlayUnitSpeedMult[i]=1; production.multiPlayBuildingSpeedMult[i]=1; }
 const auto player=w.teamA()->getControllingPlayer();
 int calls=0;
 w.logic->setTimingHook("build",[&](const GameLogic::TimingRequest &r) {
  ++calls; return r.player==player ? 5 : r.frames;
 });
 auto *fast=w.unit("FastWall",'A',300,500);
 auto *normal=w.unit("FastWall",'B',600,500);
 for (Object *wall : {fast,normal}) {
  // Match WallSpan's finished-object bootstrap before starting the span.
  Construction::completeConstruction(*wall,nullptr);
  auto *body=dynamic_cast<ActiveBody *>(wall->getBodyModule());
  body->internalChangeHealth(1-body->getHealth());
  auto *build=dynamic_cast<GettingBuiltBehavior *>(wall->findModule("GettingBuiltBehavior"));
  REQUIRE(build);
  build->runNowThenSleep(200);
  CHECK(build->friend_getNextCallFrame()==w.logic->getFrame()+(wall==fast ? 0 : 200));
 }
 CHECK(calls==2);
 w.frames(7);
 CHECK_FALSE(fast->isUnderConstruction());
 CHECK(w.health(fast)==100);
 CHECK(w.health(normal)==1);
 CHECK(calls==2);
}
namespace {
const char objects[] =
"Object BalanceWall\n KindOf = STRUCTURE IMMOBILE WALL_SEGMENT SELECTABLE\n BuildCost = 200\n Geometry = BOX\n GeometryMajorRadius = 200\n GeometryMinorRadius = 5\n GeometryHeight = 20\n Body = ActiveBody Body\n MaxHealth = 100\n End\nEnd\n"
"Object BalanceSiege\n KindOf = CAN_ATTACK_WALLS SIEGEENGINE\n Body = ActiveBody Body\n MaxHealth = 100\n End\nEnd\n";
}
TEST_CASE("wall balance: damage restrictions, health and costs preserve native mode") {
 CombatWorld w(objects);
 auto *a=w.unit("Swordsman",'A',100,100);
 auto *native=w.unit("BalanceWall",'B',500,500);
 CHECK(native->getBodyModule()->getMaxHealth()==100);
 auto &r=w.logic->settings().wallBalance;r.enabled=true;r.wallDamage=1;r.health[0]=6;r.cost[0]=.5f;
 auto *wall=w.unit("BalanceWall",'B',500,700);
 CHECK(wall->getBodyModule()->getMaxHealth()==600);
 CHECK(BuildAssistant::calcCostToBuild(*wall->getTemplate(),a->getControllingPlayer(),a,-1)==100);
 CHECK_FALSE(WallBalance::attackerAllowed(*wall,*a));
 DamageInfo damage;damage.m_input.m_sourceID=a->getID();damage.m_input.m_amount=25;damage.m_input.m_damageType=DAMAGE_UNRESISTABLE;
 wall->attemptDamage(damage);CHECK(w.health(wall)==600);
 auto *siege=w.unit("BalanceSiege",'A',100,150);
 CHECK(WallBalance::attackerAllowed(*wall,*siege));
 damage.m_input.m_sourceID=siege->getID();wall->attemptDamage(damage);CHECK(w.health(wall)==575);
 r.enabled=false;CHECK(WallBalance::attackerAllowed(*wall,*a));
 damage.m_input.m_sourceID=a->getID();wall->attemptDamage(damage);CHECK(w.health(wall)==550);
 CHECK(BuildAssistant::calcCostToBuild(*wall->getTemplate(),a->getControllingPlayer(),a,-1)==200);
}
TEST_CASE("wall balance: bombardment ships damage walls and fortresses in both restricted modes") {
 CombatWorld w(
 "Object TestWall\n KindOf = STRUCTURE IMMOBILE WALL_SEGMENT\n Body = ActiveBody Body\n MaxHealth = 100\n End\nEnd\n"
 "Object TestFortress\n KindOf = STRUCTURE IMMOBILE COMMANDCENTER\n Body = ActiveBody Body\n MaxHealth = 100\n End\nEnd\n"
 "Object ModBombardShip\n KindOf = SHIP CAN_ATTACK\n ThreatBreakdown Naval\n AIKindOf = SHIP_BOMBARD\n End\n WeaponSet\n Conditions = None\n Weapon = PRIMARY SwordWeapon\n End\n Body = ActiveBody Body\n MaxHealth = 100\n End\nEnd\n"
 "Object ModWarShip\n KindOf = SHIP CAN_ATTACK\n ThreatBreakdown Naval\n AIKindOf = SHIP_BATTLESHIP\n End\n Body = ActiveBody Body\n MaxHealth = 100\n End\nEnd\n"
 "Object ModTransport\n KindOf = SHIP\n ThreatBreakdown Naval\n AIKindOf = SHIP_TRANSPORT\n End\n Body = ActiveBody Body\n MaxHealth = 100\n End\nEnd\n");
 auto *ship=w.unit("ModBombardShip",'A',100,100);
 auto *warship=w.unit("ModWarShip",'A',100,120);
 auto *transport=w.unit("ModTransport",'A',100,140);
 auto *infantry=w.unit("Swordsman",'A',100,160);
 auto &rules=w.logic->settings().wallBalance; rules.enabled=true;
 for(int mode : {1,2}) {
  rules.wallDamage=rules.fortressDamage=mode;
  for(const char *name : {"TestWall","TestFortress"}) {
   auto *target=w.unit(name,'B',150,150);
   CHECK(WallBalance::attackerAllowed(*target,*ship));
   CHECK_FALSE(WallBalance::attackerAllowed(*target,*warship));
   CHECK_FALSE(WallBalance::attackerAllowed(*target,*transport));
   CHECK_FALSE(WallBalance::attackerAllowed(*target,*infantry));
   REQUIRE(ship->getWeapons());
   CHECK(ship->getWeapons()->canAttackObject(*target,CMD_FROM_SCRIPT,false));
   auto *weapon=ship->getWeapons()->weaponInSlot(PRIMARY_WEAPON);
   REQUIRE(weapon);
   CHECK(ship->getWeapons()->canDamage(*weapon,*target));
   DamageInfo damage;damage.m_input.m_sourceID=ship->getID();damage.m_input.m_amount=100;damage.m_input.m_damageType=DAMAGE_UNRESISTABLE;
   target->attemptDamage(damage);
   CHECK(w.health(target)==0);
   CHECK(target->isEffectivelyDead());
  }
 }
}
TEST_CASE("wall balance: a closed wall region is enclosed, a breach opens it") {
 CombatWorld w(objects);
 auto &r=w.logic->settings().wallBalance;r.enabled=true;
 auto *top=w.unit("BalanceWall",'A',500,700);
 w.unit("BalanceWall",'A',500,300);
 auto *left=w.unit("BalanceWall",'A',300,500);left->setOrientation(1.57079632679f);
 auto *right=w.unit("BalanceWall",'A',700,500);right->setOrientation(1.57079632679f);
 const Coord3D center{500,500,0};
 CHECK(WallBalance::enclosed(*w.logic,*top->getControllingPlayer(),center));
 CHECK_FALSE(WallBalance::enclosed(*w.logic,*top->getControllingPlayer(),{100,100,0}));
 int width=0,height=0;
 r.enabled=false; // paving is client decoration, independent of gameplay wall restrictions
 const auto stateBeforePaving=w.logic->computeStateHash();
 const auto paved=WallBalance::pavingMask(*w.logic,width,height);
 CHECK(w.logic->computeStateHash()==stateBeforePaving);
 // tolerance: the probe at cell (50,50) must fit within the fixture terrain, whose extent is not the behavior under test.
 REQUIRE(width>50);REQUIRE(height>50);
 CHECK(paved[50*width+50]==255);
 CHECK(paved[10*width+10]==0);
 w.logic->destroyObject(top);
 w.logic->runLogicFrame();
 CHECK_FALSE(WallBalance::enclosed(*w.logic,*left->getControllingPlayer(),center));
 const auto breached=WallBalance::pavingMask(*w.logic,width,height);
 // tolerance: ensure the cell-50 probe is in bounds before indexing the returned terrain mask.
 REQUIRE(width>50);CHECK(breached[50*width+50]==0);
 w.logic->destroyObject(left);w.logic->destroyObject(right);
 for(Object *o=w.logic->getFirstObject();o;o=o->getNextObject())if(WallBalance::wall(*o))w.logic->destroyObject(o);
 w.logic->runLogicFrame();
 CHECK(WallBalance::pavingMask(*w.logic,width,height).empty());
}

TEST_CASE("wall replacement clears side tiles only for gates") {
 const char gateObjects[] =
 "Object GateTestSegment\n KindOf = STRUCTURE IMMOBILE WALL_SEGMENT\n Geometry = BOX\n GeometryMajorRadius = 5\n GeometryMinorRadius = 10\n GeometryHeight = 20\n Body = ActiveBody Body\n MaxHealth = 100\n End\nEnd\n"
 "Object GateTestGate\n KindOf = STRUCTURE IMMOBILE WALL_UPGRADE\n Behavior = GateOpenAndCloseBehavior Gate\n End\n Geometry = BOX\n GeometryMajorRadius = 5\n GeometryMinorRadius = 30\n GeometryHeight = 20\n Body = ActiveBody Body\n MaxHealth = 100\n End\nEnd\n";
 std::string definitions(gateObjects);
 definitions += "ChildObject GateTestBarrier GateTestSegment\n GeometryMinorRadius = 240\nEnd\n";
 definitions += "ChildObject GateTestPostern GateTestSegment\n KindOf = STRUCTURE IMMOBILE WALL_UPGRADE WALL_GATE\n Behavior = DynamicPortalBehaviour Portal\n End\nEnd\n";
 definitions += "ChildObject GateTestTower GateTestSegment\n KindOf = STRUCTURE IMMOBILE WALL_UPGRADE CAN_ATTACK\n Behavior = AIUpdateInterface AI\n End\n GeometryMajorRadius = 15\n GeometryMinorRadius = 15\n AdditionalGeometry = BOX\n GeometryMajorRadius = 10\n GeometryMinorRadius = 10\n GeometryHeight = 50\n GeometryOffset = X:0 Y:0 Z:40\n AdditionalGeometry = BOX\n GeometryMajorRadius = 5\n GeometryMinorRadius = 20\n GeometryHeight = 40\nEnd\n";
 CombatWorld w(definitions.c_str());
 bool gate = true;
 bool postern = false;
 bool rotatedTower = false;
 SUBCASE("postern preserves neighboring tiles despite WALL_GATE") { gate = false; postern = true; }
 SUBCASE("gate clears three tiles") { gate = true; }
 SUBCASE("tower preserves overlapping neighboring tiles") { gate = false; }
 SUBCASE("rotated tower blocks movement across the wall") { gate = false; rotatedTower = true; }
 float angle = rotatedTower ? 1.57079632679f : 0;
 SUBCASE("vertical span") { angle = 0; }
 SUBCASE("rotated span") { angle = 1.57079632679f; }
 const bool rotated = angle != 0;
 auto segment = [&](float offset, float across = 0) {
  Object *o = w.unit("GateTestSegment", 'A', 500 + (rotated ? offset : across), 500 + (rotated ? across : offset));
  o->setOrientation(angle);
  w.ai->addObjectToPathfindMap(*o);
  return o;
 };
 Object *center = segment(0);
 Object *left = segment(-18), *right = segment(18);
 // Real wall spans overlap their tile edges: 2 * (half extent - 1).
 Object *outsideLeft = segment(-36), *outsideRight = segment(36);
 Object *parallel = segment(0, 20);
 ReplaceSelfUpgradeModuleData data;
 data.m_pieces = {gate ? "GateTestGate" : postern ? "GateTestPostern" : "GateTestTower"};
 ReplaceSelfUpgrade replacement(center, &data);
 replacement.giveSelfUpgrade();
 CHECK(left->isEffectivelyDead() == gate);
 CHECK(right->isEffectivelyDead() == gate);
 CHECK_FALSE(outsideLeft->isEffectivelyDead());
 CHECK_FALSE(outsideRight->isEffectivelyDead());
 CHECK_FALSE(parallel->isEffectivelyDead());
 for (Object *removed : (gate ? std::vector<Object *>{center, left, right} : std::vector<Object *>{center})) {
  auto *cell = w.ai->pathfinder().getCell(LAYER_GROUND, removed->getPosition());
  REQUIRE(cell);
  CHECK_FALSE(cell->isObstaclePresent(removed->getID()));
 }
 bool built = false;
 for (Object *o = w.logic->getFirstObject(); o; o = o->getNextObject())
  if (o->getTemplate()->getName() == (gate ? "GateTestGate" : postern ? "GateTestPostern" : "GateTestTower")) {
   built = true;
   if (!gate && !postern) {
    auto *cell = w.ai->pathfinder().getCell(LAYER_GROUND,o->getPosition());
    REQUIRE(cell);
    CHECK(cell->isObstaclePresent(o->getID()));
    // Neighboring wall updates can leave pinch-padding cells inside a replacement footprint.
    w.ai->removeObjectFromPathfindMap(*o);
    cell->setType(PathfindCell::CELL_BRIDGE_IMPASSABLE);
    w.ai->addObjectToPathfindMap(*o);
    CHECK(cell->isObstaclePresent(o->getID()));
    w.frames(5);
    CHECK(cell->isObstaclePresent(o->getID()));
    // Seal the span to both map borders, so a move across must stay blocked.
    for (float offset : {-260.0f,260.0f}) {
     Object *barrier = w.unit("GateTestBarrier", 'A', 500 + (rotated ? offset : 0), 500 + (rotated ? 0 : offset));
     barrier->setOrientation(angle);
     w.ai->addObjectToPathfindMap(*barrier);
    }
    Object *walker = w.unit("Swordsman", 'A', rotated ? 500 : 450, rotated ? 450 : 500);
    const int player = walker->getControllingPlayer()->getPlayerIndex();
    w.select(player,{walker});
    w.moveTo(player,rotated ? 500 : 550,rotated ? 550 : 500);
    w.frames(150);
    CHECK((rotated ? walker->getPosition()->y : walker->getPosition()->x) < 490);
   }
  }
 CHECK(built);
}

#include "GameLogic/Module/GateModules.h"
TEST_CASE("wall gates: opening clears the corridor and closing blocks it again") {
 const char definitions[] =
 "Object CorridorGate\n KindOf = STRUCTURE IMMOBILE BLOCKING_GATE WALL_GATE\n"
 " Behavior = GateOpenAndCloseBehavior Gate\n OpenByDefault = No\n ResetTimeInMilliseconds = 200\n End\n"
 " AdditionalGeometry = BOX\n GeometryName = \"Closed\"\n GeometryMajorRadius = 10\n GeometryMinorRadius = 50\n GeometryHeight = 50\n"
 " AdditionalGeometry = BOX\n GeometryName = \"OpenLeft\"\n GeometryMajorRadius = 10\n GeometryMinorRadius = 10\n GeometryHeight = 50\n GeometryOffset = X:0 Y:-50 Z:0\n"
 " AdditionalGeometry = BOX\n GeometryName = \"OpenRight\"\n GeometryMajorRadius = 10\n GeometryMinorRadius = 10\n GeometryHeight = 50\n GeometryOffset = X:0 Y:50 Z:0\n End\n";
 CombatWorld w(definitions);
 auto *gate = w.unit("CorridorGate", 'A', 500,500);
 w.frames(2);
 auto &pf = w.ai->pathfinder();
 Coord3D from{450,500,0},to{550,500,0};
 auto *cell = pf.getCell(LAYER_GROUND, gate->getPosition());
 REQUIRE(cell);
 CHECK(cell->isObstaclePresent(gate->getID()));
 auto *behavior = GateOpenAndCloseBehavior::findGate(*gate);
 REQUIRE(behavior);
 behavior->open(); w.frames(4);
 CHECK_FALSE(cell->isObstaclePresent(gate->getID()));
 auto *unit = w.unit("Swordsman", 'A',450,500);
 const int player = unit->getControllingPlayer()->getPlayerIndex();
 w.select(player,{unit});
 w.moveTo(player,550,500); w.frames(100);
 CHECK(unit->getPosition()->x > 520);
 behavior->close(); w.frames(4);
 CHECK(cell->isObstaclePresent(gate->getID()));
}

TEST_CASE("wall span terminates at terrain with a hub and cliff cap") {
 const char definitions[] =
 "Object TerrainWallHub\n KindOf = STRUCTURE IMMOBILE WALL_HUB\n Geometry = BOX\n GeometryMajorRadius = 4\n GeometryMinorRadius = 10\n GeometryHeight = 20\n Behavior = WallHubBehavior Hub\n SegmentTemplateName = TerrainWallTile\n DefaultSegmentTemplateName = TerrainWallTile\n HubCapTemplateName = TerrainWallHub\n CliffCapTemplateName = TerrainWallEnd\n BuilderRadius = 10\n End\nEnd\n"
 "Object TerrainWallTile\n KindOf = STRUCTURE IMMOBILE WALL_SEGMENT\n Geometry = BOX\n GeometryMajorRadius = 4\n GeometryMinorRadius = 10\n GeometryHeight = 20\nEnd\n"
 "ChildObject TerrainWallEnd TerrainWallTile\nEnd\n";
 CombatWorld w(definitions);
 w.logic->settings().buildRulesLoaded = true;
 w.logic->settings().allowedHeightVariationForBuilding = 10;
 w.logic->settings().maxLineBuildObjects = 50;
 auto *hub = w.unit("TerrainWallHub", 'A', 300,500);
 // A vertical cliff band crosses the proposed span, with ground continuing beyond it.
 for (int y=0; y<100; ++y) for(int x=45; x<50; ++x) w.terrain.cliff.insert({x,y});
 WallSpan::Plan plan;
 REQUIRE(WallSpan::plan(*w.logic,*hub,{300,500,0},{700,500,0},0,plan));
 REQUIRE(plan.tiles.size() >= 2);
 CHECK(plan.worstCode == 0);
 CHECK(plan.endCapPlaced);
 CHECK(plan.tiles.back().tmpl->getName() == "TerrainWallEnd");
 CHECK(plan.tiles[plan.tiles.size()-2].tmpl->getName() == "TerrainWallHub");
 for (const auto &tile : plan.tiles) CHECK(tile.pos.x < 500);
 std::vector<ObjectID> made;
 REQUIRE(WallSpan::build(*w.logic,*hub,{300,500,0},{700,500,0},*hub->getControllingPlayer(),0,&made));
 CHECK(made.size() == plan.tiles.size());
}

TEST_CASE("wall span optionally connects to friendly fortress trebuchet expansions") {
 CombatWorld w(
 "Object TestHub\n KindOf = STRUCTURE IMMOBILE WALL_HUB\n Geometry = BOX\n GeometryMajorRadius = 4\n GeometryMinorRadius = 10\n GeometryHeight = 20\n Behavior = WallHubBehavior Hub\n SegmentTemplateName = TestTile\n DefaultSegmentTemplateName = TestTile\n HubCapTemplateName = TestHub\n BuilderRadius = 10\n End\nEnd\n"
 "Object TestTile\n KindOf = STRUCTURE IMMOBILE WALL_SEGMENT\n Geometry = BOX\n GeometryMajorRadius = 4\n GeometryMinorRadius = 10\n GeometryHeight = 20\nEnd\n"
 "Object MenTrebuchetExpansion\n KindOf = STRUCTURE IMMOBILE\n Geometry = BOX\n GeometryMajorRadius = 20\n GeometryMinorRadius = 20\n GeometryHeight = 20\nEnd\n"
"ChildObject MenTrebuchetSideExpansion MenTrebuchetExpansion\nEnd\n");
 w.logic->settings().buildRulesLoaded=true;w.logic->settings().allowedHeightVariationForBuilding=10;w.logic->settings().maxLineBuildObjects=50;
 auto hub=w.unit("TestHub",'A',300,500);auto target=w.unit("MenTrebuchetExpansion",'A',600,500);
 WallSpan::Plan native;
 REQUIRE(WallSpan::plan(*w.logic,*hub,{300,500,0},{600,500,0},0,native));
 CHECK_FALSE(native.endsOnHub);
 w.logic->settings().wallBalance.enabled=true;w.logic->settings().wallBalance.trebuchetWallTargets=true;
 WallSpan::Plan connected;
 REQUIRE(WallSpan::plan(*w.logic,*hub,{300,500,0},{600,500,0},0,connected));
 CHECK(connected.endsOnHub);CHECK(connected.endHub==target->getID());
 auto side=w.unit("MenTrebuchetSideExpansion",'A',600,700);
 WallSpan::Plan middle;
 REQUIRE(WallSpan::plan(*w.logic,*hub,{300,500,0},{600,700,0},0,middle));
 CHECK(middle.endsOnHub);CHECK(middle.endHub==side->getID());
 REQUIRE_FALSE(connected.tiles.empty());
 CHECK(connected.tiles.back().pos.x>580); // extra tile clips into the expansion footprint
 CHECK(connected.worstCode==0);
 const auto last=connected.tiles.back();
 auto blocker=w.unit("MenTrebuchetExpansion",'A',last.pos.x,last.pos.y);
 WallSpan::Plan overlap;
 REQUIRE(WallSpan::plan(*w.logic,*hub,{300,500,0},{600,500,0},0,overlap));
 CHECK(overlap.tiles.back().code==0);
 CHECK(overlap.tiles.back().pos.x==last.pos.x);
 // Earlier segments still reject overlapping structures.
 blocker->setPosition(&connected.tiles.front().pos);
 WallSpan::Plan blocked;
 REQUIRE(WallSpan::plan(*w.logic,*hub,{300,500,0},{600,500,0},0,blocked));
 CHECK(blocked.tiles.front().code==LBC_OBJECTS_IN_THE_WAY);
}

TEST_CASE("wall spans: disabling intermediate hubs covers outer and expansion pattern templates") {
 bool missingDefault = false;
 SUBCASE("configured default segment") {}
 SUBCASE("missing default segment reports an error") { missingDefault = true; }
 for (const char *name : {"PatternHub", "PatternOuterHub", "PatternExpansionHub"}) {
  INFO(name);
  std::string definitions =
   "Object PlainTile\n KindOf = STRUCTURE IMMOBILE WALL_SEGMENT\n BuildCost = 1\n Geometry = BOX\n GeometryMajorRadius = 4\n GeometryMinorRadius = 10\n GeometryHeight = 20\nEnd\n";
  definitions += std::string("Object ") + name +
   "\n KindOf = STRUCTURE IMMOBILE WALL_HUB\n BuildCost = 1\n Geometry = BOX\n GeometryMajorRadius = 4\n GeometryMinorRadius = 10\n GeometryHeight = 20\nEnd\n";
  definitions += std::string("Object StartHub\n KindOf = STRUCTURE IMMOBILE WALL_HUB\n Geometry = BOX\n GeometryMajorRadius = 4\n GeometryMinorRadius = 10\n GeometryHeight = 20\n Behavior = WallHubBehavior Hub\n SegmentTemplateName = PlainTile ") + name +
   "\n DefaultSegmentTemplateName = PlainTile\n HubCapTemplateName = " + name + "\n BuilderRadius = 10\n End\nEnd\n";
  if (missingDefault) {
   const std::string field = "DefaultSegmentTemplateName = PlainTile";
   definitions.replace(definitions.find(field), field.size(), "DefaultSegmentTemplateName = UnknownTile");
  }
  CombatWorld w(definitions.c_str());
  w.logic->settings().buildRulesLoaded = true;
  w.logic->settings().allowedHeightVariationForBuilding = 10;
  w.logic->settings().maxLineBuildObjects = 50;
  auto *hub = w.unit("StartHub", 'A', 300, 500);
  auto &rules = w.logic->settings().wallBalance;
  rules.enabled = true;
  rules.intermediateHubs = false;
  WallSpan::Plan disabled;
  if (missingDefault) {
   CHECK_FALSE(WallSpan::plan(*w.logic, *hub, {300,500,0}, {600,500,0}, 0, disabled));
   CHECK(w.logic->report().errors == std::vector<std::string>{"wall span: intermediate hubs disabled but no default segment for StartHub"});
   continue;
  }
  REQUIRE(WallSpan::plan(*w.logic, *hub, {300,500,0}, {600,500,0}, 0, disabled));
  REQUIRE_FALSE(disabled.tiles.empty());
  CHECK(disabled.worstCode == 0);
  for (size_t i = 0; i + 1 < disabled.tiles.size(); ++i)
   CHECK(disabled.tiles[i].tmpl->getName() == "PlainTile");
  CHECK(disabled.tiles.back().tmpl->getName() == name); // endpoint cap is preserved
  rules.intermediateHubs = true;
  WallSpan::Plan enabled;
  REQUIRE(WallSpan::plan(*w.logic, *hub, {300,500,0}, {600,500,0}, 0, enabled));
  REQUIRE(enabled.tiles.size() == disabled.tiles.size());
  CHECK(enabled.tiles[1].tmpl->getName() == name);
  rules.enabled = false;
  rules.intermediateHubs = false;
  WallSpan::Plan native;
  REQUIRE(WallSpan::plan(*w.logic, *hub, {300,500,0}, {600,500,0}, 0, native));
  REQUIRE(native.tiles.size() == enabled.tiles.size());
  CHECK(native.tiles[1].tmpl->getName() == name);
 }
}

TEST_CASE("wall upgrade separation can be disabled while gates require three plain tiles") {
 CombatWorld w(
 "Object RelaxSegment\n KindOf = STRUCTURE IMMOBILE WALL_SEGMENT\n Geometry = BOX\n GeometryMajorRadius = 5\n GeometryMinorRadius = 10\n GeometryHeight = 20\nEnd\n"
 "ChildObject RelaxTower RelaxSegment\n KindOf = STRUCTURE IMMOBILE WALL_UPGRADE\n GeometryMinorRadius = 20\nEnd\n"
 "ChildObject RelaxHub RelaxSegment\n KindOf = STRUCTURE IMMOBILE WALL_HUB\nEnd\n"
 "ChildObject RelaxGate RelaxSegment\n KindOf = STRUCTURE IMMOBILE WALL_UPGRADE WALL_GATE\n GeometryMinorRadius = 30\nEnd\n");
 w.logic->settings().allowedHeightVariationForBuilding=10;
 auto *center=w.unit("RelaxSegment",'A',500,500);
 auto *nearby=w.unit("RelaxTower",'A',500,518);
 auto &rules=w.logic->settings().wallBalance;rules.enabled=true;
 ReplaceSelfUpgradeModuleData data;data.m_pieces={"RelaxTower"};data.m_activationMask.set(0);
 UpgradeMaskType mask;mask.set(0);
 ReplaceSelfUpgrade upgrade(center,&data);
 CHECK_FALSE(upgrade.wouldUpgrade(mask));
 const auto originalHash=w.logic->computeStateHash();
 rules.disableUpgradeSeparation=true;
 CHECK(w.logic->computeStateHash()!=originalHash);
 CHECK(upgrade.wouldUpgrade(mask));
 rules.enabled=false;CHECK_FALSE(upgrade.wouldUpgrade(mask));rules.enabled=true;
 data.m_pieces={"RelaxGate"};
 CHECK_FALSE(upgrade.wouldUpgrade(mask));
 w.logic->destroyObject(nearby);
 auto *left=w.unit("RelaxSegment",'A',500,482);
 CHECK_FALSE(upgrade.wouldUpgrade(mask));
 auto *right=w.unit("RelaxSegment",'A',500,518);
 CHECK(upgrade.wouldUpgrade(mask));
 right->setOrientation(1.57079632679f);CHECK_FALSE(upgrade.wouldUpgrade(mask));right->setOrientation(0);
 const Coord3D far{500,536,0};right->setPosition(&far);
 CHECK_FALSE(upgrade.wouldUpgrade(mask));
 const Coord3D restored{500,518,0};right->setPosition(&restored);
 w.logic->destroyObject(left);CHECK_FALSE(upgrade.wouldUpgrade(mask));
 auto *blocked=w.unit("RelaxHub",'A',500,482);CHECK_FALSE(upgrade.wouldUpgrade(mask));
 w.logic->destroyObject(blocked);
 w.unit("RelaxSegment",'B',500,482);CHECK_FALSE(upgrade.wouldUpgrade(mask));
}

TEST_CASE("wall junction tolerance only permits friendly endpoint walls")
{
 const char junctionDefinitions[] =
 "Object TerrainWallHub\n KindOf = STRUCTURE IMMOBILE WALL_HUB\n BuildCost = 20\n Geometry = BOX\n GeometryMajorRadius = 4\n GeometryMinorRadius = 10\n GeometryHeight = 20\n Behavior = WallHubBehavior Hub\n SegmentTemplateName = TerrainWallTile\n DefaultSegmentTemplateName = TerrainWallTile\n HubCapTemplateName = TerrainWallHub\n CliffCapTemplateName = TerrainWallEnd\n BuilderRadius = 10\n End\nEnd\n"
 "Object TerrainWallTile\n KindOf = STRUCTURE IMMOBILE WALL_SEGMENT\n BuildCost = 10\n Geometry = BOX\n GeometryMajorRadius = 4\n GeometryMinorRadius = 10\n GeometryHeight = 20\nEnd\n"
 "ChildObject TerrainWallEnd TerrainWallTile\n BuildCost = 7\nEnd\n";
 CombatWorld w(junctionDefinitions);
 w.logic->settings().buildRulesLoaded=true;
 w.logic->settings().allowedHeightVariationForBuilding=10;
 w.logic->settings().maxLineBuildObjects=50;
 auto *hub=w.unit("TerrainWallHub",'A',300,500);
 auto *end=w.unit("TerrainWallHub",'A',600,500);
 // A perpendicular wall ending on the destination can overlap the last tile.
 auto *attached=w.unit("TerrainWallTile",'A',596,506);
 WallSpan::Plan strict;
 REQUIRE(WallSpan::plan(*w.logic,*hub,{300,500,0},{600,500,0},0,strict));
 CHECK(strict.worstCode==LBC_OBJECTS_IN_THE_WAY);
 w.logic->settings().wallBalance.enabled=true;
 WallSpan::Plan forgiving;
 REQUIRE(WallSpan::plan(*w.logic,*hub,{300,500,0},{600,500,0},0,forgiving));
 CHECK(forgiving.endHub==end->getID());
 CHECK(forgiving.worstCode==0);
 // Cursor snapping extends to the hub's footprint, rather than only 20 units.
 const Coord3D larger{600,500,0};
 end->setPosition(&larger);
 WallSpan::Plan snap;
 REQUIRE(WallSpan::plan(*w.logic,*hub,{300,500,0},{620.5f,500,0},0,snap));
 CHECK(snap.endHub==end->getID());
 // Neither a hostile endpoint wall nor a crossing in the span is exempt.
 w.logic->destroyObject(attached);
 auto *enemy=w.unit("TerrainWallTile",'B',596,506);
 WallSpan::Plan hostile;
 REQUIRE(WallSpan::plan(*w.logic,*hub,{300,500,0},{600,500,0},0,hostile));
 CHECK(hostile.worstCode==LBC_OBJECTS_IN_THE_WAY);
 w.logic->destroyObject(enemy);
 auto *crossing=w.unit("TerrainWallTile",'A',440,500);
 WallSpan::Plan crossed;
 REQUIRE(WallSpan::plan(*w.logic,*hub,{300,500,0},{600,500,0},0,crossed));
 CHECK(crossed.worstCode==LBC_OBJECTS_IN_THE_WAY);
 CHECK_FALSE(crossing->isDestroyed());
}

TEST_CASE("fortress wall endpoints ignore their own castle foundations only on the outward side") {
 const char defs[]=
 "Object CastleJoinHub\n KindOf = STRUCTURE IMMOBILE WALL_HUB\n Geometry = BOX\n GeometryMajorRadius = 4\n GeometryMinorRadius = 10\n GeometryHeight = 20\n Behavior = CastleMemberBehavior Member\n End\n Behavior = WallHubBehavior Hub\n SegmentTemplateName = CastleJoinTile\n DefaultSegmentTemplateName = CastleJoinTile\n HubCapTemplateName = CastleJoinHub\n BuilderRadius = 10\n End\nEnd\n"
 "Object CastleJoinTile\n KindOf = STRUCTURE IMMOBILE WALL_SEGMENT\n Geometry = BOX\n GeometryMajorRadius = 4\n GeometryMinorRadius = 10\n GeometryHeight = 20\nEnd\n"
 "Object CastleJoinFoundation\n KindOf = STRUCTURE IMMOBILE BASE_FOUNDATION\n Geometry = BOX\n GeometryMajorRadius = 115\n GeometryMinorRadius = 100\n GeometryHeight = 20\nEnd\n";
 CombatWorld w(defs);
 w.logic->settings().buildRulesLoaded=true;w.logic->settings().allowedHeightVariationForBuilding=10;w.logic->settings().maxLineBuildObjects=50;
 auto *left=w.unit("CastleJoinFoundation",'A',210,500),*right=w.unit("CastleJoinFoundation",'A',690,500);
 auto *a=w.unit("CastleJoinHub",'A',300,500),*b=w.unit("CastleJoinHub",'A',600,500);
 dynamic_cast<CastleMemberBehavior *>(a->findModule("CastleMemberBehavior"))->setCastleId(left->getID());
 dynamic_cast<CastleMemberBehavior *>(b->findModule("CastleMemberBehavior"))->setCastleId(right->getID());
 WallSpan::Plan strict;
 REQUIRE(WallSpan::plan(*w.logic,*a,{300,500,0},{600,500,0},0,strict));
 CHECK(strict.worstCode==LBC_OBJECTS_IN_THE_WAY);
 w.logic->settings().wallBalance.enabled=true;
 WallSpan::Plan joined;
 REQUIRE(WallSpan::plan(*w.logic,*a,{300,500,0},{600,500,0},0,joined));
 CHECK(joined.worstCode==0);CHECK(joined.endHub==b->getID());
 WallSpan::Plan backwards;
 REQUIRE(WallSpan::plan(*w.logic,*b,{600,500,0},{300,500,0},0,backwards));
 CHECK(backwards.worstCode==0);
 auto *inside=w.unit("CastleJoinHub",'A',180,500);
 dynamic_cast<CastleMemberBehavior *>(inside->findModule("CastleMemberBehavior"))->setCastleId(left->getID());
 WallSpan::Plan inward;
 REQUIRE(WallSpan::plan(*w.logic,*a,{300,500,0},{180,500,0},0,inward));
 CHECK(inward.worstCode==LBC_OBJECTS_IN_THE_WAY);
 auto *blocker=w.unit("CastleJoinFoundation",'A',450,500);
 WallSpan::Plan blocked;
 REQUIRE(WallSpan::plan(*w.logic,*a,{300,500,0},{600,500,0},0,blocked));
 CHECK(blocked.worstCode==LBC_OBJECTS_IN_THE_WAY);
 CHECK_FALSE(blocker->isDestroyed());
}
