// OpenBFME. GPL-3.0. BFME2 FormationTranslator gesture facts, independent of retail data.
#include "doctest.h"

#include "CombatTestUtil.h"
#include "GameClient/FormationPlacement.h"
#include "GameClient/HudInput.h"
#include <limits>

namespace
{
ClientMessage button(int type, int x, int y, int time)
{
	ClientMessage m(type);
	m.appendPixel({x, y});
	m.appendInteger(123);
	m.appendInteger(time);
	return m;
}
struct FlatView : TacticalView
{
	ICoord2D size() const override
	{
		return {1280, 720};
	}
	bool worldToScreen(const Coord3D &p, ICoord2D &out) const override
	{
		out = {(int)p.x, (int)p.y};
		return true;
	}
	bool screenToRay(const ICoord2D &p, Coord3D &o, Coord3D &d) const override
	{
		o = {(float)p.x, (float)p.y, 1000};
		d = {0, 0, -1};
		return true;
	}
	Coord3D position() const override
	{
		return {};
	}
	void lookAt(const Coord3D &) override
	{
	}
};
struct Rig
{
	combattest::CombatWorld world;
	FlatView view;
	InGameUI ui;
	MessageStream stream;
	HudContext ctx{*world.logic, world.ai.get(), view, ui, stream, nullptr, {}};
	FormationPlacement placement{ctx};
	Rig()
	{
		Object *unit = world.unit("Slasher", 'A', 100, 100);
		world.logic->players().setLocalPlayer(unit->getControllingPlayer());
		ui.selectObject(unit->getID());
		stream.attachTranslator(&placement, 35);
	}
	std::vector<ClientMessage> send(const ClientMessage &m)
	{
		stream.append(m.type()) = m;
		return stream.propagate();
	}
};
} // namespace

TEST_CASE("formation chord: BFME2 RVA 0x431BDD requires opposite buttons, <500 ms and <5 pixels on each axis")
{
	const auto first = button(CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 100, 100, 1000);
	CHECK(FormationPlacement::isStartOfDrag(first, button(CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 104, 96, 1499)));
	CHECK_FALSE(FormationPlacement::isStartOfDrag(first, button(CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 100, 100, 1500)));
	CHECK_FALSE(FormationPlacement::isStartOfDrag(first, button(CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 105, 100, 1001)));
	CHECK_FALSE(FormationPlacement::isStartOfDrag(first, button(CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 100, 95, 1001)));
	CHECK_FALSE(FormationPlacement::isStartOfDrag(first, button(CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 100, 100, 1001)));
	CHECK(FormationPlacement::isStartOfDrag(button(CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 100, 100, -20), button(CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 100, 100, 20)));
	CHECK_FALSE(FormationPlacement::withinChordDistance({std::numeric_limits<int>::min(), 0}, {std::numeric_limits<int>::max(), 0}));
}

TEST_CASE("formation chord: native first-press anchor, either release commits once, both resulting clicks are eaten")
{
	for (bool rightFirst : {false, true})
	{
		Rig r;
		int first = rightFirst ? CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN : CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN;
		int second = rightFirst ? CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN : CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN;
		CHECK(r.send(button(first, 200, 200, 1000)).empty());
		CHECK_FALSE(r.ui.formationActive);
		CHECK(r.send(button(second, 204, 204, 1020)).empty());
		REQUIRE(r.ui.formationActive);
		REQUIRE(r.ui.formationSlots.size() == 1);
		CHECK(r.ui.formationSlots[0].position.x == 200.f);
		CHECK(r.ui.formationSlots[0].position.y == 200.f);
		auto out = r.send(button(CMSG_RAW_MOUSE_LEFT_BUTTON_UP, 204, 204, 1040));
		REQUIRE(out.size() == 1);
		CHECK(out[0].type() == MSG_DO_MOVE_AND_ORIENTATE_OBJECTTO);
		CHECK_FALSE(r.ui.formationActive);
		CHECK(r.send(button(CMSG_RAW_MOUSE_RIGHT_BUTTON_UP, 204, 204, 1060)).empty());
		CHECK(r.send(ClientMessage(CMSG_MOUSE_LEFT_CLICK)).empty());
		CHECK(r.send(ClientMessage(CMSG_MOUSE_RIGHT_CLICK)).empty());
	}
}

TEST_CASE("formation chord: failed chord replays the original press before the event, preserving modifiers and timestamp")
{
	for (int failure : {0, 1, 2, 3})
	{
		Rig r;
		auto first = button(CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 200, 200, 1000);
		REQUIRE(r.send(first).empty());
		ClientMessage next = failure == 0	? button(CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 200, 200, 1500)
							 : failure == 1 ? button(CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 205, 200, 1020)
							 : failure == 2 ? button(CMSG_RAW_MOUSE_POSITION, 205, 200, 1020)
											: button(CMSG_RAW_MOUSE_LEFT_BUTTON_UP, 200, 200, 1020);
		auto out = r.send(next);
		REQUIRE(out.size() == 2);
		CHECK(out[0].type() == first.type());
		CHECK(out[0].arg(0).pixel.x == 200);
		CHECK(out[0].arg(1).integer == 123);
		CHECK(out[0].arg(2).integer == 1000);
		CHECK(out[1].type() == next.type());
		CHECK_FALSE(r.ui.formationActive);
	}
}

TEST_CASE("formation chord: failed placement replays both buttons; reset discards a pending first press")
{
	Rig r;
	r.ui.deselectAll(false);
	REQUIRE(r.send(button(CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 200, 200, 1000)).empty());
	auto out = r.send(button(CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 200, 200, 1020));
	REQUIRE(out.size() == 2);
	CHECK(out[0].type() == CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN);
	CHECK(out[1].type() == CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN);
	CHECK_FALSE(r.ui.formationActive);
	r.placement.cancel();
	REQUIRE(r.send(button(CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 200, 200, 2000)).empty());
	r.placement.cancel();
	out = r.send(button(CMSG_RAW_MOUSE_LEFT_BUTTON_UP, 200, 200, 2020));
	REQUIRE(out.size() == 1);
	CHECK(out[0].type() == CMSG_RAW_MOUSE_LEFT_BUTTON_UP);
}

TEST_CASE("formation chord: S-4464 explicitly reports the remaining reconstructed layout and commands")
{
	const std::string expected =
		"[S-4464] Two-button formation: BFME2's first-press buffering, <5-pixel / <500-ms chord and first-press anchor are ported "
		"(BFME2 RVAs 0x431978 / 0x431BDD / 0x431C9D / 0x432038); RotWK parity is unverified (S-001). "
		"The FormationAssistant templates, selection criteria, native preview decals and MSG_DO_MOVETO_FORMATION template handling are not ported: "
		"the preview grid and individual move-and-orientate orders remain an OpenBFME reconstruction.";
	CHECK(FormationPlacement::stopLine() == expected);
	const auto stops = HudInput::acceptanceStops();
	CHECK(std::count(stops.begin(), stops.end(), expected) == 1);
}

TEST_CASE("formation chord: ordinary clicks and box drags still pass through the complete HUD pipeline")
{
	combattest::CombatWorld world;
	FlatView view;
	MetaMap meta;
	CommandList commands;
	Object *unit = world.unit("Slasher", 'A', 100, 100);
	world.logic->players().setLocalPlayer(unit->getControllingPlayer());
	HudInput input(*world.logic, world.ai.get(), view, commands, MouseSettings{15, 5, 150}, meta);
	input.commandTranslator().setUseAlternateMouse(true);
	auto down = [&](int x, int y, int time) {
		input.mouseButton(HudInput::Button::Left, true, x, y, 0, time);
		input.update();
	};
	auto up = [&](int x, int y, int time) {
		input.mouseButton(HudInput::Button::Left, false, x, y, 0, time);
		input.update();
	};
	down(100, 100, 1000);
	up(100, 100, 1020);
	CHECK(input.ui().selected() == std::vector<ObjectID>{unit->getID()});
	input.ui().deselectAll(false);
	down(70, 70, 2000);
	input.mouseMove(130, 130, 0);
	input.update();
	up(130, 130, 2200);
	CHECK(input.ui().selected() == std::vector<ObjectID>{unit->getID()});
	CHECK_FALSE(input.ui().formationActive);
}

TEST_CASE("formation chord: after a failed drag the next down starts a new wait even while the other button is held")
{
	Rig r;
	REQUIRE(r.send(button(CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 200, 200, 1000)).empty());
	CHECK(r.send(button(CMSG_RAW_MOUSE_POSITION, 205, 200, 1020)).size() == 2);
	REQUIRE(r.send(button(CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN, 300, 300, 1040)).empty());
	CHECK(r.send(button(CMSG_RAW_MOUSE_LEFT_BUTTON_UP, 300, 300, 1060)).size() == 1);
	CHECK(r.send(button(CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN, 300, 300, 1080)).empty());
	CHECK(r.ui.formationActive);
	REQUIRE(r.ui.formationSlots.size() == 1);
	CHECK(r.ui.formationSlots[0].position.x == 300.f);
	CHECK(r.ui.formationSlots[0].position.y == 300.f);
}
