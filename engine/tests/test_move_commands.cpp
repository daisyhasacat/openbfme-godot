// OpenBFME unit tests: the player commands of movement through the lockstep command path (lane MOVE-1): GameMessage -> command list -> dispatcher -> AI.

#include "doctest.h"
#include "MoveTestUtil.h"

#include <algorithm>

using namespace movetest;

TEST_CASE("commands: a selection and a move message move the selected units to the one point, each adjusted beside the others (RW 0x75748C, MoveToGroupOrder)")
{
	MoveWorld mw;
	mw.buildMap();
	const int alice = mw.playerIndex("Alice");
	Object *a = mw.spawn("Walker", 305.0f, 305.0f);
	Object *b = mw.spawn("Walker", 305.0f, 345.0f);
	Object *c = mw.spawn("Walker", 305.0f, 385.0f);
	Object *other = mw.spawn("Walker", 505.0f, 305.0f); // not selected
	mw.frames(2);
	const float otherX = other->getPosition()->x;
	mw.select(alice, { a, b, c });
	mw.moveTo(alice, 705.0f, 705.0f);
	CHECK(mw.commands.messages().size() == 2);
	CHECK(a->getAIUpdateInterface()->isIdle());       // nothing happens until the logic frame runs the command list
	mw.frames(1);
	CHECK(mw.commands.messages().size() == 0);                   // the list was processed and reset
	CHECK(mw.aiCommands.stats().moves == 1);
	CHECK(AICommands::selection(*mw.logic, alice).size() == 3);
	CHECK_FALSE(a->getAIUpdateInterface()->isIdle());
	CHECK_FALSE(b->getAIUpdateInterface()->isIdle());
	CHECK_FALSE(c->getAIUpdateInterface()->isIdle());
	CHECK(other->getAIUpdateInterface()->isIdle());
	mw.frames(150);
	CHECK(a->getAIUpdateInterface()->isIdle());
	CHECK(b->getAIUpdateInterface()->isIdle());
	CHECK(c->getAIUpdateInterface()->isIdle());
	// lane MOVE-3: RotWK's MSG_DO_MOVETO hands the group to the group manager's MoveToGroupOrder (RW 0x77BAAA -> 0x75748C): every unit, in group order, gets
	// the point adjusted by adjustDestination against the goals reserved before it (RW 0x94DF97): a takes the point, b and c the nearest free cells (the ZH
	// group move kept each unit's offset from the nearest one; that is now MSG_DO_FORCEMOVETO's)
	const Coord3D goal{ 705.0f, 705.0f, 0.0f };
	CHECK(dist2d(*a->getPosition(), goal) < 25.0f);
	CHECK(dist2d(*b->getPosition(), goal) < 40.0f);
	CHECK(dist2d(*c->getPosition(), goal) < 40.0f);
	CHECK(dist2d(*a->getPosition(), *b->getPosition()) > 5.0f); // not stacked on one cell
	CHECK(dist2d(*a->getPosition(), *c->getPosition()) > 5.0f);
	CHECK(dist2d(*b->getPosition(), *c->getPosition()) > 5.0f);
	CHECK(other->getPosition()->x == otherX);
}

TEST_CASE("commands: a stop message idles the group; a new selection without createNew adds; remove takes units out")
{
	MoveWorld mw;
	mw.buildMap();
	const int alice = mw.playerIndex("Alice");
	Object *a = mw.spawn("Walker", 105.0f, 105.0f);
	Object *b = mw.spawn("Walker", 105.0f, 205.0f);
	mw.frames(2);
	mw.select(alice, { a });
	mw.select(alice, { b }, false);
	mw.frames(1);
	CHECK(AICommands::selection(*mw.logic, alice).size() == 2);
	mw.moveTo(alice, 905.0f, 105.0f);
	mw.frames(8);
	CHECK(a->getPosition()->x > 150.0f);
	CHECK(b->getPosition()->x > 150.0f);
	GameMessage stop(MSG_DO_STOP, alice);
	mw.send(stop);
	mw.frames(1);
	CHECK(a->getAIUpdateInterface()->isIdle());
	CHECK(b->getAIUpdateInterface()->isIdle());
	const float ax = a->getPosition()->x;
	mw.frames(5);
	CHECK(a->getPosition()->x == ax);
	GameMessage remove(MSG_REMOVE_FROM_SELECTED_GROUP, alice);
	remove.appendObjectIDArgument(a->getID());
	mw.send(remove);
	mw.frames(1);
	REQUIRE(AICommands::selection(*mw.logic, alice).size() == 1);
	CHECK(AICommands::selection(*mw.logic, alice)[0] == b->getID());
	GameMessage destroy(MSG_DESTROY_SELECTED_GROUP, alice);
	mw.send(destroy);
	mw.frames(1);
	CHECK(AICommands::selection(*mw.logic, alice).empty());
}

TEST_CASE("commands: MSG_ADD_WAYPOINT appends to the path the units follow")
{
	MoveWorld mw;
	mw.buildMap();
	const int alice = mw.playerIndex("Alice");
	Object *a = mw.spawn("Walker", 105.0f, 105.0f);
	mw.frames(2);
	mw.select(alice, { a });
	mw.moveTo(alice, 505.0f, 105.0f, MSG_ADD_WAYPOINT);
	mw.moveTo(alice, 505.0f, 505.0f, MSG_ADD_WAYPOINT);
	mw.frames(1);
	CHECK(a->getAIUpdateInterface()->currentStateId() == (unsigned)AI_FOLLOW_PATH);
	CHECK(mw.aiCommands.stats().waypoints == 2);
	bool passedFirst = false;
	for (int f = 0; f < 200 && !a->getAIUpdateInterface()->isIdle(); ++f)
	{
		mw.frames(1);
		if (dist2d(*a->getPosition(), Coord3D{ 505.0f, 105.0f, 0.0f }) < 30.0f)
		{
			passedFirst = true;
		}
	}
	CHECK(passedFirst);
	CHECK(dist2d(*a->getPosition(), Coord3D{ 505.0f, 505.0f, 0.0f }) < 15.0f);
}

TEST_CASE("commands: a formation move message ends with the units facing the angle; a move-and-orientate message orders one object")
{
	MoveWorld mw;
	mw.buildMap();
	const int alice = mw.playerIndex("Alice");
	Object *a = mw.spawn("Walker", 105.0f, 105.0f);
	Object *b = mw.spawn("Walker", 305.0f, 105.0f);
	mw.frames(2);
	mw.select(alice, { a });
	GameMessage fm(MSG_DO_MOVETO_FORMATION, alice);
	fm.appendLocationArgument(Coord3D{ 105.0f, 405.0f, 0.0f });
	fm.appendRealArgument(3.14159265f);
	fm.appendIntegerArgument(0);
	fm.appendBooleanArgument(false);
	mw.send(fm);
	GameMessage mo(MSG_DO_MOVE_AND_ORIENTATE_OBJECTTO, alice);
	mo.appendObjectIDArgument(b->getID());
	mo.appendLocationArgument(Coord3D{ 305.0f, 405.0f, 0.0f });
	mo.appendRealArgument(1.5707963f);
	mw.send(mo);
	mw.frames(80);
	CHECK(a->getAIUpdateInterface()->isIdle());
	CHECK(b->getAIUpdateInterface()->isIdle());
	CHECK(dist2d(*a->getPosition(), Coord3D{ 105.0f, 405.0f, 0.0f }) < 15.0f);
	CHECK(dist2d(*b->getPosition(), Coord3D{ 305.0f, 405.0f, 0.0f }) < 15.0f);
	CHECK(std::fabs(a->getOrientation()) > 3.0f);                        // facing pi (either sign of the wrap)
	CHECK(b->getOrientation() == doctest::Approx(1.5707963f).epsilon(0.02));
	CHECK(mw.aiCommands.stats().formationMoves == 1);
	CHECK(mw.aiCommands.stats().orientMoves == 1);
}

TEST_CASE("commands: malformed messages are rejected and counted, an unknown type is reported unhandled, a second handler for a type is an error")
{
	MoveWorld mw;
	mw.buildMap();
	const int alice = mw.playerIndex("Alice");
	GameMessage noArg(MSG_DO_MOVETO, alice);
	mw.send(noArg);
	GameMessage badPlayer(MSG_DO_MOVETO, 999);
	badPlayer.appendLocationArgument(Coord3D{ 1, 1, 0 });
	mw.send(badPlayer);
	GameMessage unknown(MSG_DO_SPECIAL_POWER, alice);
	mw.send(unknown);
	mw.frames(1);
	CHECK(mw.aiCommands.stats().rejected == 2);
	CHECK(mw.dispatcher.unhandled().at(MSG_DO_MOVETO) == 2);
	CHECK(mw.dispatcher.unhandled().at(MSG_DO_SPECIAL_POWER) == 1);
	CHECK(mw.dispatcher.handlerLane(MSG_DO_MOVETO) == "MOVE-1");
	AICommands again;
	CHECK_THROWS_AS(again.registerHandlers(mw.dispatcher), std::logic_error);
}

TEST_CASE("commands: the same messages in two worlds give the same state hash in every frame")
{
	auto run = [](std::vector<std::uint32_t> &hashes) {
		MoveWorld mw;
		mw.buildMap();
		const int alice = mw.playerIndex("Alice");
		std::vector<Object *> us;
		for (int i = 0; i < 6; ++i)
		{
			us.push_back(mw.spawn("Walker", 105.0f + 40.0f * (float)i, 105.0f + 20.0f * (float)(i % 3)));
		}
		mw.frames(2);
		mw.select(alice, us);
		mw.moveTo(alice, 605.0f, 605.0f);
		for (int f = 0; f < 80; ++f)
		{
			mw.frames(1);
			hashes.push_back(mw.logic->computeStateHash());
		}
	};
	std::vector<std::uint32_t> h1, h2;
	run(h1);
	// a different heap layout: allocate and keep a few blocks so the second world's objects are not at the addresses of the first's
	std::vector<std::unique_ptr<char[]>> pad;
	for (int i = 1; i < 50; ++i)
	{
		pad.emplace_back(new char[(size_t)i * 37]);
	}
	run(h2);
	REQUIRE(h1.size() == h2.size());
	for (size_t i = 0; i < h1.size(); ++i)
	{
		INFO("frame " << i);
		CHECK(h1[i] == h2[i]);
	}
	CHECK(h1.front() != h1.back()); // the state really changes
}

#include "GameClient/HudContext.h"
#include "GameClient/MessageStream/CommandXlat.h"
#include "GameLogic/BuildPlacement.h"

TEST_CASE("reachability: closed walls refuse ground orders and builder placement")
{
	const char objects[] =
		"Object ReachWallX\n KindOf = STRUCTURE IMMOBILE\n Geometry = BOX\n GeometryMajorRadius = 215\n GeometryMinorRadius = 15\n GeometryHeight = 50\n End\n"
		"Object ReachWallY\n KindOf = STRUCTURE IMMOBILE\n Geometry = BOX\n GeometryMajorRadius = 15\n GeometryMinorRadius = 215\n GeometryHeight = 50\n End\n"
		"ChildObject ReachBuilder Walker\n KindOf = SELECTABLE DOZER\n End\n";
	MoveWorld mw(100, 100, objects);
	mw.spawn("ReachWallX", 500, 300);
	mw.spawn("ReachWallX", 500, 700);
	mw.spawn("ReachWallY", 300, 500);
	Object *wall = mw.spawn("ReachWallY", 700, 500);
	Object *unit = mw.spawn("ReachBuilder", 150, 150);
	Object *trapped = mw.spawn("ReachBuilder", 500, 500);
	mw.buildMap();
	mw.frames(2);
	mw.logic->players().setLocalPlayer(unit->getControllingPlayer());
	InGameUI ui;
	ui.selectObject(unit->getID());
	PinholeView view;
	MessageStream stream;
	HudContext context{*mw.logic, mw.ai.get(), view, ui, stream, nullptr, {}};
	CommandTranslator translator(context);
	ClientMessage selected(MSG_CREATE_SELECTED_GROUP);
	translator.translate(selected);
	const Coord3D inside{500,500,0}, outside{150,250,0};
	CHECK(translator.evaluateContextCommand(nullptr, &inside, EvaluateType::DoHint) == 0);
	CHECK(ui.cursor() == MouseCursorName::GenericInvalid);
	CHECK(translator.evaluateContextCommand(nullptr, &inside, EvaluateType::DoCommand) == 0);
	CHECK(stream.pending() == 0);
	translator.issueRadarMove(inside);
	CHECK(stream.pending() == 0);
	CHECK(translator.evaluateContextCommand(nullptr, &outside, EvaluateType::DoHint) == MSG_DO_MOVETO);
	CHECK(ui.cursor() == MouseCursorName::Move);
	const ThingTemplate *build = mw.w.things.findTemplate("Block");
	REQUIRE(build);
	CHECK(BuildPlacement::isLocationLegalToBuild(*mw.logic, inside, *build, 0, LLF_CLEAR_PATH, unit, unit->getControllingPlayer()) == LBC_NO_CLEAR_PATH);
	CHECK(BuildPlacement::isLocationLegalToBuild(*mw.logic, outside, *build, 0, LLF_CLEAR_PATH, unit, unit->getControllingPlayer()) == LBC_OK);
	// Leaving a sealed base must be refused as well as entering it. Otherwise
	// a normal click can feed the mover's closest-point fallback at the wall.
	ui.deselectObject(unit->getID());
	ui.selectObject(trapped->getID());
	CHECK(translator.evaluateContextCommand(nullptr, &outside, EvaluateType::DoHint) == 0);
	CHECK(ui.cursor() == MouseCursorName::GenericInvalid);
	CHECK(translator.evaluateContextCommand(nullptr, &outside, EvaluateType::DoCommand) == 0);
	translator.issueRadarMove(outside);
	CHECK(stream.pending() == 0);
	// Removing a wall footprint (the same pathfinder update used by open gates)
	// must restore both commands and placement without stale client state.
	mw.ai->removeObjectFromPathfindMap(*wall);
	mw.frames(2);
	CHECK(translator.evaluateContextCommand(nullptr, &outside, EvaluateType::DoHint) == MSG_DO_MOVETO);
	CHECK(ui.cursor() == MouseCursorName::Move);
	ui.deselectObject(trapped->getID());
	ui.selectObject(unit->getID());
	CHECK(translator.evaluateContextCommand(nullptr, &inside, EvaluateType::DoHint) == MSG_DO_MOVETO);
	CHECK(ui.cursor() == MouseCursorName::Move);
	CHECK(BuildPlacement::isLocationLegalToBuild(*mw.logic, inside, *build, 0, LLF_CLEAR_PATH, unit, unit->getControllingPlayer()) == LBC_OK);

}

TEST_CASE("commands spread: 100 ground units reserve a growing grid around the destination, including repeated orders")
{
	MoveWorld mw(200, 200);
	std::vector<Object *> units;
	for (int i = 0; i != 100; ++i) units.push_back(mw.spawn("Walker", 105.0f + 30.0f * (i % 10), 105.0f + 30.0f * (i / 10)));
	mw.buildMap();
	mw.frames(2);
	const int player = mw.playerIndex("Alice");
	mw.select(player, units);
	std::vector<Coord3D> previous;
	for (int pass = 0; pass != 2; ++pass)
	{
		mw.moveTo(player, 1405, 1405, pass == 0 ? MSG_DO_MOVETO : MSG_DO_ATTACKMOVETO);
		mw.frames(1);
		std::set<float> xs, ys;
		std::set<std::pair<float, float>> cells;
		std::vector<Coord3D> goals;
		for (Object *o : units)
		{
			Coord3D goal;
			REQUIRE(mw.ai->pathfinder().goalPosition(mw.ai->adapterFor(*o), &goal));
			xs.insert(goal.x); ys.insert(goal.y); cells.emplace(goal.x, goal.y);
			goals.push_back(goal);
		}
		CHECK(cells.size() == 100);
		CHECK(xs.size() == 10);
		CHECK(ys.size() == 10);
		// Compact spacing is clamped to one 10-unit cell: ten slots span 9 * 10.
		CHECK(*xs.rbegin() - *xs.begin() == 90.0f);
		CHECK(*ys.rbegin() - *ys.begin() == 90.0f);
		// Sharing applies only during this selection's synchronous order. The
		// ordinary reservation test must reject another unit's occupied goal.
		int radius;
		bool center;
		auto &adapter = mw.ai->adapterFor(*units[1]);
		mw.ai->pathfinder().getRadiusAndCenter(&adapter, radius, center);
		ICoord2D cell;
		mw.ai->pathfinder().worldToCell(&goals[0], center, &cell);
		CHECK_FALSE(mw.ai->pathfinder().checkDestination(&adapter, cell.x, cell.y, LAYER_GROUND, radius, center));
		if (pass == 1)
			for (size_t i = 0; i != goals.size(); ++i)
			{
				CHECK(goals[i].x == previous[i].x);
				CHECK(goals[i].y == previous[i].y);
			}
		previous = std::move(goals);
	}
}

TEST_CASE("commands spread: battalions overlap their envelopes while retaining distinct centres")
{
	MoveWorld mw(300, 300);
	std::vector<Object *> units;
	for (int i = 0; i != 16; ++i) units.push_back(mw.spawn("Horde", 305.0f + 150.0f * (i % 4), 305.0f + 150.0f * (i / 4)));
	mw.buildMap();
	mw.frames(2);
	const int player = mw.playerIndex("Alice");
	mw.select(player, units);
	mw.moveTo(player, 2005, 2005);
	mw.frames(1);
	std::set<float> xs, ys;
	std::set<std::pair<float, float>> cells;
	for (Object *o : units)
	{
		Coord3D goal;
		REQUIRE(mw.ai->pathfinder().goalPosition(mw.ai->adapterFor(*o), &goal));
		xs.insert(goal.x); ys.insert(goal.y); cells.emplace(goal.x, goal.y);
	}
	CHECK(cells.size() == 16);
	CHECK(xs.size() == 4);
	CHECK(ys.size() == 4);
	// The old 120-unit spacing is scaled by 1/sqrt(10); snapped
	// centres are 40 units apart here and span 120 instead of 360.
	CHECK(*xs.rbegin() - *xs.begin() == 120.0f);
	CHECK(*ys.rbegin() - *ys.begin() == 120.0f);
}

TEST_CASE("commands spread: edge clicks shift the whole layout inside the map")
{
	MoveWorld mw;
	std::vector<Object *> units;
	for (int i = 0; i != 16; ++i) units.push_back(mw.spawn("Walker", 105.0f + 30.0f * (i % 4), 105.0f + 30.0f * (i / 4)));
	mw.buildMap();
	mw.frames(2);
	const int player = mw.playerIndex("Alice");
	mw.select(player, units);
	mw.moveTo(player, 995, 995);
	mw.frames(1);
	std::set<float> xs, ys;
	std::set<std::pair<float, float>> cells;
	for (Object *o : units)
	{
		Coord3D goal;
		REQUIRE(mw.ai->pathfinder().goalPosition(mw.ai->adapterFor(*o), &goal));
		xs.insert(goal.x); ys.insert(goal.y); cells.emplace(goal.x, goal.y);
		CHECK(mw.ai->pathfinder().clientSafeQuickDoesPathExist(o->getAIUpdateInterface()->locomotorInfo(), o->getPosition(), &goal));
	}
	CHECK(cells.size() == 16);
	CHECK(*xs.rbegin() - *xs.begin() == 30.0f);
	CHECK(*ys.rbegin() - *ys.begin() == 30.0f);
	// Last row is cell 98, leaving room for the footprint at the cell-99 edge.
	CHECK(*xs.rbegin() == 980.5f);
	CHECK(*ys.rbegin() == 980.5f);
}

TEST_CASE("commands spread: sealed walls keep units in reachable slots; opening an exit permits the spread order")
{
	const char walls[] =
		"Object SpreadWallX\n KindOf = STRUCTURE IMMOBILE\n Geometry = BOX\n GeometryMajorRadius = 215\n GeometryMinorRadius = 15\n End\n"
		"Object SpreadWallY\n KindOf = STRUCTURE IMMOBILE\n Geometry = BOX\n GeometryMajorRadius = 15\n GeometryMinorRadius = 215\n End\n";
	MoveWorld mw(100, 100, walls);
	mw.spawn("SpreadWallX", 500, 300);
	mw.spawn("SpreadWallX", 500, 700);
	mw.spawn("SpreadWallY", 300, 500);
	Object *gate = mw.spawn("SpreadWallY", 700, 500);
	std::vector<Object *> units;
	std::vector<Coord3D> starts;
	for (int i = 0; i != 16; ++i)
	{
		Object *o = mw.spawn("Walker", 400.5f + 30.0f * (i % 4), 400.5f + 30.0f * (i / 4));
		units.push_back(o);
		starts.push_back(*o->getPosition());
	}
	mw.buildMap();
	mw.frames(2);
	const int player = mw.playerIndex("Alice");
	mw.select(player, units);
	mw.moveTo(player, 850, 850);
	mw.frames(1);
	for (size_t i = 0; i != units.size(); ++i)
	{
		Coord3D goal;
		REQUIRE(mw.ai->pathfinder().goalPosition(mw.ai->adapterFor(*units[i]), &goal));
		CHECK(goal.x == starts[i].x);
		CHECK(goal.y == starts[i].y);
	}
	mw.ai->removeObjectFromPathfindMap(*gate);
	mw.frames(2);
	mw.moveTo(player, 850, 850);
	mw.frames(1);
	std::set<std::pair<float,float>> goals;
	for (Object *o : units)
	{
		Coord3D goal;
		REQUIRE(mw.ai->pathfinder().goalPosition(mw.ai->adapterFor(*o), &goal));
		CHECK(mw.ai->pathfinder().clientSafeQuickDoesPathExist(o->getAIUpdateInterface()->locomotorInfo(), o->getPosition(), &goal));
		// tolerance: the entire four-by-four target grid lies beyond the ring's x=700 wall.
		CHECK(goal.x > 700.0f);
		goals.emplace(goal.x, goal.y);
	}
	CHECK(goals.size() == 16);
}

TEST_CASE("commands spread: repeated large orders produce the same simulation hashes")
{
	auto run = [] {
		MoveWorld mw(150, 150);
		std::vector<Object *> units;
		for (int i = 0; i != 25; ++i) units.push_back(mw.spawn("Walker", 105.0f + 30.0f * (i % 5), 105.0f + 30.0f * (i / 5)));
		mw.buildMap();
		mw.frames(2);
		const int player = mw.playerIndex("Alice");
		mw.select(player, units);
		mw.moveTo(player, 1005, 1005);
		std::vector<std::uint32_t> hashes;
		for (int i = 0; i != 40; ++i)
		{
			if (i == 20) mw.moveTo(player, 1005, 205, MSG_DO_ATTACKMOVETO);
			mw.frames(1);
			hashes.push_back(mw.logic->computeStateHash());
		}
		return hashes;
	};
	const auto first = run(), second = run();
	CHECK(first == second);
	CHECK(first.front() != first.back());
}
