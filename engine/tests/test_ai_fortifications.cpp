// OpenBFME. GPL-3.0. Synthetic siege scenarios; no retail archives required.
#include "doctest.h"
#include "Common/XferSave.h"
#include "Common/XferLoad.h"
#include "CombatTestUtil.h"
#include "Common/Thing/ThingFactory.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/Combat/TargetFinder.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/SkirmishAI/AIFortification.h"
#include "GameLogic/SkirmishAI/AIBaseBuilder.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "GameLogic/SkirmishAI/AIStatCollector.h"
#include "GameLogic/WallBalance.h"
#include "GameLogic/UpgradeTypes.h"
using namespace combattest;
namespace
{
const char data[] = R"ini(
Armor SiegeProofExceptSiege
 Armor = DEFAULT 0%
 Armor = SIEGE 100%
End
Weapon BreachHit
 AttackRange = 200
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
Weapon MeleeBreach
 AttackRange = 20
 MeleeWeapon = Yes
 DamageNugget
  Damage = 25
  DamageType = SIEGE
 End
End
ChildObject WallAttacker BreachEngine
 KindOf = SELECTABLE CAN_ATTACK CAN_ATTACK_WALLS
 WeaponSet
  Conditions = None
  Weapon = PRIMARY MeleeBreach
 End
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
ChildObject BlockingWall BreachWall
 Geometry = BOX
 GeometryMajorRadius = 5
 GeometryMinorRadius = 600
 GeometryHeight = 25
End
ChildObject BlockingGate BlockingWall
 KindOf = STRUCTURE IMMOBILE WALL_GATE SELECTABLE
End
ChildObject WallHub BreachWall
 KindOf = STRUCTURE IMMOBILE WALL_HUB SELECTABLE
End
ChildObject ArmedWallTower BreachWall
 KindOf = STRUCTURE IMMOBILE WALL_UPGRADE CAN_ATTACK SELECTABLE
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
		w.logic->settings().enhancedAI = true;
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
 Object *factory(const char *name, char team, float x, float y)
 {
  Object *o = w.unit(name, team, x, y);
  // This fixture owns a detached AI; forward the manager's object event explicitly.
  AIBaseBuilder::onObjectEntered(*w.logic, bot, *o);
  return o;
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
 // A sealed terrain corridor makes connectivity explicit in this synthetic map.
 for(int y=0;y<100;++y)s.w.terrain.cliff.insert({60,y});
 s.w.buildMap();
	Object *fortress = s.w.unit("BreachFortress", 'B', 850, 500);
	s.w.frames(2);
	AITactic squad;
	squad.teams.emplace_back();
	squad.teams[0].members = {soldier->getID(), engine->getID()};
	s.bot.brain.tactics.push_back(squad);
	AIFortification::update(*s.w.logic, s.bot);
	CHECK(s.bot.brain.fortificationTarget == wall->getID());
	CHECK(s.bot.brain.siegeUnits == std::set<ObjectID>{engine->getID()});
	CHECK(s.bot.brain.tactics[0].teams[0].members.empty());
	CHECK(s.bot.brain.siegeEscorts.count(soldier->getID()) == 1);
	CHECK_FALSE(s.bot.brain.siegeLaunched);
	s.w.frames(225);AIFortification::update(*s.w.logic, s.bot);
	CHECK(engine->getAIUpdateInterface()->currentVictimId() == wall->getID());
	// This exercises the real attack state and body damage, not only target selection.
	s.w.frames(100);
	CHECK(s.w.health(wall) < 500); // tolerance: at least one siege hit must land after crossing the flat approach
	s.w.logic->destroyObject(wall);
 s.w.terrain.cliff.clear();s.w.buildMap();
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
	Object *factory = s.factory("BreachWorkshop", 'A', 100, 500);
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

TEST_CASE("fortification AI: six melee engines satisfy demand and policy changes release the breach team")
{
	Scenario s;
	s.restrict();
	s.w.unit("BreachFortress", 'B', 850, 500);
	Object *a = s.w.unit("BreachEngine", 'A', 300, 500);
	Object *b = s.w.unit("BreachEngine", 'A', 320, 500);
	Object *c = s.w.unit("BreachEngine", 'A', 340, 500);
 for(int i=0;i<2;++i)s.w.unit("BreachEngine",'A',360+i*20,500);
	s.factory("BreachWorkshop", 'A', 100, 500);
	s.w.unit("Swordsman", 'A', 350, 500);
	s.w.frames(2);
	s.bot.build.baseBuilderDisabled = true;
	s.bot.build.economy.disabled = true;
	AIBaseBuilder::update(*s.w.logic, s.bot);
	CHECK(s.bot.build.units.requests.empty());
	AIFortification::update(*s.w.logic, s.bot);
	CHECK(s.bot.brain.siegeUnits.size()==5);
	s.w.logic->settings().wallBalance.enabled = false;
	AIFortification::update(*s.w.logic, s.bot);
	CHECK(s.bot.brain.siegeUnits.empty());
	CHECK(s.bot.brain.fortificationTarget == INVALID_ID);
}

TEST_CASE("fortification AI: bypass open walls and hubs but retain armed emplacements")
{
 Scenario s;s.restrict();
 Object *soldier=s.w.unit("Swordsman",'A',300,500);
 Object *hub=s.w.unit("WallHub",'B',400,500);
 Object *wall=s.w.unit("BreachWall",'B',600,500);
 Object *fortress=s.w.unit("BreachFortress",'B',850,500);
 s.w.frames(2);
 CHECK(AIFortification::findTarget(*s.w.logic,s.bot)==fortress);
 CHECK_FALSE(AIFortification::allowAutoTarget(*soldier,*hub));
 CHECK_FALSE(AIFortification::allowAutoTarget(*soldier,*wall));
 Object *tower=s.w.unit("ArmedWallTower",'B',450,550);
 CHECK_FALSE(AIFortification::passiveBarrier(*tower));
 CHECK(AIFortification::allowAutoTarget(*soldier,*tower));
 soldier->getControllingPlayer()->setPlayerType(PLAYER_HUMAN,true);
 CHECK(AIFortification::allowAutoTarget(*soldier,*hub));
}

TEST_CASE("fortification AI: a blocking gate takes precedence over a nearby hub")
{
 Scenario s;s.restrict();
 s.w.unit("Swordsman",'A',300,500);
 Object *engine=s.w.unit("BreachEngine",'A',320,500);
 s.w.unit("WallHub",'B',450,500);
 Object *gate=s.w.unit("BlockingGate",'B',600,500);
 for(int y=0;y<100;++y)s.w.terrain.cliff.insert({60,y});
 s.w.buildMap();
 Object *fortress=s.w.unit("BreachFortress",'B',850,500);
 s.w.frames(2);
 AIFortification::update(*s.w.logic,s.bot);
 CHECK(s.bot.brain.fortificationTarget==gate->getID());
 CHECK_FALSE(s.bot.brain.siegeLaunched);
 s.w.frames(225);AIFortification::update(*s.w.logic,s.bot);
 CHECK(engine->getAIUpdateInterface()->currentVictimId()==gate->getID());
 s.w.logic->destroyObject(gate);s.w.terrain.cliff.clear();s.w.buildMap();s.w.frames(1);
 AIFortification::update(*s.w.logic,s.bot);
 CHECK(s.bot.brain.fortificationTarget==fortress->getID());
 CHECK(engine->getAIUpdateInterface()->currentVictimId()==fortress->getID());
}

TEST_CASE("fortification AI: a hub is a last-resort blocker, never a cleanup objective")
{
 Scenario s;s.restrict();
 s.w.unit("Swordsman",'A',300,500);
 Object *engine=s.w.unit("BreachEngine",'A',320,500);
 Object *hub=s.w.unit("WallHub",'B',600,500);
 s.w.unit("BreachFortress",'B',850,500);
 for(int y=0;y<100;++y)s.w.terrain.cliff.insert({60,y});s.w.buildMap();s.w.frames(2);
 CHECK(AIFortification::findTarget(*s.w.logic,s.bot)==hub);
 AIFortification::update(*s.w.logic,s.bot);
 s.w.frames(225);AIFortification::update(*s.w.logic,s.bot);
 CHECK(engine->getAIUpdateInterface()->currentVictimId()==hub->getID());
 // A different part of the wall was breached: the hub is still alive.
 s.w.terrain.cliff.clear();s.w.buildMap();s.w.frames(1);
 AIFortification::update(*s.w.logic,s.bot);
 CHECK(engine->getAIUpdateInterface()->currentVictimId()!=hub->getID());
 CHECK(s.w.health(hub)==500);
}

TEST_CASE("fortification AI: an infantry-only army holds outside intact walls beyond the launch timeout")
{
 Scenario s; s.restrict();
 auto *gate=s.w.unit("BlockingGate",'B',600,500);
 s.w.unit("BreachFortress",'B',850,500);
 for(int y=0;y<100;++y)s.w.terrain.cliff.insert({60,y});s.w.buildMap();
 AITactic tactic; tactic.teams.emplace_back();
 tactic.teams.front().handedOver=true;
 for(int i=0;i<10;++i)
 {
  auto *unit=s.w.unit("Swordsman",'A',150+i*10,550);
  tactic.teams.front().members.push_back(unit->getID());
  unit->getAIUpdateInterface()->aiMoveToPosition(*gate->getPosition(),CMD_FROM_AI);
  unit->getAIUpdateInterface()->armAttackMove(*gate->getPosition(),CMD_FROM_AI);
 }
 s.bot.brain.tactics.push_back(tactic);
 for(int i=0;i<240;++i) { AIFortification::update(*s.w.logic,s.bot); s.w.frames(1); }
 CHECK(s.bot.brain.siegeUnits.empty());
 CHECK(s.bot.brain.siegeEscorts.size()==10);
 CHECK_FALSE(s.bot.brain.siegeLaunched);
 // Losing the last engine after an earlier launch must also cancel the assault.
 s.bot.brain.siegeLaunched=true;
 AIFortification::update(*s.w.logic,s.bot);
 CHECK_FALSE(s.bot.brain.siegeLaunched);
 CHECK(s.bot.brain.tactics.front().teams.front().members.empty());
 for(auto id:s.bot.brain.siegeEscorts)
 {
  auto *unit=s.w.logic->findObjectByID(id);
  CHECK(unit->getAIUpdateInterface()->currentVictimId()==INVALID_ID);
  // tolerance: all held infantry remain on the home side of the wall.
  CHECK(unit->getPosition()->x < 400);
 }
}

TEST_CASE("fortification AI: a walled base gathers three engines and six escorts before launching")
{
 Scenario s; s.restrict();
 Object *gate=s.w.unit("BlockingGate",'B',600,500);
 s.w.unit("BreachFortress",'B',850,500);
 for(int y=0;y<100;++y)s.w.terrain.cliff.insert({60,y});s.w.buildMap();
 for(int i=0;i<3;++i)s.w.unit("BreachEngine",'A',150+i*20,450);
 for(int i=0;i<5;++i)s.w.unit("Swordsman",'A',150+i*20,550);
 s.w.frames(2);
 AIFortification::update(*s.w.logic,s.bot);
 CHECK(s.bot.brain.siegeUnits.size()==3);
 CHECK(s.bot.brain.siegeEscorts.size()==5);
 CHECK_FALSE(s.bot.brain.siegeLaunched);
 for(auto id:s.bot.brain.siegeUnits)CHECK(s.w.logic->findObjectByID(id)->getAIUpdateInterface()->currentVictimId()==INVALID_ID);
 s.w.unit("Swordsman",'A',280,550);s.w.frames(1);
 AIFortification::update(*s.w.logic,s.bot);
 CHECK(s.bot.brain.siegeLaunched);
 CHECK(s.bot.brain.siegeEscorts.size()==6);
 for(auto id:s.bot.brain.siegeUnits)CHECK(s.w.logic->findObjectByID(id)->getAIUpdateInterface()->currentVictimId()==gate->getID());
 for (auto id : s.bot.brain.siegeEscorts)
 {
  auto *escort = s.w.logic->findObjectByID(id);
  auto *a = escort->getAIUpdateInterface();
  CHECK(a->currentVictimId()==INVALID_ID);
  // Hold on the home side, never at the gate's x=600 attack-move destination.
  CHECK(a->stateMachine().goalPosition().x == 150);
 }
 // A straggler already at the wall must retreat without its path restarting each update.
 auto *straggler = s.w.logic->findObjectByID(*s.bot.brain.siegeEscorts.begin());
 Coord3D exposed{500,500,0}; straggler->setPosition(&exposed);
 straggler->getAIUpdateInterface()->aiIdle(CMD_FROM_AI);
 for (int i=0; i<25; ++i) { AIFortification::update(*s.w.logic,s.bot); s.w.frames(1); }
 // tolerance: proves actual retreat progress; locomotor steps are not the policy's contract.
 CHECK(straggler->getPosition()->x < 500);
 CHECK(straggler->getAIUpdateInterface()->currentVictimId() == INVALID_ID);
 gate->setTeam(s.w.teamA());s.w.terrain.cliff.clear();s.w.buildMap();
 AIFortification::update(*s.w.logic,s.bot);
 CHECK(s.bot.brain.fortificationTarget!=gate->getID());
}

TEST_CASE("offensive AI: research and unit upgrades use paid queues and prerequisites")
{
 Scenario s;
 REQUIRE(s.w.w.load(R"ini(
Upgrade Upgrade_TestArrowTechnology
 Type = PLAYER
 BuildCost = 200
 BuildTime = 1
End
Upgrade Upgrade_TestFireArrows
 Type = OBJECT
 BuildCost = 100
 BuildTime = 1
End
ChildObject UpgradeLab BreachWorkshop
 CommandSet = UpgradeLabCommands
End
ChildObject UpgradeArcher Swordsman
 CommandSet = UpgradeArcherCommands
 Behavior = ProductionUpdate Production
 End
 Behavior = ModelConditionUpgrade FireArrowUpgrade
  TriggeredBy = Upgrade_TestFireArrows
  AddConditionFlags = USER_1
 End
End
CommandButton ResearchTestArrows
 Command = PLAYER_UPGRADE
 Upgrade = Upgrade_TestArrowTechnology
End
CommandButton BuyTestArrows
 Command = OBJECT_UPGRADE
 Upgrade = Upgrade_TestFireArrows
 Options = NEED_UPGRADE
 NeededUpgrade = Upgrade_TestArrowTechnology
End
CommandSet UpgradeLabCommands
 1 = ResearchTestArrows
End
CommandSet UpgradeArcherCommands
 1 = BuyTestArrows
End
)ini").empty());
 auto *lab=s.w.unit("UpgradeLab",'A',100,500);
 auto *archer=s.w.unit("UpgradeArcher",'A',120,500);
 auto *player=lab->getControllingPlayer();
 const auto *technology=TheUpgradeCenter->findUpgrade("Upgrade_TestArrowTechnology");
 const auto *arrows=TheUpgradeCenter->findUpgrade("Upgrade_TestFireArrows");
 REQUIRE(technology); REQUIRE(arrows);
 s.bot.build.baseBuilderDisabled=true;s.bot.build.economy.disabled=true;s.bot.build.units.disabled=true;s.bot.build.phase=1;
 player->getMoney()->withdraw(player->getMoney()->countMoney(),nullptr,false);
 player->getMoney()->deposit(600,nullptr,false);
 for(int i=0;i<5;++i){AIBaseBuilder::update(*s.w.logic,s.bot);s.w.frames(1);}
 CHECK_FALSE(lab->getProductionUpdate()->isUpgradeInQueue(technology));
 CHECK_FALSE(archer->getProductionUpdate()->isUpgradeInQueue(arrows));
 player->getMoney()->withdraw(500,nullptr,false);
 // The reserve moved to AIStatCollector in AI5; use the same record the live upgrader reads.
 auto &manager = s.w.logic->skirmishAI();
 REQUIRE(s.w.w.load("SkirmishAIData TestData\nEnd\n").empty());
 manager.setStore(&s.store);
 manager.newGame(true);
 manager.setScriptControlled(player->getPlayerIndex(), true); // the detached fixture bot is stepped explicitly below
 auto *record = manager.record(player->getPlayerIndex());
 REQUIRE(record);
 record->setReserve(1500);
 s.store.data().disableUnitUpgrading=true;
 for(int i=0;i<5;++i){AIBaseBuilder::update(*s.w.logic,s.bot);s.w.frames(1);}
 CHECK_FALSE(lab->getProductionUpdate()->isUpgradeInQueue(technology));
 CHECK(player->getMoney()->countMoney()+record->reserve()==1600);
 s.store.data().disableUnitUpgrading=false;
 for(int i=0;i<5;++i){AIBaseBuilder::update(*s.w.logic,s.bot);s.w.frames(1);}
 CHECK((player->hasUpgradeComplete(technology) || lab->getProductionUpdate()->isUpgradeInQueue(technology)));
 s.w.frames(10);
 for(int i=0;i<5;++i){AIBaseBuilder::update(*s.w.logic,s.bot);s.w.frames(1);}
 s.w.frames(10);
 CHECK(archer->hasUpgrade(arrows));
 CHECK(player->getMoney()->countMoney()+record->reserve()==1300);
 for(int i=0;i<10;++i){AIBaseBuilder::update(*s.w.logic,s.bot);s.w.frames(1);}
 CHECK(player->getMoney()->countMoney()+record->reserve()==1300);
}

TEST_CASE("offensive AI: midgame maintains siege without a protected target or composition share")
{
 Scenario s;
 s.w.unit("BreachEngine",'A',150,500);
 auto *factory=s.factory("BreachWorkshop",'A',100,500);
 s.w.frames(2);
 s.bot.build.baseBuilderDisabled=true;s.bot.build.economy.disabled=true;s.bot.build.phase=1;
 REQUIRE(AIFortification::findTarget(*s.w.logic,s.bot)==nullptr);
 AIBaseBuilder::update(*s.w.logic,s.bot);
 REQUIRE(s.bot.build.units.requests.size()==1);
 CHECK(s.bot.build.units.requests.front()->templateName=="BreachEngine");
 CHECK(factory->getProductionUpdate()->getProductionCount()==1);
}

TEST_CASE("offensive AI: ranged siege takes priority over existing melee breach units")
{
 Scenario s;s.restrict();
 REQUIRE(s.w.w.load("CommandButton TrainCatapult\n Command = UNIT_BUILD\n Object = BreachCatapult\nEnd\nCommandSet RangedWorkshopCommands\n 1 = TrainBreach\n 2 = TrainCatapult\nEnd\nChildObject RangedWorkshop BreachWorkshop\n CommandSet = RangedWorkshopCommands\nEnd\nArmyDefinition RangedArmy\n Side = Ranged\n ArmyMemberDefinition Ram\n Unit = WallAttacker\n End\n ArmyMemberDefinition Catapult\n Unit = BreachCatapult\n End\nEnd\n").empty());
 s.bot.army=s.store.findArmy("Ranged");
 s.w.unit("BreachFortress",'B',850,500);
 auto *factory=s.factory("RangedWorkshop",'A',100,500);
 for(int i=0;i<6;++i)s.w.unit("WallAttacker",'A',200+i*20,500);
 s.w.frames(2);s.bot.build.baseBuilderDisabled=true;s.bot.build.economy.disabled=true;
 AIBaseBuilder::update(*s.w.logic,s.bot);
 REQUIRE(s.bot.build.units.requests.size()==1);
 CHECK(s.bot.build.units.requests.front()->templateName=="BreachCatapult");
 CHECK(factory->getProductionUpdate()->getProductionCount()==1);
}

TEST_CASE("offensive AI: a busy siege producer can queue its unlocking level upgrade")
{
 Scenario s;
 s.w.logic->productionSettings().loaded=true;
 s.w.logic->productionSettings().multipleFactory=1;
 s.w.logic->productionSettings().multiPlayEntries=8;
 for(int i=0;i<8;++i){s.w.logic->productionSettings().multiPlayUnitSpeedMult[i]=1;s.w.logic->productionSettings().multiPlayBuildingSpeedMult[i]=1;}
 REQUIRE(s.w.w.load(R"ini(
Upgrade Upgrade_TestSiegeLevel2
 Type = OBJECT
 BuildCost = 200
 BuildTime = 1
End
ChildObject BusyWorkshop BreachWorkshop
 CommandSet = BusyWorkshopCommands
 Behavior = ModelConditionUpgrade WorkshopLevel
  TriggeredBy = Upgrade_TestSiegeLevel2
  AddConditionFlags = USER_1
 End
End
CommandButton BuySiegeLevel
 Command = OBJECT_UPGRADE
 Upgrade = Upgrade_TestSiegeLevel2
End
CommandSet BusyWorkshopCommands
 1 = TrainBreach
 2 = BuySiegeLevel
End
)ini").empty());
 auto *factory=s.factory("BusyWorkshop",'A',100,500);
 auto *pu=factory->getProductionUpdate();
 auto *unit=s.w.w.things.findTemplate("BreachEngine");
 REQUIRE(pu->queueCreateUnit(unit,-1,pu->requestUniqueUnitID(),-1,false,std::string(),false));
 s.bot.build.baseBuilderDisabled=true;s.bot.build.economy.disabled=true;s.bot.build.units.disabled=true;s.bot.build.phase=1;
 const auto *upgrade=TheUpgradeCenter->findUpgrade("Upgrade_TestSiegeLevel2");REQUIRE(upgrade);
 for(int i=0;i<5;++i){AIBaseBuilder::update(*s.w.logic,s.bot);s.w.frames(1);}
 CHECK((factory->hasUpgrade(upgrade) || pu->isUpgradeInQueue(upgrade)));
}

TEST_CASE("offensive AI: upgrading a registered factory reveals newly unlocked artillery")
{
 Scenario s;
 REQUIRE(s.w.w.load(R"ini(
Upgrade Upgrade_TestFactoryArtillery
 Type = OBJECT
End
ChildObject UnlockingWorkshop BreachWorkshop
 CommandSet = UnlockingWorkshopCommands
 Behavior = ModelConditionUpgrade Unlock
  TriggeredBy = Upgrade_TestFactoryArtillery
  AddConditionFlags = USER_1
 End
End
CommandButton TrainUnlockedCatapult
 Command = UNIT_BUILD
 Object = BreachCatapult
 Options = NEED_UPGRADE
 NeededUpgrade = Upgrade_TestFactoryArtillery
End
CommandSet UnlockingWorkshopCommands
 1 = TrainBreach
 2 = TrainUnlockedCatapult
End
ArmyDefinition UnlockingArmy
 Side = Unlocking
 ArmyMemberDefinition Ram
  Unit = BreachEngine
 End
 ArmyMemberDefinition Artillery
  Unit = BreachCatapult
 End
End
)ini").empty());
 UpgradeTypeTable types;std::string error;
 REQUIRE(types.scan("Upgrade Upgrade_TestFactoryArtillery\n Type = OBJECT\nEnd\n",&error));
 s.w.logic->setUpgradeTypes(&types);
 s.bot.army=s.store.findArmy("Unlocking");
 auto *factory=s.factory("UnlockingWorkshop",'A',100,500);
 s.w.frames(2);s.bot.build.baseBuilderDisabled=true;s.bot.build.economy.disabled=true;
 AIBaseBuilder::update(*s.w.logic,s.bot);
 auto key=s.store.keyOf("BreachCatapult");
 CHECK(s.bot.build.units.factories.count(key)==0);
 factory->giveUpgrade("Upgrade_TestFactoryArtillery");
 for(int i=0;i<5;++i){s.w.frames(1);AIBaseBuilder::update(*s.w.logic,s.bot);}
 CHECK(s.bot.build.units.factories.count(key)==1);
 for(int i=0;i<5;++i){s.w.frames(1);AIBaseBuilder::update(*s.w.logic,s.bot);}
 CHECK(s.bot.build.units.factories.count(key)==1);
}

TEST_CASE("offensive AI: saturated target slots still dispatch a new full attack wave")
{
 Scenario s;
 auto *target=s.w.unit("BreachFortress",'B',850,500);
 s.bot.build.phase=1;
 s.bot.brain.chooser=false;
 s.bot.brain.bestTarget=-1;
 AITarget objective;
 objective.inactive=false;
 objective.object=target->getID();
 objective.position=*target->getPosition();
 objective.teams=objective.maxTeams=1;
 s.bot.brain.targets.push_back(objective);
 for(int i=0;i<8;++i)s.w.unit("Swordsman",'A',150+i*20,450);
 s.w.frames(30);
 auto *player=s.w.players.findPlayerWithName("Alice");
 player->setPlayerType(PLAYER_HUMAN,true);
 REQUIRE(s.w.w.load("SkirmishAIData TheSkirmishAIData\nEnd\n").empty());
 s.w.logic->skirmishAI().setStore(&s.store);
 s.w.logic->skirmishAI().newGame(true);
 player->setPlayerType(PLAYER_COMPUTER,true);
 AITacticalAI::updateBrain(*s.w.logic,s.bot,&s.bot);
 REQUIRE(s.bot.brain.tactics.size()==1);
 CHECK(s.bot.brain.targets[0].teams==2);
 CHECK(s.bot.brain.targets[0].maxTeams==1);
 REQUIRE(s.bot.brain.tactics[0].teams.size()==1);
 CHECK(s.bot.brain.tactics[0].teams[0].members.size()==8);
 AITacticalAI::updateTeamBuilder(*s.w.logic,s.bot);
 CHECK(s.bot.brain.tactics[0].teams[0].handedOver);
 AITacticalAI::updateBrain(*s.w.logic,s.bot,&s.bot);
 s.w.frames(1);
 AITacticalAI::updateBrain(*s.w.logic,s.bot,&s.bot);
 CHECK(s.bot.brain.attacksLaunched==1);
 CHECK(s.bot.brain.targets[0].teams==2);
}

TEST_CASE("source AI reserve: execution withdraws only the shortfall, never the full price")
{
	// DONOR AIBuildable_doBuild.cpp: a 100-cost unit, cash/reserve before and after execution.
	struct Example { unsigned cash, reserve, cashAfter, reserveAfter; bool queued; };
	for (const Example example : { Example{40, 70, 0, 10, true}, Example{140, 70, 40, 70, true}, Example{40, 50, 40, 50, false} })
	{
		CAPTURE(example.cash);
		CAPTURE(example.reserve);
		Scenario s;
		s.w.logic->settings().enhancedAI = false;
		s.factory("BreachWorkshop", 'A', 100, 500);
		auto *player = s.w.players.findPlayerWithName("Alice");
		REQUIRE(s.w.w.load("SkirmishAIData TestData\nEnd\n").empty());
		auto &manager = s.w.logic->skirmishAI();
		manager.setStore(&s.store);
		manager.newGame(true);
		manager.setScriptControlled(player->getPlayerIndex(), true);
		auto *record = manager.record(player->getPlayerIndex());
		REQUIRE(record);
		player->getMoney()->withdraw(player->getMoney()->countMoney(), nullptr, false);
		player->getMoney()->deposit(example.cash, nullptr, false);
		record->setReserve(example.reserve);
		s.bot.build.baseBuilderDisabled = true;
		s.bot.build.economy.disabled = true;
		s.bot.build.units.disabled = true;
		auto request = std::make_shared<AIBuildRequest>();
		request->unit = true;
		request->reserveMoney = true;
		request->templateName = "BreachEngine";
		request->priority = 500;
		s.bot.build.pending.push_back(request);
		AIBaseBuilder::update(*s.w.logic, s.bot);
		CHECK(request->state == (example.queued ? 1 : 0));
		CHECK(player->getMoney()->countMoney() == example.cashAfter);
		CHECK(record->reserve() == example.reserveAfter);
		CHECK(s.bot.build.failed == 0);
	}
}

TEST_CASE("source AI spatial groups: strict neighbor boundary, bridge merge, and removal rebuild")
{
	// DONOR Rva004DFA43Sphere / Rva00283114GroupMerge: squared distance < padding^2 * 2.
	AIPositionSampler sampler;
	std::map<ObjectID, Coord3D> points{{(ObjectID)1, {0, 0, 0}}, {(ObjectID)2, {24, 0, 0}}, {(ObjectID)3, {12, 0, 0}}, {(ObjectID)4, {10, 10, 0}}};
	auto position = [&](ObjectID id) { return points.at(id); };
	sampler.add((ObjectID)1, 10, position);
	sampler.add((ObjectID)2, 10, position);
	REQUIRE(sampler.groups().size() == 2);
	CHECK(sampler.groups()[0].radius == 10.0f);
	sampler.add((ObjectID)3, 10, position);
	REQUIRE(sampler.groups().size() == 1);
	CHECK((sampler.groups()[0].members == std::vector<ObjectID>{(ObjectID)1, (ObjectID)3, (ObjectID)2}));
	CHECK(sampler.groups()[0].center.x == 12.0f);
	CHECK(sampler.groups()[0].radius == 22.0f);
	sampler.groups()[0].selected = true;
	sampler.remove((ObjectID)3, position);
	REQUIRE(sampler.groups().size() == 2);
	CHECK(sampler.groups()[0].center.x == 0.0f);
	CHECK(sampler.groups()[1].center.x == 24.0f);
	CHECK_FALSE(sampler.groups()[0].selected);
	AIPositionSampler boundary;
	boundary.add((ObjectID)1, 10, position);
	boundary.add((ObjectID)4, 10, position);
	CHECK(boundary.groups().size() == 2);
	// Native removal erases one registration, not every duplicate.
	boundary.add((ObjectID)1, 10, position);
	boundary.remove((ObjectID)1, position);
	REQUIRE(boundary.groups().size() == 2);
	CHECK((boundary.groups()[1].members == std::vector<ObjectID>{(ObjectID)1}));
}

TEST_CASE("source AI spatial groups: snapshot preserves selection and registration order")
{
	AIPositionSampler sampler;
	std::map<ObjectID, Coord3D> points{{(ObjectID)1, {0, 0, 0}}, {(ObjectID)2, {24, 0, 0}}, {(ObjectID)3, {12, 0, 0}}};
	auto position = [&](ObjectID id) { return points.at(id); };
	for (ObjectID id : {(ObjectID)1, (ObjectID)2, (ObjectID)3})
		sampler.add(id, 10, position);
	sampler.groups()[0].selected = true;
	XferSave save;
	sampler.xfer(save);
	AIPositionSampler restored;
	XferLoad load(save.data());
	restored.xfer(load);
	REQUIRE(restored.groups().size() == 1);
	CHECK(restored.groups()[0].selected);
	CHECK(restored.members() == sampler.members());
	CHECK(restored.groups()[0].members == sampler.groups()[0].members);
	XferSave again;
	restored.xfer(again);
	CHECK(again.data() == save.data());
	restored.remove((ObjectID)3, position);
	CHECK(restored.groups().size() == 2);
	// An internally inconsistent group cannot load and feed later position lookups.
	restored.groups()[0].members.push_back((ObjectID)999);
	XferSave corrupt;
	restored.xfer(corrupt);
	AIPositionSampler refused;
	XferLoad bad(corrupt.data());
	CHECK_THROWS_WITH(refused.xfer(bad), "skirmish ai: spatial groups differ from registrations");
}

TEST_CASE("source AI defense: native target groups create and recruit SimpleDefense on every difficulty")
{
	for (int difficulty = 0; difficulty < 4; ++difficulty)
	{
		CAPTURE(difficulty);
		Scenario s;
		s.w.logic->settings().enhancedAI = false;
		REQUIRE(s.w.w
					.load("SkirmishAIData NativeDefense\nEnd\nArmyDefinition NativeAlpha\n Side = Alpha\n TacticalAITargets = DEFENSIVE\n "
						  "MaxTeamsPerTarget = 1\nEnd\n")
					.empty());
		Object *base = s.w.unit("BreachWorkshop", 'A', 100, 500);
		s.w.unit("BreachWall", 'A', 120, 500);
		Object *enemy = s.w.unit("Swordsman", 'B', 150, 500);
		std::vector<ObjectID> own;
		for (int i = 0; i < 4; ++i)
			own.push_back(s.w.unit("Swordsman", 'A', 80, (float)(i == 0 ? 490 : 440 + i * 10))->getID());
		auto &manager = s.w.logic->skirmishAI();
		manager.setStore(&s.store);
		manager.newGame(true);
		auto *player = s.w.players.findPlayerWithName("Alice");
		player->setSkirmishDifficulty(difficulty);
		for (const auto &group : manager.groups())
			for (int index : group->members)
				manager.setScriptControlled(index, true);
		auto *ai = const_cast<AISkirmishPlayer *>(manager.findAI(player->getPlayerIndex()));
		REQUIRE(ai);
		REQUIRE(ai->army);
		ai->brain.farmKillEarly = false;
		s.w.frames(50);
		s.w.logic->partition().update();
		AITacticalAI::updateBrain(*s.w.logic, *ai, ai);
		REQUIRE(ai->brain.targets.size() == 1);
		CHECK(ai->brain.targets[0].object == enemy->getID());
		CHECK(ai->brain.targets[0].defenseSize == 1);
		CHECK(ai->brain.targets[0].radius == 200.0f);
		REQUIRE(ai->brain.tactics.size() == 1);
		auto &defense = ai->brain.tactics[0];
		CHECK(defense.kind == "SimpleDefense");
		REQUIRE(defense.teams.size() == 1);
		CHECK(defense.teams[0].maximum == 3);
		CHECK(defense.teams[0].type == 1);
		AITacticalAI::updateTeamBuilder(*s.w.logic, *ai);
		CHECK(defense.teams[0].members.size() == 3);
		CHECK((defense.teams[0].members == std::vector<ObjectID>{own[0], own[3], own[2]}));
		CHECK(defense.teams[0].hasRecruitOrigin);
		CHECK(defense.teams[0].recruitOrigin.x == enemy->getPosition()->x);
		CHECK(defense.teams[0].handedOver);
		AITacticalAI::updateBrain(*s.w.logic, *ai, ai);
		AITacticalAI::updateBrain(*s.w.logic, *ai, ai);
		CHECK(ai->brain.attacksLaunched == 1);
		CHECK(base->getControllingPlayer() == player);
	}
}

TEST_CASE("source AI defense: small group chance and large group half-army sizing survive snapshots")
{
	for (int structures : {1, 3})
	{
		CAPTURE(structures);
		Scenario s;
		s.w.logic->settings().enhancedAI = false;
		REQUIRE(s.w.w
					.load("SkirmishAIData NativeDefense\nEnd\nArmyDefinition NativeAlpha\n Side = Alpha\n TacticalAITargets = DEFENSIVE\n "
						  "MaxTeamsPerTarget = 1\nEnd\n")
					.empty());
		for (int i = 0; i < structures; ++i)
			s.w.unit("BreachWorkshop", 'A', (float)(100 + i * 20), 500);
		Object *enemy = s.w.unit("Swordsman", 'B', 150, 500);
		for (int i = 0; i < 5; ++i)
			s.w.unit("Swordsman", 'A', 80, (float)(450 + i * 10));
		auto &manager = s.w.logic->skirmishAI();
		manager.setStore(&s.store);
		manager.newGame(true);
		for (const auto &group : manager.groups())
			for (int index : group->members)
				manager.setScriptControlled(index, true);
		const int player = s.w.players.findPlayerWithName("Alice")->getPlayerIndex();
		auto *ai = const_cast<AISkirmishPlayer *>(manager.findAI(player));
		REQUIRE(ai);
		ai->brain.farmKillEarly = false;
		auto &random = s.w.logic->random();
		random.seedRandom(1);
		GameLogicRandom oracle = random;
		(void)oracle.getValue(0, 0, "AITargetChooser.cpp", 0xB8);
		(void)oracle.getValue(0, 0, "AITargetHeuristicBaseDefense.cpp", 0x57);
		const int roll = oracle.getValue(0, 100, "AITargetHeuristicBaseDefense.cpp", 0x60);
		random.enableCallLog(true);
		random.clearCallLog();
		AITacticalAI::updateBrain(*s.w.logic, *ai, ai);
		REQUIRE(ai->brain.targets.size() == 1);
		const bool accepts = structures != 1 || roll < 30; // Native 30/101 small-base gate; larger groups do not make this draw.
		CHECK(ai->brain.targets[0].inactive == !accepts);
		CHECK(ai->brain.targets[0].defenseSize == (structures == 1 ? 0 : 2));
		bool gateFound = false;
		for (const auto &call : random.callLog())
			if (call.file == "AITargetHeuristicBaseDefense.cpp" && call.line == 0x60)
			{
				gateFound = true;
				CHECK(call.lo == 0);
				CHECK(call.hi == 100);
				CHECK(call.result == roll);
			}
		CHECK(gateFound == (structures == 1));
		if (accepts)
		{
			CHECK(ai->brain.targets[0].object == enemy->getID());
			REQUIRE(ai->brain.tactics.size() == 1);
			CHECK(ai->brain.tactics[0].teams[0].maximum == (structures == 1 ? 1 : 2));
			XferSave save;
			manager.xfer(save);
			ai->brain.targets[0].defenseSize = 1;
			XferLoad load(save.data());
			manager.xfer(load);
			const auto *restored = manager.findAI(player);
			REQUIRE(restored);
			CHECK(restored->brain.targets[0].defenseSize == (structures == 1 ? 0 : 2));
			CHECK(restored->brain.tactics[0].kind == "SimpleDefense");
			CHECK(restored->brain.tactics[0].teams[0].hasRecruitOrigin);
			CHECK(restored->brain.tactics[0].teams[0].recruitOrigin.x == enemy->getPosition()->x);
		}
	}
}
