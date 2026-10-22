// OpenBFME. GARRISON-1: an input-driven HUD test on a retail map. A click selects a Gondor archer horde, the alternate setup's right click on a Gondor battle tower is
// the context command MSG_ENTER (ActionManager::canEnterObject); the order travels the command list to the logic and the archers garrison the tower. Needs the retail
// install.

#include "HudTestUtil.h"
#include "GameClient/HudObjects.h"

#include "GameLogic/AI/GarrisonCommands.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/Contain/GarrisonContainRuntime.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"

#include <set>

using namespace hudtest;

namespace
{
size_t logCountOf(const HudInput &in, const std::string &prefix)
{
	size_t n = 0;
	for (const std::string &l : in.messageLog())
	{
		n += l.compare(0, prefix.size(), prefix) == 0 ? 1u : 0u;
	}
	return n;
}
} // namespace

TEST_CASE("garrison hud: selecting an archer horde and clicking a Gondor battle tower is MSG_ENTER; the archers garrison it through the command list")
{
	if (!haveWorld("garrison hud"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	float mx = 0, my = 0;
	REQUIRE(r.logic().terrain() != nullptr);
	REQUIRE(r.logic().terrain()->getExtent(0, mx, my));
	const Coord3D c0 = r.freeSpot(mx * 0.5f, my * 0.5f, 300.0f);
	Object *tower = r.make("GondorKeep", c0.x, c0.y);
	Object *archers = r.make("GondorArcherHorde", c0.x - 160.0f, c0.y);
	r.lookAt(Coord3D{ c0.x - 80.0f, c0.y, 0.0f }, 420.0f, 520.0f);
	r.frame(3);
	REQUIRE(r.local != nullptr);
	const ICoord2D pa = r.screenOf({ c0.x - 160.0f, c0.y, 6.0f });
	r.leftClick(pa.x, pa.y);
	r.frame(1);
	REQUIRE(r.local->selection().size() == 1);
	CHECK(r.local->selection()[0] == archers->getID());
	r.input->commandTranslator().setUseAlternateMouse(true);
	const ICoord2D pt = r.screenOf({ c0.x, c0.y, 40.0f });
	r.rightClick(pt.x, pt.y);
	CHECK(logCountOf(*r.input, "MSG_ENTER") == 1);
	CHECK(logCountOf(*r.input, "MSG_DO_MOVETO") == 0);
	r.frame(1);
	CHECK(r.game->dispatch().unhandled().count(MSG_ENTER) == 0);
	size_t frames = 0;
	HordeContainInterface *h = archers->getContain()->getHordeContainInterface();
	while (frames < 600 && !(archers->getContainedBy() == tower && h->allMembersEntered()))
	{
		r.frame(1);
		++frames;
	}
	CHECK(archers->getContainedBy() == tower);
	CHECK(h->isGarrisoned());
	CHECK(tower->getContain()->getContainCount() == 1u);
	// with the W3D assets the tower has its ARROW_ garrison points (PassengerBonePrefix, RW 0x87C2B7)
	const GarrisonContain *g = dynamic_cast<const GarrisonContain *>(tower->getContain());
	REQUIRE(g != nullptr);
	CHECK(g->pointsLoaded());
	std::printf("  info: HUD garrison order: inside after %zu frames, garrison points %d / %d / %d\n", frames, g->pointCount(0), g->pointCount(1), g->pointCount(2));
}

TEST_CASE("ring delivery hud: right-clicking a fortress with a selected ring carrier issues enter and delivers the ring")
{
	if (!haveWorld("ring delivery hud")) return;
	Rig r(shared());
	float mx = 0, my = 0;
	REQUIRE(r.logic().terrain() != nullptr);
	REQUIRE(r.logic().terrain()->getExtent(0, mx, my));
	const Coord3D c = r.freeSpot(mx * 0.5f, my * 0.5f, 300.0f);
	Object *fortress = r.make("MenFortressCitadel", c.x, c.y);
	Object *carrier = r.make("GondorArcherHorde", c.x - 200.0f, c.y);
	r.make("TheDroppedRing", c.x - 200.0f, c.y);
	r.lookAt(Coord3D{ c.x - 100.0f, c.y, 0.0f }, 420.0f, 600.0f);
	r.frame(3);
	REQUIRE(carrier->testStatus((unsigned)CombatNames::status("HOLDING_THE_RING")));
	const ICoord2D pc = r.screenOf({ c.x - 200.0f, c.y, 6.0f });
	r.leftClick(pc.x, pc.y);
	r.frame(1);
	REQUIRE(r.local->selection().size() == 1);
	REQUIRE(r.local->selection()[0] == carrier->getID());
	r.input->commandTranslator().setUseAlternateMouse(true);
	const ICoord2D pf = r.screenOf({ c.x, c.y, 40.0f });
	r.move(pf.x, pf.y);
	CHECK(r.input->ui().cursor() == "DeliverRing");
	r.rightClick(pf.x, pf.y);
	CHECK(logCountOf(*r.input, "MSG_ENTER") == 1);
	CHECK(logCountOf(*r.input, "MSG_DO_MOVETO") == 0);
	for (int i = 0; i < 600 && !fortress->hasUpgrade("Upgrade_FortressRingHero"); ++i) r.frame(1);
	CHECK(fortress->hasUpgrade("Upgrade_FortressRingHero"));
	CHECK_FALSE(carrier->testStatus((unsigned)CombatNames::status("HOLDING_THE_RING")));
}

TEST_CASE("treasure hud: an enemy-owned chest is a move destination rather than an attack target")
{
	if (!haveWorld("treasure hud")) return;
	Rig r(shared());
	float mx = 0, my = 0;
	REQUIRE(r.logic().terrain()->getExtent(0, mx, my));
	const Coord3D c = r.freeSpot(mx * 0.5f, my * 0.5f, 300.0f);
	Player *enemy = r.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy != nullptr);
	Object *unit = r.make("GondorArcherHorde", c.x - 200.0f, c.y);
	Object *chest = r.make("TreasureChest1", c.x, c.y, enemy);
	const ObjectID chestID = chest->getID();
	r.lookAt(Coord3D{ c.x - 100.0f, c.y, 0.0f }, 420.0f, 600.0f);
	r.frame(3);
	const ICoord2D pu = r.screenOf({ c.x - 200.0f, c.y, 6.0f });
	r.leftClick(pu.x, pu.y);
	r.frame();
	REQUIRE(r.local->selection().size() == 1);
	REQUIRE(r.local->selection()[0] == unit->getID());
	r.input->commandTranslator().setUseAlternateMouse(true);
	const ICoord2D pc = r.screenOf({ c.x, c.y, 6.0f });
	REQUIRE(HudObjects::pickObject(r.input->context(), pc) == chest);
	r.move(pc.x, pc.y);
	CHECK(r.input->ui().cursor() == "Move");
	r.rightClick(pc.x, pc.y);
	CHECK(logCountOf(*r.input, "MSG_DO_MOVETO") == 1);
	CHECK(logCountOf(*r.input, "MSG_DO_ATTACK_OBJECT") == 0);
	for (int i = 0; i < 600 && r.logic().findObjectByID(chestID); ++i) r.frame();
	CHECK(r.logic().findObjectByID(chestID) == nullptr);
}

TEST_CASE("treasure retail: lair treasure uses its configured formation spread")
{
	if (!haveWorld("treasure retail")) return;
	Rig r(shared());
	const auto context = shared().world->enterContext();
	float mx = 0, my = 0;
	REQUIRE(r.logic().terrain()->getExtent(0, mx, my));
	const Coord3D c = r.freeSpot(mx * 0.5f, my * 0.5f, 300.0f);
	Object *source = r.make("GondorFighter", c.x, c.y);
	const ObjectCreationList *ocl = TheObjectCreationListStore->findObjectCreationList("OCL_SpawnLairTreasure");
	REQUIRE(ocl != nullptr);
	const auto made = ocl->create(r.logic(), source, *source->getPosition());
	REQUIRE(made.size() == 4);
	std::set<std::pair<float, float>> positions;
	for (Object *chest : made)
	{
		const Coord3D p = *chest->getPosition();
		const float dx = p.x - c.x, dy = p.y - c.y;
		// tolerance: retail INI's inner radius is sampled from 50..60, outer radius is 80.
		CHECK(dx * dx + dy * dy >= 50.0f * 50.0f);
		// tolerance: retail INI's configured maximum formation distance.
		CHECK(dx * dx + dy * dy <= 80.0f * 80.0f);
		positions.insert({p.x, p.y});
	}
	CHECK(positions.size() == 4);
}
