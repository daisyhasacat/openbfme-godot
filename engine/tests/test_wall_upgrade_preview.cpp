// OpenBFME. GPL-3.0. Wall preview spacing is presentation only.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "ProdTestUtil.h"
#include "GameClient/WallUpgradePreview.h"
#include "GameLogic/Module/ConstructionModules.h"
#include <cmath>
using namespace combattest;
namespace {
const char definitions[]=
"Object PreviewWall\n KindOf = STRUCTURE IMMOBILE WALL_SEGMENT SELECTABLE\n Geometry = BOX\n GeometryMajorRadius = 3\n GeometryMinorRadius = 10\n GeometryHeight = 40\n Body = ActiveBody Body\n MaxHealth = 100\n End\n Behavior = GettingBuiltBehavior Built\n End\nEnd\n"
"ChildObject PreviewHub PreviewWall\n KindOf = STRUCTURE IMMOBILE WALL_HUB WALL_SEGMENT\nEnd\n";
}
TEST_CASE("wall upgrade preview: alternating span slots, gate footprint, branches and gaps") {
 CombatWorld w(definitions);
 w.logic->productionSettings()=prodtest::defaultSettings();
 float angle=0;
 SUBCASE("axis aligned") {}
 SUBCASE("diagonal") {angle=.78539816339f;}
 std::vector<Object *> walls;
 for(int i=-4;i<=4;++i) {
  auto *o=w.unit("PreviewWall",'A',500-std::sin(angle)*i*18,500+std::cos(angle)*i*18);
  o->setOrientation(angle);walls.push_back(o);
 }
 auto *source=walls[4];auto *built=dynamic_cast<GettingBuiltBehavior *>(source->findModule("GettingBuiltBehavior"));REQUIRE(built);
 for(auto *o:walls)built->addLinkedPiece(*o);
 auto *branch=w.unit("PreviewWall",'A',500,500);branch->setOrientation(angle+1.57079632679f);built->addLinkedPiece(*branch);
 auto *hub=w.unit("PreviewHub",'A',500,500);built->addLinkedPiece(*hub);
 auto *foreign=w.unit("PreviewWall",'B',walls[0]->getPosition()->x,walls[0]->getPosition()->y);foreign->setOrientation(angle);built->addLinkedPiece(*foreign);
 w.unit("PreviewWall",'A',500-std::sin(angle)*6*18,500+std::cos(angle)*6*18); // collinear, but belongs to no recorded span
 const auto hash=w.logic->computeStateHash();
 CHECK(WallUpgradePreview::objects(*w.logic,*source,false)==std::vector<ObjectID>{walls[0]->getID(),walls[2]->getID(),source->getID(),walls[6]->getID(),walls[8]->getID()});
 CHECK(WallUpgradePreview::objects(*w.logic,*source,true)==std::vector<ObjectID>{walls[1]->getID(),walls[3]->getID(),source->getID(),walls[5]->getID(),walls[7]->getID()});
 CHECK(w.logic->computeStateHash()==hash);
 w.logic->destroyObject(walls[6]);
 CHECK(WallUpgradePreview::objects(*w.logic,*source,false)==std::vector<ObjectID>{walls[0]->getID(),walls[2]->getID(),source->getID(),walls[8]->getID()});
 w.logic->destroyObject(source);CHECK(WallUpgradePreview::objects(*w.logic,*source,false).empty());
}
