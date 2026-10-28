// OpenBFME. GPL-3.0. Synthetic strategy scenarios; no retail content.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/SkirmishAI/AIStrategy.h"
#include "GameLogic/SkirmishAI/AIBaseBuilder.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "GameLogic/System/ShroudManager.h"
#include "Common/StateHash.h"
#include "GameLogic/AI/AIStateMachine.h"
using namespace combattest;
namespace
{
const char data[] = R"ini(
ChildObject CounterPike Swordsman
 KindOf = SELECTABLE CAN_ATTACK INFANTRY PIKE
 CommandPoints = 5
 BuildCost = 100
End
ChildObject CounterBow Archer
 KindOf = SELECTABLE CAN_ATTACK INFANTRY ARCHER
 CommandPoints = 5
 BuildCost = 100
End
ChildObject CounterHorse Swordsman
 KindOf = SELECTABLE CAN_ATTACK CAVALRY
 CommandPoints = 5
 BuildCost = 100
End
ChildObject CounterHero Swordsman
 KindOf = SELECTABLE CAN_ATTACK HERO
End
ChildObject CounterSiege Archer
 KindOf = SELECTABLE CAN_ATTACK SIEGEENGINE
End
Object CounterFactory
 KindOf = STRUCTURE IMMOBILE SELECTABLE FS_FACTORY
 CommandSet = CounterFactoryCommands
 Body = ActiveBody Body
  MaxHealth = 1000
 End
 Behavior = ProductionUpdate Production
 End
End
)ini";
struct Scenario
{
 CombatWorld w{data};
 CommandStore commands;
 CommandStore *previous = TheCommandStore;
 SkirmishAIStore store{w.w.keys};
 AISkirmishPlayer bot;
 Scenario()
 {
  w.logic->settings().enhancedAI = true;
		w.combat().setAutoAcquireEnabled(false);
  auto *player = w.players.findPlayerWithName("Alice");
  player->setPlayerType(PLAYER_COMPUTER, true);
  bot.playerIndex = player->getPlayerIndex();
  bot.build.startPosition = {100, 500, 0};
  bot.store = &store;
  bot.build.baseBuilderDisabled = true;
  bot.build.economy.disabled = true;
  commands.setThingFactory(&w.w.things);
  TheCommandStore = &commands;
  store.registerBlocks(w.w.fx.env.blocks);
  w.w.fx.env.blocks.registerBlock("CommandButton", [](INI *ini) { CommandStore::parseCommandButtonDefinitionGlobal(ini); });
  w.w.fx.env.blocks.registerBlock("CommandSet", [](INI *ini) { CommandStore::parseCommandSetDefinitionGlobal(ini); });
  REQUIRE(w.w.load(R"ini(
CommandButton TrainCounterPike
 Command = UNIT_BUILD
 Object = CounterPike
End
CommandButton TrainCounterBow
 Command = UNIT_BUILD
 Object = CounterBow
End
CommandSet CounterFactoryCommands
 1 = TrainCounterPike
 2 = TrainCounterBow
End
ArmyDefinition CounterArmy
 Side = Counter
 ArmyMemberDefinition Pike
  Unit = CounterPike
  PercentageOfArmyPhase1 = 0
  PercentageOfArmyPhase2 = 0
  PercentageOfArmyPhase3 = 0
 End
 ArmyMemberDefinition Bow
  Unit = CounterBow
  PercentageOfArmyPhase1 = 0
  PercentageOfArmyPhase2 = 0
  PercentageOfArmyPhase3 = 0
 End
End
)ini").empty());
  bot.army = store.findArmy("Counter");
  while (w.logic->getFrame() % 5 != (unsigned)bot.playerIndex % 5) w.frames(1);
 }
 Object *factory(const char *name, char team, float x, float y)
 {
  Object *o = w.unit(name, team, x, y);
  // This fixture owns a detached AI; forward the manager's object event explicitly.
  AIBaseBuilder::onObjectEntered(*w.logic, bot, *o);
  return o;
 }
 ~Scenario() { TheCommandStore = previous; }
 void decide()
 {
  while (w.logic->getFrame() % 5 != (unsigned)bot.playerIndex % 5) w.frames(1);
  AIStrategy::update(*w.logic, bot);
 }
};
}
TEST_CASE("strategy AI: role deficits react to cavalry flyers and pike screens")
{
 AIStrategy::Situation s;
 s.enemy[AIStrategy::Cavalry] = 2;
 CHECK(AIStrategy::productionNeed(AIStrategy::Pikes, s) == 42);
 s.own[AIStrategy::Pikes] = 2;
 CHECK(AIStrategy::productionNeed(AIStrategy::Pikes, s) == 0);
 s.enemy[AIStrategy::Flyers] = 1;
 CHECK(AIStrategy::productionNeed(AIStrategy::Archers, s) == 52);
 s.enemy[AIStrategy::Archers] = 3;
 CHECK(AIStrategy::productionNeed(AIStrategy::Cavalry, s) == 21);
 s.enemy[AIStrategy::Pikes] = 1;
 CHECK(AIStrategy::productionNeed(AIStrategy::Cavalry, s) == 0);
 CHECK(AIStrategy::productionNeed(AIStrategy::Swords, s) == 16);
}
TEST_CASE("strategy AI: visible composition counts battalions and pending counters")
{
 Scenario s;
 s.w.unit("CounterHorse", 'B', 500, 500);
 s.w.unit("CounterPike", 'A', 200, 500);
 auto request = std::make_shared<AIBuildRequest>();
 request->templateName = "CounterPike";
 s.bot.build.units.requests.push_back(request);
 auto state = AIStrategy::assess(*s.w.logic, s.bot);
 CHECK(state.enemy[AIStrategy::Cavalry] == 1);
 CHECK(state.own[AIStrategy::Pikes] == 2);
 CHECK(state.homeThreat);
 request->state = 3;
 CHECK(AIStrategy::assess(*s.w.logic, s.bot).own[AIStrategy::Pikes] == 1);
}
TEST_CASE("strategy AI: hidden enemies do not inform counter production")
{
 Scenario s;
 auto *enemy = s.w.unit("CounterHorse", 'B', 800, 500);
 ShroudManager fog(*s.w.logic);
 fog.init(20, 0);
 fog.setExtent(0, 0, 1000, 1000);
 fog.attach();
 s.w.logic->setShroud(&fog);
 fog.registerObject(*enemy);
 fog.update();
 CHECK_FALSE(AIStrategy::visible(*s.w.logic, *enemy, s.bot.playerIndex));
 CHECK(AIStrategy::assess(*s.w.logic, s.bot).enemy[AIStrategy::Cavalry] == 0);
 fog.revealMapForPlayerPermanently(s.bot.playerIndex);
 CHECK(AIStrategy::visible(*s.w.logic, *enemy, s.bot.playerIndex));
 CHECK(AIStrategy::assess(*s.w.logic, s.bot).enemy[AIStrategy::Cavalry] == 1);
 s.w.logic->setShroud(nullptr);
}
TEST_CASE("strategy AI: an observed cavalry rush buys pikes through paid production")
{
 Scenario s;
 s.w.unit("CounterHorse", 'B', 650, 500);
 auto *factory = s.factory("CounterFactory", 'A', 100, 500);
 s.w.frames(2);
 const auto before = factory->getControllingPlayer()->getMoney()->countMoney();
 AIBaseBuilder::update(*s.w.logic, s.bot);
 REQUIRE(s.bot.build.units.requests.size() == 1);
 CHECK(s.bot.build.units.requests.front()->templateName == "CounterPike");
 CHECK(factory->getProductionUpdate()->getProductionCount() == 1);
 CHECK(factory->getControllingPlayer()->getMoney()->countMoney() == before - 100);
 AIBaseBuilder::update(*s.w.logic, s.bot);
 CHECK(s.bot.build.units.requests.size() == 1);
 CHECK(factory->getProductionUpdate()->getProductionCount() == 1);
}
TEST_CASE("strategy AI: cavalry withdraws instead of attacking a nearby pike")
{
 Scenario s;
 auto *horse = s.w.unit("CounterHorse", 'A', 400, 500);
 auto *pike = s.w.unit("CounterPike", 'B', 520, 500);
 REQUIRE(horse->getAIUpdateInterface()->aiAttackObject(pike, CMD_FROM_AI));
 s.decide();

 CHECK(horse->getAIUpdateInterface()->currentVictimId() == INVALID_ID);
 CHECK(horse->getAIUpdateInterface()->stateMachine().goalPosition().x == 240.0f);
 CHECK(s.bot.brain.strategyRecovering.count(horse->getID()) == 1);
}
TEST_CASE("strategy AI: protected siege reservations are never redirected by micro")
{
 Scenario s;
 auto *horse = s.w.unit("CounterHorse", 'A', 400, 500);
 auto *pike = s.w.unit("CounterPike", 'B', 520, 500);
 REQUIRE(horse->getAIUpdateInterface()->aiAttackObject(pike, CMD_FROM_AI));
 s.bot.brain.siegeEscorts.insert(horse->getID());
 s.decide();
 CHECK(horse->getAIUpdateInterface()->currentVictimId() == pike->getID());
}
TEST_CASE("strategy AI: wounded heroes leave squads and remain reserved during recovery")
{
 Scenario s;
 auto *hero = s.w.unit("CounterHero", 'A', 400, 500);
 s.w.unit("CounterPike", 'B', 520, 500);
 hero->getBodyModule()->setInitialHealth(20);
 AITactic tactic;
 tactic.teams.emplace_back();
 tactic.teams.front().members.push_back(hero->getID());
 s.bot.brain.tactics.push_back(tactic);
 s.decide();
 CHECK(s.bot.brain.tactics.front().teams.front().members.empty());
 CHECK(s.bot.brain.strategyRecovering.count(hero->getID()) == 1);
 hero->getBodyModule()->setInitialHealth(100);
 s.w.frames(5);s.decide();
 CHECK(s.bot.brain.strategyRecovering.empty());
}
TEST_CASE("strategy AI: archers focus accessible infantry instead of economy structures")
{
 Scenario s;
 auto *archer = s.w.unit("CounterBow", 'A', 400, 500);
 s.w.unit("CounterFactory", 'B', 450, 500);
 auto *pike = s.w.unit("CounterPike", 'B', 550, 500);
 s.decide();
 CHECK(archer->getAIUpdateInterface()->currentVictimId() == pike->getID());
}
TEST_CASE("strategy AI: tactical disable prevents all new retreat and target orders")
{
 Scenario s;
 auto *horse = s.w.unit("CounterHorse", 'A', 400, 500);
 auto *pike = s.w.unit("CounterPike", 'B', 520, 500);
 REQUIRE(horse->getAIUpdateInterface()->aiAttackObject(pike, CMD_FROM_AI));
 s.store.data().disableTacticalAI = true;
 s.decide();
 CHECK(horse->getAIUpdateInterface()->currentVictimId() == pike->getID());
 CHECK(s.bot.brain.strategyRecovering.empty());
}
TEST_CASE("strategy AI: a counter request cannot create an unfunded unit")
{
 Scenario s;
 s.w.unit("CounterHorse", 'B', 650, 500);
 auto *factory = s.factory("CounterFactory", 'A', 100, 500);
 s.w.frames(2);
 auto *money = factory->getControllingPlayer()->getMoney();
 money->withdraw(money->countMoney(), nullptr, false);
 AIBaseBuilder::update(*s.w.logic, s.bot);
 REQUIRE(s.bot.build.units.requests.size() == 1);
 CHECK(s.bot.build.units.requests.front()->templateName == "CounterPike");
 CHECK(factory->getProductionUpdate()->getProductionCount() == 0);
 CHECK(money->countMoney() == 0);
}
TEST_CASE("strategy AI: recovery reservations change the deterministic brain hash")
{
 AIBrainState a, b;
 a.strategyRecovering[7] = 100;
 a.strategyRecovering[3] = 80;
 b.strategyRecovering[3] = 80;
 b.strategyRecovering[7] = 100;
 StateHasher ha, hb;
 AITacticalAI::crc(a, ha);
 AITacticalAI::crc(b, hb);
 CHECK(ha.value() == hb.value());
 b.strategyRecovering[7] = 101;
 StateHasher changed;
 AITacticalAI::crc(b, changed);
 CHECK_FALSE(ha.value() == changed.value());
}
TEST_CASE("strategy AI: script-controlled players retain authority over their units")
{
 Scenario s;
 auto *horse = s.w.unit("CounterHorse", 'A', 400, 500);
 auto *pike = s.w.unit("CounterPike", 'B', 520, 500);
 REQUIRE(horse->getAIUpdateInterface()->aiAttackObject(pike, CMD_FROM_AI));
 s.bot.scriptControlled = true;
 s.decide();
 CHECK(horse->getAIUpdateInterface()->currentVictimId() == pike->getID());
}
TEST_CASE("strategy AI: recovery expires without granting health")
{
 Scenario s;
 auto *hero = s.w.unit("CounterHero", 'A', 400, 500);
 hero->getBodyModule()->setInitialHealth(20);
 const auto health = hero->getBodyModule()->getHealth();
 s.bot.brain.strategyRecovering[hero->getID()] = s.w.logic->getFrame();
 s.decide();
 CHECK(s.bot.brain.strategyRecovering.empty());
 CHECK(hero->getBodyModule()->getHealth() == health);
}
TEST_CASE("strategy AI: visible home pressure suspends optional equipment purchases")
{
 Scenario s;
 REQUIRE(s.w.w.load(R"ini(
Upgrade Upgrade_StrategyArrows
 Type = PLAYER
 BuildCost = 200
 BuildTime = 1
End
CommandButton ResearchStrategyArrows
 Command = PLAYER_UPGRADE
 Upgrade = Upgrade_StrategyArrows
End
CommandSet StrategyLabCommands
 1 = ResearchStrategyArrows
End
ChildObject StrategyLab CounterFactory
 CommandSet = StrategyLabCommands
End
)ini").empty());
 auto *lab = s.w.unit("StrategyLab", 'A', 100, 500);
 auto *enemy = s.w.unit("CounterHorse", 'B', 650, 500);
 const auto *upgrade = TheUpgradeCenter->findUpgrade("Upgrade_StrategyArrows");
 REQUIRE(upgrade);
 s.bot.build.phase = 1;
 s.bot.build.units.disabled = true;
 auto *payer = lab->getControllingPlayer();
 REQUIRE(TheCommandStore == &s.commands);
 REQUIRE_FALSE(lab->isEffectivelyDead());
 REQUIRE_FALSE(lab->isUnderConstruction());
 REQUIRE(lab->isInWorld());
 REQUIRE(lab->getProductionUpdate()->canQueueUpgrade(upgrade) == 0);
 REQUIRE(s.commands.findCommandSet(lab->getCommandSetName())->getCommandButton(0)->m_upgradeName == "Upgrade_StrategyArrows");
 REQUIRE(s.commands.findCommandSet(lab->getCommandSetName()));
 REQUIRE(s.commands.findCommandSet(lab->getCommandSetName())->getCommandButton(0));
 REQUIRE_FALSE(lab->hasUpgrade(upgrade));
 REQUIRE(upgrade->getUpgradeType() == UPGRADE_TYPE_PLAYER);
 // Start the manager's record store, then stop native decisions: this scenario owns its detached policy.
 REQUIRE(s.w.w.load("SkirmishAIData TheSkirmishAIData\nEnd\n").empty());
 s.w.logic->skirmishAI().setStore(&s.store);
 s.w.logic->skirmishAI().newGame(true);
 s.w.logic->skirmishAI().setScriptControlled(payer->getPlayerIndex(), true);
 auto *record = s.w.logic->skirmishAI().record(payer->getPlayerIndex());
 REQUIRE(record != nullptr);
 payer->getMoney()->withdraw(payer->getMoney()->countMoney(), nullptr, false);
 record->setReserve(1000);
 REQUIRE(s.bot.build.moneyReserve == 0);
 const auto before = (std::uint32_t)AIBaseBuilder::moneyPlusReserve(*s.w.logic, *payer);
 for (int i = 0; i < 5; ++i) { AIBaseBuilder::update(*s.w.logic, s.bot); s.w.frames(1); }
 CHECK_FALSE(payer->hasUpgradeComplete(upgrade));
 CHECK_FALSE(lab->getProductionUpdate()->isUpgradeInQueue(upgrade));
 CHECK((std::uint32_t)AIBaseBuilder::moneyPlusReserve(*s.w.logic, *payer) == before);
 s.w.logic->destroyObject(enemy);
 s.w.frames(1); // Destruction unlinks the enemy on the next simulation frame.
 for (int i = 0; i < 5; ++i) { AIBaseBuilder::update(*s.w.logic, s.bot); s.w.frames(1); }
 CHECK((payer->hasUpgradeComplete(upgrade) || lab->getProductionUpdate()->isUpgradeInQueue(upgrade)));
 CHECK(record->reserve() == 800);
 CHECK((std::uint32_t)AIBaseBuilder::moneyPlusReserve(*s.w.logic, *payer) == before - 200);
 s.w.logic->skirmishAI().reset();
}

TEST_CASE("strategy AI: local base exclusion turns enemy computers away and can be disabled")
{
 Scenario s;
 auto *unit = s.w.unit("CounterPike", 'A', 400, 500);
 auto *protectedPlayer = s.w.players.findPlayerWithName("Bob");
 REQUIRE(protectedPlayer);
 s.w.logic->setAIBaseExclusion(protectedPlayer->getPlayerIndex(), {100,500,0}, 500);
 Coord3D goal;
 REQUIRE(s.w.logic->aiBaseExclusion(*unit, goal));
 CHECK(s.w.logic->hasPeaceExclusion(*unit));
 CHECK_FALSE(s.w.logic->peaceExclusionAllows(*unit, {300,500,0}, true));
 CHECK(s.w.logic->peaceExclusionAllows(*unit, {900,500,0}, true));
 CHECK(goal.x == 1200);
 CHECK(goal.y == 500);
 s.decide();
 CHECK(unit->getAIUpdateInterface()->isMoving());
 s.w.frames(20);
 // tolerance: movement must increase distance from the protected center; exact path steps depend on the synthetic locomotor.
 CHECK(unit->getPosition()->x > 400);
 auto *friendly = s.w.unit("CounterPike", 'B', 400, 500);
 CHECK_FALSE(s.w.logic->aiBaseExclusion(*friendly, goal));
 CHECK_FALSE(s.w.logic->hasPeaceExclusion(*friendly));
 Coord3D far{1500,500,0}; unit->setPosition(&far);
 CHECK_FALSE(s.w.logic->aiBaseExclusion(*unit, goal));
 s.w.logic->setAIBaseExclusion(protectedPlayer->getPlayerIndex(), {100,500,0}, 0);
 Coord3D near{400,500,0};unit->setPosition(&near);
 CHECK_FALSE(s.w.logic->aiBaseExclusion(*unit, goal));
}

TEST_CASE("strategy AI: source rules issue no enhanced micro, opt-in enables it")
{
	Scenario s;
	auto *archer = s.w.unit("CounterBow", 'A', 100, 500);
	auto *enemy = s.w.unit("CounterHorse", 'B', 130, 500);
	s.w.logic->settings().enhancedAI = false;
	CHECK(archer->getAIUpdateInterface()->currentVictimId() == INVALID_ID);
	s.decide();
	CHECK(archer->getAIUpdateInterface()->currentVictimId() == INVALID_ID);
	s.w.logic->settings().enhancedAI = true;
	s.decide();
	CHECK(archer->getAIUpdateInterface()->currentVictimId() == enemy->getID());
}
