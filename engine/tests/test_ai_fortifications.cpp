// OpenBFME. GPL-3.0. Synthetic siege scenarios; no retail archives required.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/Thing/ThingFactory.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/Combat/TargetFinder.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/SkirmishAI/AIFortification.h"
#include "GameLogic/SkirmishAI/AIBaseBuilder.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "GameLogic/WallBalance.h"
using namespace combattest;
namespace
{
const char data[] = R"ini(
Armor SiegeProofExceptSiege
 Armor = DEFAULT 0%
 Armor = SIEGE 100%
End
Weapon BreachHit
 AttackRange = 150
 DelayBetweenShots = 1000
 DamageNugget
  Damage = 25
  DamageType = SIEGE
  DeathType = NORMAL
 End
End
Weapon BreachProjectile
 AttackRange = 200
 ProjectileNugget
  ProjectileTemplateName = Arrow
  WarheadTemplateName = BreachHit
 End
End
ChildObject BreachEngine Swordsman
 KindOf = SELECTABLE CAN_ATTACK CAN_ATTACK_WALLS SIEGEENGINE
 CommandPoints = 5
 BuildCost = 100
 WeaponSet
  Conditions = None
  Weapon = PRIMARY BreachHit
 End
End
ChildObject BreachCatapult BreachEngine
 WeaponSet
  Conditions = None
  Weapon = PRIMARY BreachProjectile
 End
End
ChildObject WallAttacker BreachEngine
 KindOf = SELECTABLE CAN_ATTACK CAN_ATTACK_WALLS
End
ChildObject UselessSiege Swordsman
 KindOf = SELECTABLE CAN_ATTACK CAN_ATTACK_WALLS SIEGEENGINE
End
ChildObject MixedEngine BreachEngine
 WeaponSet
  Conditions = None
  Weapon = PRIMARY SwordWeapon
  Weapon = SECONDARY BreachHit
 End
End
Object BreachWall
 KindOf = STRUCTURE IMMOBILE WALL_SEGMENT SELECTABLE
 Geometry = BOX
 GeometryMajorRadius = 5
 GeometryMinorRadius = 40
 GeometryHeight = 25
 Body = ActiveBody Body
  MaxHealth = 500
 End
End
ChildObject BreachFortress BreachWall
 KindOf = STRUCTURE IMMOBILE COMMANDCENTER DOZER_FACTORY SELECTABLE
End
ChildObject ArmoredFortress BreachFortress
 ArmorSet
  Conditions = None
  Armor = SiegeProofExceptSiege
 End
End
Object BreachWorkshop
 KindOf = STRUCTURE IMMOBILE FS_FACTORY SELECTABLE
 CommandSet = BreachWorkshopCommands
 Body = ActiveBody Body
  MaxHealth = 500
 End
 Behavior = ProductionUpdate Production
 End
End
)ini";

struct Scenario
{
	CombatWorld w{data};
	CommandStore commands;
	CommandStore *savedCommands = TheCommandStore;
	SkirmishAIStore store{w.w.keys};
	AISkirmishPlayer bot;
	Scenario()
	{
		w.combat().setAutoAcquireEnabled(false);
		Player *p = w.players.findPlayerWithName("Alice");
		p->setPlayerType(PLAYER_COMPUTER, true);
		bot.playerIndex = p->getPlayerIndex();
		bot.build.startPosition = {100, 500, 0};
		bot.store = &store;
		commands.setThingFactory(&w.w.things);
		TheCommandStore = &commands;
		store.registerBlocks(w.w.fx.env.blocks);
		w.w.fx.env.blocks.registerBlock("CommandButton", [](INI *ini) { CommandStore::parseCommandButtonDefinitionGlobal(ini); });
		w.w.fx.env.blocks.registerBlock("CommandSet", [](INI *ini) { CommandStore::parseCommandSetDefinitionGlobal(ini); });
		REQUIRE(w.w.load("CommandButton TrainBreach\n Command = UNIT_BUILD\n Object = BreachEngine\nEnd\n"
			"CommandSet BreachWorkshopCommands\n 1 = TrainBreach\nEnd\n"
			"ArmyDefinition TestArmy\n Side = Test\n ArmyMemberDefinition Engine\n Unit = BreachEngine\n"
			" PercentageOfArmyPhase1 = 0\n PercentageOfArmyPhase2 = 0\n PercentageOfArmyPhase3 = 0\n End\nEnd\n").empty());
		bot.army = store.findArmy("Test");
	}
	~Scenario() { TheCommandStore = savedCommands; }
	void restrict(int mode = 2)
	{
		auto &r = w.logic->settings().wallBalance;
		r.enabled = true;
		r.wallDamage = r.fortressDamage = mode;
	}
};
}

TEST_CASE("fortification AI: armor and wall policy reject futile attacks but permit effective weapons")
{
	Scenario s;
	Object *soldier = s.w.unit("Swordsman", 'A', 300, 500);
	Object *engine = s.w.unit("BreachEngine", 'A', 320, 500);
	Object *catapult = s.w.unit("BreachCatapult", 'A', 340, 500);
	Object *target = s.w.unit("ArmoredFortress", 'B', 600, 500);
	s.w.frames(2);
	CHECK_FALSE(soldier->getWeapons()->canAttackObject(*target, CMD_FROM_AI, false));
	CHECK(engine->getWeapons()->canAttackObject(*target, CMD_FROM_AI, false));
	CHECK(catapult->getWeapons()->canAttackObject(*target, CMD_FROM_AI, false));
	CHECK(AIFortification::findTarget(*s.w.logic, s.bot) == target);
	CHECK(AIFortification::canBreach(*s.w.logic, *catapult->getTemplate(), *target));
	CHECK_FALSE(AIFortification::canBreach(*s.w.logic, *soldier->getTemplate(), *target));
	Object *fake = s.w.unit("UselessSiege", 'A', 360, 500);
	Object *mixed = s.w.unit("MixedEngine", 'A', 380, 500);
	s.w.frames(2);
	CHECK_FALSE(AIFortification::canBreach(*s.w.logic, *fake->getTemplate(), *target));
	CHECK_FALSE(fake->getWeapons()->canAttackObject(*target, CMD_FROM_AI, false));
	CHECK(AIFortification::canBreach(*s.w.logic, *mixed->getTemplate(), *target));
	CHECK(mixed->getWeapons()->canAttackObject(*target, CMD_FROM_AI, false));
	target->getBodyModule()->setIndestructible(true);
	CHECK_FALSE(engine->getWeapons()->canAttackObject(*target, CMD_FROM_AI, false));
	CHECK_FALSE(AIFortification::canBreach(*s.w.logic, *engine->getTemplate(), *target));
	CHECK(AIFortification::findTarget(*s.w.logic, s.bot) == nullptr);
}

TEST_CASE("fortification AI: native damage remains available and siege-only settings use actual attacker kinds")
{
	Scenario s;
	Object *soldier = s.w.unit("Swordsman", 'A', 300, 500);
	Object *wallUnit = s.w.unit("WallAttacker", 'A', 320, 500);
	Object *engine = s.w.unit("BreachEngine", 'A', 340, 500);
	Object *wall = s.w.unit("BreachWall", 'B', 600, 500);
	s.w.frames(2);
	CHECK(soldier->getWeapons()->canAttackObject(*wall, CMD_FROM_AI, false));
	CHECK(AIFortification::findTarget(*s.w.logic, s.bot) == nullptr);
	s.restrict(1);
	CHECK_FALSE(soldier->getWeapons()->canAttackObject(*wall, CMD_FROM_AI, false));
	CHECK(wallUnit->getWeapons()->canAttackObject(*wall, CMD_FROM_AI, false));
	s.restrict(2);
	CHECK_FALSE(wallUnit->getWeapons()->canAttackObject(*wall, CMD_FROM_AI, false));
	CHECK(engine->getWeapons()->canAttackObject(*wall, CMD_FROM_AI, false));
	CHECK_FALSE(soldier->getWeapons()->canDamage(*soldier->getWeapons()->currentWeapon(), *wall));
}

TEST_CASE("fortification AI: breach outer walls, reserve capable units and retarget after a breach")
{
	Scenario s;
	s.restrict();
	Object *soldier = s.w.unit("Swordsman", 'A', 300, 500);
	Object *engine = s.w.unit("BreachEngine", 'A', 320, 500);
	Object *wall = s.w.unit("BreachWall", 'B', 600, 500);
	Object *fortress = s.w.unit("BreachFortress", 'B', 850, 500);
	s.w.frames(2);
	AITactic squad;
	squad.teams.emplace_back();
	squad.teams[0].members = {soldier->getID(), engine->getID()};
	s.bot.brain.tactics.push_back(squad);
	AIFortification::update(*s.w.logic, s.bot);
	CHECK(s.bot.brain.fortificationTarget == wall->getID());
	CHECK(s.bot.brain.siegeUnits == std::set<ObjectID>{engine->getID()});
	CHECK(s.bot.brain.tactics[0].teams[0].members == std::vector<ObjectID>{soldier->getID()});
	CHECK(engine->getAIUpdateInterface()->currentVictimId() == wall->getID());
	// This exercises the real attack state and body damage, not only target selection.
	s.w.frames(100);
	CHECK(s.w.health(wall) < 500); // tolerance: at least one siege hit must land after crossing the flat approach
	s.w.logic->destroyObject(wall);
	s.w.frames(1);
	AIFortification::update(*s.w.logic, s.bot);
	CHECK(s.bot.brain.fortificationTarget == fortress->getID());
	CHECK(engine->getAIUpdateInterface()->currentVictimId() == fortress->getID());
	fortress->setTeam(s.w.teamA());
	AIFortification::update(*s.w.logic, s.bot);
	CHECK(s.bot.brain.fortificationTarget == INVALID_ID);
	CHECK(s.bot.brain.siegeUnits.empty());
	CHECK(engine->getAIUpdateInterface()->currentVictimId() == INVALID_ID);
}

TEST_CASE("fortification AI: peace time holds breach orders until the exact expiry")
{
	Scenario s;
	s.restrict();
	Object *engine = s.w.unit("BreachEngine", 'A', 100, 500);
	Object *fortress = s.w.unit("BreachFortress", 'B', 850, 500);
	s.w.logic->setPeaceTimeMinutes(5);
	s.w.frames(1499);
	AIFortification::update(*s.w.logic, s.bot);
	CHECK(s.bot.brain.siegeUnits.empty());
	CHECK(s.w.health(fortress) == 500);
	s.w.frames(1);
	AIFortification::update(*s.w.logic, s.bot);
	CHECK(s.bot.brain.siegeUnits == std::set<ObjectID>{engine->getID()});
	CHECK(engine->getAIUpdateInterface()->currentVictimId() == fortress->getID());
}

TEST_CASE("fortification AI: production requests useful siege even with zero composition share")
{
	Scenario s;
	s.restrict();
	Object *target = s.w.unit("BreachFortress", 'B', 850, 500);
	Object *factory = s.w.unit("BreachWorkshop", 'A', 100, 500);
	s.w.frames(2);
	s.bot.build.baseBuilderDisabled = true;
	s.bot.build.economy.disabled = true;
	AIBaseBuilder::update(*s.w.logic, s.bot);
	REQUIRE(s.bot.build.units.requests.size() == 1);
	CHECK(s.bot.build.units.requests[0]->templateName == "BreachEngine");
	CHECK(s.bot.build.units.requests[0]->priority == 1000.0f);
	CHECK(s.bot.build.units.requests[0]->state == 1);
	CHECK(factory->getProductionUpdate()->getProductionCount() == 1);
	CHECK(AIFortification::factoryCanBreach(*s.w.logic, *factory->getTemplate(), *target));
}

TEST_CASE("fortification AI: acquisition skips an immune fortress for an enemy it can damage")
{
	Scenario s;
	Object *soldier = s.w.unit("Swordsman", 'A', 300, 500);
	s.w.unit("ArmoredFortress", 'B', 400, 500);
	Object *enemy = s.w.unit("Dummy", 'B', 500, 500);
	s.w.frames(2);
	CHECK(s.w.combat().targets().findClosestEnemy(*soldier, 300, TargetFinder::ALLOW_STRUCTURES, CMD_FROM_AI) == enemy);
}

TEST_CASE("fortification AI: a later siege factory starts once and retains its planned build site")
{
	Scenario s;
	s.restrict();
	s.w.unit("BreachFortress", 'B', 850, 500);
	s.bot.build.economy.disabled = true;
	AIBaseInstance base;
	AIBaseSlot slot;
	slot.index = 2;
	auto workshop = std::make_shared<AIBuildRequest>();
	workshop->templateName = "BreachWorkshop";
	workshop->offset = {120, 200, 0};
	workshop->basePosition = s.bot.build.startPosition;
	slot.entries.push_back(workshop);
	base.slots.push_back(slot);
	s.bot.build.bases.push_back(base);
	AIBaseBuilder::update(*s.w.logic, s.bot);
	CHECK(s.bot.build.pending.size() == 1);
	CHECK(workshop->priority == 1000.0f);
	CHECK(workshop->starts == 1);
	CHECK(workshop->offset.x == 120);
	CHECK(workshop->offset.y == 200);
	s.bot.build.phase = 2;
	AIBaseBuilder::update(*s.w.logic, s.bot);
	CHECK(workshop->starts == 1);
	CHECK(s.bot.build.pending.size() == 1);
}

TEST_CASE("fortification AI: two capable engines satisfy demand and policy changes release the breach team")
{
	Scenario s;
	s.restrict();
	s.w.unit("BreachFortress", 'B', 850, 500);
	Object *a = s.w.unit("BreachEngine", 'A', 300, 500);
	Object *b = s.w.unit("BreachEngine", 'A', 320, 500);
	s.w.unit("BreachWorkshop", 'A', 100, 500);
	s.w.unit("Swordsman", 'A', 350, 500);
	s.w.frames(2);
	s.bot.build.baseBuilderDisabled = true;
	s.bot.build.economy.disabled = true;
	AIBaseBuilder::update(*s.w.logic, s.bot);
	CHECK(s.bot.build.units.requests.empty());
	AIFortification::update(*s.w.logic, s.bot);
	CHECK(s.bot.brain.siegeUnits == std::set<ObjectID>{a->getID(), b->getID()});
	s.w.logic->settings().wallBalance.enabled = false;
	AIFortification::update(*s.w.logic, s.bot);
	CHECK(s.bot.brain.siegeUnits.empty());
	CHECK(s.bot.brain.fortificationTarget == INVALID_ID);
}
