// OpenBFME input improvement: nearby enemy targeting is shared by mouse and controller.
// Synthetic world; a drawable miss deliberately models the gap beside a soldier.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "GameClient/HudObjects.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameClient/MessageStream/CommandXlat.h"

using namespace combattest;

namespace
{
struct AssistView : TacticalView
{
	int height = 720;
	ICoord2D size() const override { return {1280, height}; }
	bool worldToScreen(const Coord3D &p, ICoord2D &out) const override { out = {(int)p.x, (int)p.y}; return true; }
	bool screenToRay(const ICoord2D &p, Coord3D &o, Coord3D &d) const override
	{
		o = {(float)p.x, (float)p.y, 1000}; d = {0, 0, -1}; return true;
	}
	Coord3D position() const override { return {0,0,0}; }
	void lookAt(const Coord3D &) override {}
};
}

TEST_CASE("hud attack assist: nearby mouse and controller targets match the attack cursor and command")
{
	CombatWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *source = w.unit("Slasher", 'A', 100, 100);
	Object *enemy = w.unit("Dummy", 'B', 400, 300);
	w.logic->players().setLocalPlayer(source->getControllingPlayer());
	AssistView view;
	InGameUI ui;
	MessageStream stream;
	HudContext ctx{*w.logic, w.ai.get(), view, ui, stream, nullptr, {}};
	ctx.pickRay = [](const Object &, const Coord3D &, const Coord3D &, float *) { return DrawablePick::Result::Miss; };
	ui.selectObject(source->getID());
	const ICoord2D near{430,300};
	CHECK(HudObjects::pickForSelection(ctx, near) == nullptr);
	CHECK(HudObjects::pickForHover(ctx, near) == enemy);
	CHECK(HudObjects::pickObject(ctx, near) == enemy); // controller and order clicks use this same path
	CHECK(HudObjects::pickNearbyAttackTarget(ctx, {432,300}) == enemy);
	CHECK(HudObjects::pickNearbyAttackTarget(ctx, {433,300}) == nullptr);
	CHECK(stream.pending() == 0);
	CommandTranslator commands(ctx);
	CHECK(commands.evaluateContextCommand(enemy, enemy->getPosition(), EvaluateType::DoHint) == MSG_DO_ATTACK_OBJECT);
	CHECK(ui.cursor() == MouseCursorName::AttackObj);
	CHECK(commands.evaluateContextCommand(HudObjects::pickObject(ctx, near), enemy->getPosition(), EvaluateType::DoCommand) == MSG_DO_ATTACK_OBJECT);
	CHECK(stream.pending() == 1);
}

TEST_CASE("hud attack assist: priority and eligibility keep unrelated actions precise")
{
	CombatWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *source = w.unit("Slasher", 'A', 100, 100);
	Object *enemy = w.unit("Dummy", 'B', 400, 300);
	w.logic->players().setLocalPlayer(source->getControllingPlayer());
	AssistView view;
	InGameUI ui;
	MessageStream stream;
	HudContext ctx{*w.logic, w.ai.get(), view, ui, stream, nullptr, {}};
	ctx.pickRay = [](const Object &, const Coord3D &, const Coord3D &, float *) { return DrawablePick::Result::Miss; };
	ui.selectObject(source->getID());
	ICoord2D near{430,300};
	Object *expected = enemy;
	SUBCASE("no selection") { ui.deselectAll(true); expected = nullptr; }
	SUBCASE("noncombat selection") { ui.deselectAll(true); ui.selectObject(w.unit("Dummy", 'A', 100, 200)->getID()); expected = nullptr; }
	SUBCASE("dead enemy") { enemy->kill(DEATH_NORMAL); expected = nullptr; }
	SUBCASE("friendly unit") { enemy->setTeam(w.teamA()); expected = nullptr; }
	SUBCASE("horde member targets its horde")
	{
		expected = w.unit("ArcherHorde", 'B', 800, 600);
		enemy->friend_setContainedBy(expected);
	}
	SUBCASE("nearest enemy") { expected = w.unit("Dummy", 'B', 425, 300); }
	SUBCASE("equal distance uses object id") { w.unit("Dummy", 'B', 400, 300); }
	SUBCASE("scaled viewport") { view.height = 1440; near = {464,300}; }
	SUBCASE("outside scaled radius") { view.height = 1440; near = {465,300}; expected = nullptr; }
	SUBCASE("direct model hit retains priority")
	{
		expected = w.unit("Dummy", 'A', 200, 200);
		ctx.pickRay = [expected](const Object &o, const Coord3D &, const Coord3D &, float *t) {
			*t = 1; return &o == expected ? DrawablePick::Result::Hit : DrawablePick::Result::Miss;
		};
	}
	SUBCASE("force move") { ui.setForceMoveToMode(true); expected = nullptr; }
	SUBCASE("waypoint") { ui.setWaypointMode(true); expected = nullptr; }
	SUBCASE("drawable hidden") { enemy->setDrawableHidden(true); expected = nullptr; }
	SUBCASE("garrisoned enemy") { enemy->friend_setContainedBy(source); expected = nullptr; }
	SUBCASE("pending ability") { static CommandButton pending; ui.setGUICommand(&pending); expected = nullptr; }
	CHECK(HudObjects::pickForHover(ctx, near) == expected);
	CHECK(HudObjects::pickObject(ctx, near) == expected);
}

TEST_CASE("hud attack assist: fog and shroud do not reveal nearby enemy positions")
{
	CombatWorld w;
	w.combat().setAutoAcquireEnabled(false);
	ShroudManager shroud(*w.logic);
	shroud.init(40.0f, 1);
	shroud.setExtent(0,0,1000,1000);
	shroud.attach();
	shroud.setDisplayed(true);
	Object *source = w.unit("Slasher", 'A', 100, 100);
	Object *enemy = w.unit("Dummy", 'B', 400, 300);
	Player *local = source->getControllingPlayer();
	w.logic->players().setLocalPlayer(local);
	AssistView view;
	InGameUI ui;
	MessageStream stream;
	HudContext ctx{*w.logic, w.ai.get(), view, ui, stream, nullptr, {}};
	ctx.pickRay = [](const Object &, const Coord3D &, const Coord3D &, float *) { return DrawablePick::Result::Miss; };
	ui.selectObject(source->getID());
	shroud.updateNow(*enemy);
	Object *expected = nullptr;
	SUBCASE("never seen") {}
	SUBCASE("fogged") { shroud.revealMapForPlayer(local->getPlayerIndex()); }
	SUBCASE("visible") { shroud.revealMapForPlayerPermanently(local->getPlayerIndex()); expected = enemy; }
	const auto hash = w.logic->computeStateHash();
	CHECK(HudObjects::pickForHover(ctx, {430,300}) == expected);
	CHECK(HudObjects::pickObject(ctx, {430,300}) == expected);
	CHECK(w.logic->computeStateHash() == hash); // client visibility queries must not populate hashed caches
}
