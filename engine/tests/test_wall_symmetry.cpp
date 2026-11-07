// OpenBFME. GPL-3.0. Synthetic expectations for the requested Shift placement aid.
#include "doctest.h"
#include "ProdTestUtil.h"
#include "GameClient/WallSymmetry.h"
#include "GameClient/PlacementIcons.h"
#include "GameClient/Drawable.h"
#include "GameClient/MessageStream/PlaceEventTranslator.h"
#include "PathfindTestUtil.h"
#include "GameLogic/AI/AIWorld.h"

namespace {
const char definitions[] =
"Object SymmetryCastle\n KindOf = IMMOBILE CASTLE_CENTER\nEnd\n"
"Object SymmetryHub\n KindOf = STRUCTURE IMMOBILE WALL_HUB\n"
" Behavior = CastleMemberBehavior Member\n End\n"
" Behavior = GettingBuiltBehavior Built\n End\n"
" Behavior = WallHubBehavior Hub\n End\nEnd\n"
"Object SymmetryOuter\n KindOf = STRUCTURE IMMOBILE WALL_HUB\n Behavior = GettingBuiltBehavior Built\n End\n Behavior = WallHubBehavior Hub\n End\nEnd\n";
struct Rig : prodtest::ProdWorld {
    Object *castle, *left, *right;
    Rig() {
        load(definitions);
        castle = make("SymmetryCastle", teamOf("Alice"));
        left = make("SymmetryHub", teamOf("Alice"));
        right = make("SymmetryHub", teamOf("Alice"));
        Coord3D c{500,500,0}, a{564,564,0}, b{564,436,0};
        castle->setPosition(&c); left->setPosition(&a); right->setPosition(&b);
        for (auto *hub : {left, right})
            dynamic_cast<CastleMemberBehavior *>(hub->findModule("CastleMemberBehavior"))->setCastleId(castle->getID());
    }
    void connect(Object &outer, Object &root) {
        dynamic_cast<GettingBuiltBehavior *>(outer.findModule("GettingBuiltBehavior"))->addLinkedPiece(root);
    }
};
}
TEST_CASE("wall symmetry: front pair reflects destination and leaves simulation unchanged") {
    Rig r;
    const auto hash = r.logic->computeStateHash();
    Coord3D out;
    REQUIRE(WallSymmetry::partner(*r.logic, *r.left, {764,664,0}, 0, out) == r.right);
    CHECK(out.x == 764); CHECK(out.y == 336); CHECK(out.z == 0);
    REQUIRE(WallSymmetry::partner(*r.logic, *r.right, {764,336,0}, 0, out) == r.left);
    CHECK(out.x == 764); CHECK(out.y == 664);
    CHECK(r.logic->computeStateHash() == hash);
}
TEST_CASE("wall symmetry: rotated fortress uses local front axis") {
    Rig r;
    r.castle->setOrientation(1.5707963267948966f);
    Coord3D a{436,564,0}, b{564,564,0}, out;
    r.left->setPosition(&a); r.right->setPosition(&b);
    REQUIRE(WallSymmetry::partner(*r.logic, *r.left, {336,764,0}, 0, out) == r.right);
    CHECK(std::round(out.x) == 664); CHECK(std::round(out.y) == 764);
}
TEST_CASE("wall symmetry: missing, unfinished, rear and unrelated hubs do not pair") {
    Rig r; Coord3D out;
    SUBCASE("missing") { r.logic->destroyObject(r.right); }
    SUBCASE("enemy") { r.right->setTeam(r.teamOf("Bob")); }
    SUBCASE("unfinished") { r.right->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, true); }
    SUBCASE("rear") {
        Coord3D a{436,564,0}, b{436,436,0}; r.left->setPosition(&a); r.right->setPosition(&b);
    }
    SUBCASE("different fortress") {
        dynamic_cast<CastleMemberBehavior *>(r.right->findModule("CastleMemberBehavior"))->setCastleId(INVALID_ID);
    }
    SUBCASE("offset partner") { Coord3D p{570,436,0}; r.right->setPosition(&p); }
    REQUIRE(WallSymmetry::partner(*r.logic, *r.left, {764,664,0}, 0, out) == nullptr);
}
TEST_CASE("wall symmetry: one connected span qualifies, a second hop does not") {
    Rig r; Coord3D out;
    auto *a = r.make("SymmetryOuter", r.teamOf("Alice"));
    auto *b = r.make("SymmetryOuter", r.teamOf("Alice"));
    Coord3D pa{764,564,0}, pb{764,436,0}; a->setPosition(&pa); b->setPosition(&pb);
    CHECK(WallSymmetry::partner(*r.logic, *a, {864,664,0}, 0, out) == nullptr);
    a->setProducer(r.left); b->setProducer(r.right);
    CHECK(WallSymmetry::castleOf(*r.logic, *a) == nullptr);
    r.connect(*a, *r.left); r.connect(*b, *r.right);
    REQUIRE(WallSymmetry::partner(*r.logic, *a, {864,664,0}, 0, out) == b);
    CHECK(out.x == 864); CHECK(out.y == 336);
    auto *far = r.make("SymmetryOuter", r.teamOf("Alice"));
    Coord3D p{964,564,0}; far->setPosition(&p); r.connect(*far, *a);
    CHECK(WallSymmetry::anchor(*r.logic, *far) == nullptr);
}
namespace {
struct EmptyAssets : W3DFileSource {
    bool Exists(const std::string &) const override { return false; }
    bool Read(const std::string &, std::vector<std::uint8_t> &, std::string *) override { return false; }
};
}
TEST_CASE("wall symmetry: Shift previews and dispatches both spans, release restores one") {
    Rig r;
    r.load("Object SymmetrySegment\n KindOf = STRUCTURE IMMOBILE WALL_SEGMENT\n BuildCost = 10\n Geometry = BOX\n GeometryMajorRadius = 10\n GeometryMinorRadius = 20\n GeometryHeight = 10\nEnd\n"
        "ChildObject SymmetryBuildHub SymmetryHub\n BuildCost = 20\n Geometry = BOX\n GeometryMajorRadius = 10\n GeometryMinorRadius = 20\n GeometryHeight = 10\n Behavior = WallHubBehavior Hub\n SegmentTemplateName = SymmetrySegment\n DefaultSegmentTemplateName = SymmetrySegment\n HubCapTemplateName = SymmetryBuildHub\n BuilderRadius = 20\n End\nEnd\n");
    auto *left = r.make("SymmetryBuildHub", r.teamOf("Alice"));
    auto *right = r.make("SymmetryBuildHub", r.teamOf("Alice"));
    left->setPosition(r.left->getPosition()); right->setPosition(r.right->getPosition());
    for (auto *hub : {left,right}) dynamic_cast<CastleMemberBehavior *>(hub->findModule("CastleMemberBehavior"))->setCastleId(r.castle->getID());
    r.logic->destroyObject(r.left); r.logic->destroyObject(r.right);
    pathtest::SyntheticTerrain terrain{256,256}; INIMacroTable macros;
    AIWorldConfig config; config.pathfind = pathtest::testConfig();
    AIWorld ai(*r.logic,config,macros); ai.attach(); ai.newMap(terrain);
    r.logic->settings().buildRulesLoaded = true;
    r.logic->settings().maxLineBuildObjects = 50;
    CommandButton command; command.m_options = 0;
    InGameUI ui; ui.placeBuildAvailable("SymmetryBuildHub",left->getID(), &command);
    Coord3D end{864,564,0}; ui.setPlaceGhost(end,0,0);
    WallSpan::Plan first, second;
    REQUIRE(WallSpan::plan(*r.logic,*left,*left->getPosition(),end,0,first));
    REQUIRE(WallSpan::plan(*r.logic,*right,*right->getPosition(),{864,436,0},0,second));
    REQUIRE(first.worstCode == 0); REQUIRE(second.worstCode == 0);
    EmptyAssets source; WW3DAssetManager assets(source); DrawServices services(assets,r.w.modules,{});
    PlacementIcons icons(services,*r.logic);
    ui.setPreferSelectionMode(true);
    icons.update(ui,33,PlaceEventTranslator::ghostOptions());
    CHECK(icons.icons().size() == first.tiles.size()+second.tiles.size());
    CHECK(icons.icons().back().drawable->getPosition()->y == second.tiles.back().pos.y);
    ui.setPreferSelectionMode(false); icons.update(ui,33,PlaceEventTranslator::ghostOptions());
    CHECK(icons.icons().size() == first.tiles.size());
    bool shift = true;
    SUBCASE("Shift builds both") {}
    SUBCASE("released Shift builds one") { shift = false; }
    SUBCASE("insufficient combined funds builds neither") {
        r.alice()->withdrawMoney(r.alice()->getMoney()->countMoney() - first.cost, false);
    }
    const bool enough = r.alice()->getMoney()->countMoney() >= (unsigned)(first.cost+second.cost);
    ui.setPreferSelectionMode(shift); ui.placementSnap = true;
    ICoord2D pixel{864,564}; ui.setPlacementStart(&pixel); ui.setLineBuildStarted(true);
    MessageStream stream; PinholeView view;
    HudContext ctx{*r.logic,&ai,view,ui,stream,&r.commands,{}, {}};
    PlaceEventTranslator translator(ctx);
    ClientMessage click(CMSG_MOUSE_LEFT_CLICK); click.appendPixelRegion({pixel,pixel}); click.appendInteger(0);
    translator.translate(click);
    auto messages = stream.propagate();
    const size_t expected = shift ? (enough ? 2 : 0) : 1;
    REQUIRE(messages.size() == expected);
    if (expected) {
        CHECK(messages[0].type() == MSG_WALL_HUB_CONSTRUCT_SPAN);
        CHECK(messages[0].arg(4).objectID == left->getID());
    }
    if (expected == 2) {
        CHECK(messages[1].arg(4).objectID == right->getID());
        CHECK(messages[1].arg(2).location.y == 436);
    }
}
