// OpenBFME. GPL-3.0. Authored fishing-mod acceptance tests; no retail assets.
#include "doctest.h"

#include "Common/Player.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "EconGameData.h"
#include "GameClient/InGameUI.h"
#include "GameClient/HudObjects.h"
#include "GameClient/MessageStream/CommandXlat.h"
#include "GameClient/TacticalView.h"
#include "GameLogic/AI/GarrisonCommands.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/Module/FishingUpdate.h"
#include "MoveTestUtil.h"

namespace {
struct FishingWorld : movetest::MoveWorld {
  FishingWorld() {
    FishingUpdate::registerExtension(w.modules);
    REQUIRE(w.load("ChildObject FishingBoat Walker\n"
                   " Behavior = OpenBFMEFishingUpdate Fishing\n"
                   "  TargetFilter = ANY +FishPoint\n"
                   "  Range = 120\n"
                   "  IncomeInterval = 1000\n"
                   "  IncomeAmount = 25\n"
                   " End\nEnd\n"
                   "Object FishPoint\n KindOf = SELECTABLE\nEnd\n")
                .empty());
    std::string error;
    REQUIRE(EconomySettings::scan(econtest::kGameData,
                                  logic->economy().settings(), &error));
    GarrisonCommands::registerHandlers(dispatcher);
    buildMap();
  }
  auto cash() {
    return players.findPlayerWithName("Alice")->getMoney()->countMoney();
  }
  void fish(Object *boat, Object *point) {
    const int player = playerIndex("Alice");
    select(player, {boat});
    GameMessage m(MSG_ENTER, player);
    m.appendObjectIDArgument(boat->getID());
    m.appendObjectIDArgument(point->getID());
    send(m);
    frames(1);
  }
};
} // namespace
TEST_CASE("fishing: proximity alone pays nothing; explicit enter starts full "
          "intervals without containment") {
  FishingWorld w;
  Object *boat = w.spawn("FishingBoat", 105, 105);
  Object *point = w.spawn("FishPoint", 205, 105);
  const auto initial = w.cash();
  w.frames(20);
  CHECK(w.cash() == initial);
  w.fish(boat, point);
  auto *f = FishingUpdate::find(*boat);
  REQUIRE(f != nullptr);
  REQUIRE(f->working());
  CHECK(boat->getContainedBy() == nullptr);
  CHECK(f->target() == point->getID());
  w.frames(4);
  CHECK(w.cash() == initial);
  w.frames(1);
  CHECK(w.cash() == initial + 25);
  w.frames(5);
  CHECK(w.cash() == initial + 50);
  boat->getAIUpdateInterface()->aiIdle(CMD_FROM_PLAYER);
  CHECK(f->target() == INVALID_ID);
  w.frames(10);
  CHECK(w.cash() == initial + 50);
}
TEST_CASE("fishing: approach pays nothing until arrival; another move cancels "
          "the task") {
  FishingWorld w;
  Object *boat = w.spawn("FishingBoat", 105, 105);
  Object *point = w.spawn("FishPoint", 505, 105);
  const auto initial = w.cash();
  w.fish(boat, point);
  auto *f = FishingUpdate::find(*boat);
  CHECK_FALSE(f->working());
  w.frames(5);
  CHECK(w.cash() == initial);
  for (int i = 0; i < 150 && !f->working(); ++i)
    w.frames(1);
  REQUIRE(f->working());
  w.frames(5);
  CHECK(w.cash() == initial + 25);
  w.moveTo(w.playerIndex("Alice"), 105, 405);
  w.frames(1);
  CHECK(f->target() == INVALID_ID);
  const auto stopped = w.cash();
  w.frames(20);
  CHECK(w.cash() == stopped);
}
TEST_CASE("fishing: invalid targets and enemy selections are refused; a "
          "deleted point ends income") {
  FishingWorld w;
  Object *boat = w.spawn("FishingBoat", 105, 105);
  Object *point = w.spawn("FishPoint", 205, 105);
  Object *other = w.spawn("Walker", 205, 205);
  auto *f = FishingUpdate::find(*boat);
  CHECK_FALSE(f->order(other));
  CHECK_FALSE(f->order(boat));
  const int bob = w.playerIndex("Bob");
  w.select(bob, {boat});
  GameMessage forged(MSG_ENTER, bob);
  forged.appendObjectIDArgument(boat->getID());
  forged.appendObjectIDArgument(point->getID());
  w.send(forged);
  w.frames(1);
  CHECK(f->target() == INVALID_ID);
  w.fish(boat, point);
  REQUIRE(f->working());
  const auto initial = w.cash();
  w.logic->destroyObject(point);
  w.frames(10);
  CHECK(f->target() == INVALID_ID);
  CHECK(w.cash() == initial);
}
TEST_CASE("fishing: leaving range resets payment and task") {
  FishingWorld w;
  Object *boat = w.spawn("FishingBoat", 105, 105);
  Object *point = w.spawn("FishPoint", 205, 105);
  const auto initial = w.cash();
  w.fish(boat, point);
  w.frames(3);
  Coord3D away{705, 705, 0};
  boat->setPosition(&away);
  w.frames(5);
  CHECK(w.cash() == initial);
  CHECK(FishingUpdate::find(*boat)->target() == INVALID_ID);
}

namespace {
struct FishingView : TacticalView {
  ICoord2D size() const override { return {1280, 720}; }
  bool worldToScreen(const Coord3D &p, ICoord2D &out) const override {
    out = {(int)p.x, (int)p.y};
    return true;
  }
  bool screenToRay(const ICoord2D &p, Coord3D &o, Coord3D &d) const override {
    o = {(float)p.x, (float)p.y, 1000};
    d = {0, 0, -1};
    return true;
  }
  Coord3D position() const override { return {0, 0, 0}; }
  void lookAt(const Coord3D &) override {}
};
} // namespace
TEST_CASE("fishing: context cursor and right-click issue enter only for a "
          "fishing ship and spot") {
  FishingWorld w;
  Object *boat = w.spawn("FishingBoat", 105, 105);
  Object *point = w.spawn("FishPoint", 205, 105);
  Object *ordinary = w.spawn("Walker", 105, 205);
  w.players.setLocalPlayer(boat->getControllingPlayer());
  FishingView view;
  InGameUI ui;
  MessageStream stream;
  HudContext context{*w.logic, w.ai.get(), view, ui, stream, nullptr, {}};
  CommandTranslator commands(context);
  ui.selectObject(boat->getID());
  context.pickRay = [](const Object &, const Coord3D &, const Coord3D &, float *) { return DrawablePick::Result::Miss; };
  CHECK(HudObjects::pickObject(context, {205, 105}) == point);
  CHECK(commands.evaluateContextCommand(point, point->getPosition(),
                                        EvaluateType::DoHint) == MSG_ENTER);
  CHECK(ui.cursor() == MouseCursorName::EnterFriendly);
  CHECK(stream.pending() == 0);
  CHECK(commands.evaluateContextCommand(point, point->getPosition(),
                                        EvaluateType::DoCommand) == MSG_ENTER);
  CHECK(stream.pending() == 1);
  ui.deselectAll();
  ui.selectObject(ordinary->getID());
  CHECK(HudObjects::pickObject(context, {205, 105}) == nullptr);
  CHECK(commands.evaluateContextCommand(point, point->getPosition(),
                                        EvaluateType::EvaluateOnly) !=
        MSG_ENTER);
}
TEST_CASE("fishing: capture cancels the old task and active task state "
          "participates in hashing") {
  FishingWorld w;
  Object *boat = w.spawn("FishingBoat", 105, 105);
  Object *point = w.spawn("FishPoint", 205, 105);
  w.fish(boat, point);
  auto *f = FishingUpdate::find(*boat);
  REQUIRE(f->working());
  const auto before = w.logic->computeStateHash();
  f->cancel();
  CHECK(w.logic->computeStateHash() != before);
  w.fish(boat, point);
  const auto initial = w.cash();
  boat->setTeam(w.teamOf("Bob"));
  w.frames(10);
  CHECK(f->target() == INVALID_ID);
  CHECK(w.cash() == initial);
}

TEST_CASE(
    "fishing: native module snapshot restores the pending payment interval") {
  FishingWorld w;
  Object *boat = w.spawn("FishingBoat", 105, 105);
  Object *point = w.spawn("FishPoint", 205, 105);
  w.fish(boat, point);
  w.frames(3);
  auto *f = FishingUpdate::find(*boat);
  const auto initial = w.cash();
  XferSave saved;
  f->xfer(saved);
  f->cancel();
  XferLoad restored(saved.data());
  f->xfer(restored);
  REQUIRE(restored.atEnd());
  REQUIRE(f->working());
  CHECK(f->target() == point->getID());
  w.frames(1);
  CHECK(w.cash() == initial);
  w.frames(1);
  CHECK(w.cash() == initial + 25);
}
