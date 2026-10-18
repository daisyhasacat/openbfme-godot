// OpenBFME. COMBAT-1 tests: the attack AI - the attack order (approach, aim, fire, wait), the victim's death, idle acquisition (the mood scan), attack-move, the attack commands of
// the command list and the AI of a dead unit. Synthetic data (combattest::CombatWorld), no retail files.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"

using namespace combattest;

TEST_CASE("combat ai: an attack order walks to the victim, swings until it dies, then the unit is idle again and the victim is gone")
{
	CombatWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *a = w.unit("Slasher", 'A', 300, 300);
	Object *v = w.unit("Dummy", 'B', 400, 300);
	w.frames(2);
	const ObjectID vid = v->getID();
	AIUpdateInterface *ai = a->getAIUpdateInterface();
	CHECK(ai->isIdle());
	CHECK_FALSE(ai->isAttacking());
	REQUIRE(ai->aiAttackObject(v, CMD_FROM_PLAYER));
	CHECK(ai->isAttacking());
	CHECK(ai->currentVictimId() == vid);
	// the unit walks (55 per second = 11 per frame) the 100 units less its reach, the first hit lands after the pre-attack
	w.frames(3);
	CHECK(a->getPosition()->x > 300.0f);
	CHECK(w.health(v) == 100.0f);
	w.runUntil([&] { return w.byId(vid) == nullptr; }, 400);
	CHECK(w.byId(vid) == nullptr);              // 100 health / (30 SLASH, the victim's PlainArmor 50%) = 7 hits
	CHECK(w.combat().counters().kills == 1);
	CHECK(w.combat().counters().damageApplications == 7);
	w.frames(3);
	CHECK(ai->isIdle());
	CHECK_FALSE(ai->isAttacking());
	CHECK(ai->currentVictimId() == INVALID_ID);
	// it stood next to the victim; lane PHYS-1: the attack path (RW 0x6FC18E's forward probe, half-cell steps with an extra 10 of reach) ends at 377.5 (was > 380 with the
	// move to the victim's centre)
	CHECK(a->getPosition()->x > 370.0f);
	CHECK(a->getPosition()->x < 400.0f);
}

TEST_CASE("combat ai: an attack order on an ally, on a dead object or by a unit without a weapon is refused")
{
	CombatWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	Object *ally = w.unit("Dummy", 'A', 350, 300);
	Object *enemy = w.unit("Dummy", 'B', 400, 300);
	Object *unarmed = w.unit("Dummy", 'A', 250, 300);
	w.frames(2);
	CHECK_FALSE(a->getAIUpdateInterface()->aiAttackObject(ally, CMD_FROM_PLAYER));      // an ally is no target
	CHECK(a->getAIUpdateInterface()->aiForceAttackObject(ally, CMD_FROM_PLAYER));       // unless the player forces it
	a->getAIUpdateInterface()->aiIdle(CMD_FROM_PLAYER);
	CHECK_FALSE(unarmed->getAIUpdateInterface()->aiAttackObject(enemy, CMD_FROM_PLAYER)); // no weapon
	enemy->kill(DEATH_NORMAL);
	CHECK_FALSE(a->getAIUpdateInterface()->aiAttackObject(enemy, CMD_FROM_PLAYER));       // already dead
	CHECK(a->getAIUpdateInterface()->isIdle());
}

TEST_CASE("combat ai: the idle scan - an enemy in the vision range is attacked on sight, one outside it and an ally are not, the scan can be switched off")
{
	CombatWorld w;
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	Object *far = w.unit("Dummy", 'B', 300, 600);   // 300 away, vision range 150
	Object *ally = w.unit("Dummy", 'A', 340, 300);
	w.frames(10);
	CHECK(a->getAIUpdateInterface()->isIdle());
	CHECK(w.health(far) == 100.0f);
	CHECK(w.health(ally) == 100.0f);
	Object *near = w.unit("Dummy", 'B', 300, 400);  // 100 away
	// MoodAttackCheckRate 400 ms = 2 frames: the unit finds it within a few frames and walks to it
	w.frames(4);
	CHECK(a->getAIUpdateInterface()->isAttacking());
	CHECK(a->getAIUpdateInterface()->currentVictimId() == near->getID());
	w.runUntil([&] { return w.health(near) < 100.0f; }, 80);
	CHECK(w.health(near) < 100.0f);
	CHECK(w.health(far) == 100.0f);
	CHECK(w.health(ally) == 100.0f);
}

TEST_CASE("combat ai: the nearest enemy is chosen, a tie goes to the lower object id")
{
	CombatWorld w;
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	Object *first = w.unit("Dummy", 'B', 380, 300);   // 80 east
	Object *second = w.unit("Dummy", 'B', 220, 300);  // 80 west: the same distance, the higher id
	Object *nearer = nullptr;
	w.frames(4);
	CHECK(a->getAIUpdateInterface()->currentVictimId() == first->getID());
	nearer = w.unit("Dummy", 'B', 300, 340);          // closer than both, but the current attack goes on
	w.frames(3);
	CHECK(a->getAIUpdateInterface()->currentVictimId() == first->getID());
	CHECK(nearer != nullptr);
	CHECK(second != nullptr);
}

TEST_CASE("combat ai: with the idle scan switched off (the movement sweeps) nothing is attacked")
{
	CombatWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	Object *v = w.unit("Dummy", 'B', 330, 300);
	w.frames(20);
	CHECK(a->getAIUpdateInterface()->isIdle());
	CHECK(w.health(v) == 100.0f);
	w.combat().setAutoAcquireEnabled(true);
	// the unit had gone to its long idle sleep (kIdleCountdownDelay 10 frames + up to 10 frames of random offset): it looks again when that is over
	w.frames(25);
	CHECK(a->getAIUpdateInterface()->isAttacking());
}

TEST_CASE("combat ai: attack-move marches to the goal, attacks the enemy it meets on the way and then resumes the march")
{
	const char *target="Dummy";
	SUBCASE("enemy unit") {}
	SUBCASE("enemy building") { target="MarchBuilding"; }
	CombatWorld w("ChildObject MarchBuilding Dummy\n KindOf = STRUCTURE IMMOBILE SELECTABLE SCORE\nEnd\n");
	Object *a = w.unit("Slasher", 'A', 100, 300);
	Object *v = w.unit(target, 'B', 300, 330);
	w.frames(2);
	const ObjectID vid = v->getID();
	w.select(w.playerIndex("Alice"), { a });
	w.moveTo(w.playerIndex("Alice"), 600.0f, 300.0f, MSG_DO_ATTACKMOVETO);
	w.frames(2);
	CHECK(w.aiCommands.stats().attackMoves == 1);
	// the vision range of the Slasher is 150: the enemy 200 units ahead is met after the unit has walked about 50; the unit fights it
	bool attacked = false;
	for (int f = 0; f < 120 && !attacked; ++f)
	{
		w.frames(1);
		attacked = a->getAIUpdateInterface()->isAttacking();
	}
	CHECK(attacked);
	w.runUntil([&] { return w.byId(vid) == nullptr; }, 300);
	CHECK(w.byId(vid) == nullptr);
	// then the march goes on to (600, 300)
	w.runUntil([&] { return std::hypot(a->getPosition()->x - 600.0f, a->getPosition()->y - 300.0f) < 15.0f; }, 300);
	CHECK(std::hypot(a->getPosition()->x - 600.0f, a->getPosition()->y - 300.0f) < 15.0f);
	w.frames(25); // the move completes, the idle state ends the march
	CHECK(a->getAIUpdateInterface()->isIdle());
	CHECK_FALSE(a->getAIUpdateInterface()->attackMoveActive());
}

TEST_CASE("combat ai: a plain move order cancels an attack-move")
{
	CombatWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *a = w.unit("Slasher", 'A', 100, 300);
	w.frames(2);
	a->getAIUpdateInterface()->armAttackMove(Coord3D{ 500.0f, 300.0f, 0.0f });
	CHECK(a->getAIUpdateInterface()->attackMoveActive());
	a->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 200.0f, 300.0f, 0.0f }, CMD_FROM_PLAYER);
	CHECK_FALSE(a->getAIUpdateInterface()->attackMoveActive());
}

TEST_CASE("combat ai: the attack commands go through the command list (select, MSG_DO_ATTACK_OBJECT) and are counted; a force attack on an ally is accepted, a plain one is not")
{
	CombatWorld w;
	w.combat().setAutoAcquireEnabled(false);
	const int alice = w.playerIndex("Alice");
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	Object *enemy = w.unit("Dummy", 'B', 360, 300);
	Object *ally = w.unit("Dummy", 'A', 240, 300);
	w.frames(2);
	w.select(alice, { a });
	w.attackMessage(alice, enemy);
	w.frames(1);
	CHECK(w.aiCommands.stats().attacks == 1);
	CHECK(w.aiCommands.stats().attackOrdersAccepted == 1);
	CHECK(a->getAIUpdateInterface()->isAttacking());
	CHECK(a->getAIUpdateInterface()->currentVictimId() == enemy->getID());
	w.attackMessage(alice, ally);
	w.frames(1);
	CHECK(w.aiCommands.stats().attacks == 2);
	CHECK(w.aiCommands.stats().attackOrdersAccepted == 1); // refused: the ally is no enemy
	CHECK(a->getAIUpdateInterface()->currentVictimId() == enemy->getID());
	w.attackMessage(alice, ally, MSG_DO_FORCE_ATTACK_OBJECT);
	w.frames(1);
	CHECK(w.aiCommands.stats().forceAttacks == 1);
	CHECK(w.aiCommands.stats().attackOrdersAccepted == 2);
	CHECK(a->getAIUpdateInterface()->currentVictimId() == ally->getID());
	// an order for a victim that does not exist is dropped without a trace on the unit
	GameMessage gone(MSG_DO_ATTACK_OBJECT, alice);
	gone.appendObjectIDArgument(99999);
	w.send(gone);
	w.frames(1);
	CHECK(w.aiCommands.stats().attackOrdersAccepted == 2);
	// only the player's own selection obeys: Bob cannot order Alice's unit
	GameMessage bob(MSG_DO_ATTACK_OBJECT, w.playerIndex("Bob"));
	bob.appendObjectIDArgument(enemy->getID());
	w.send(bob);
	w.frames(1);
	CHECK(a->getAIUpdateInterface()->currentVictimId() == ally->getID());
}

TEST_CASE("combat ai: when the victim is killed by somebody else the attacker goes idle")
{
	CombatWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	Object *v = w.unit("Dummy", 'B', 400, 300);
	w.frames(2);
	a->getAIUpdateInterface()->aiAttackObject(v, CMD_FROM_PLAYER);
	w.frames(3);
	v->kill(DEATH_NORMAL);
	w.frames(3);
	CHECK(a->getAIUpdateInterface()->isIdle());
	CHECK(a->getAIUpdateInterface()->currentVictimId() == INVALID_ID);
}

TEST_CASE("combat ai: a dead unit's AI stops, it takes no orders and its footprint leaves the pathfinder at once")
{
	CombatWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	w.frames(3);
	const ObjectID id = a->getID();
	CHECK(w.inPathfinder(id));
	a->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 500.0f, 300.0f, 0.0f }, CMD_FROM_PLAYER);
	w.frames(3);
	a->kill(DEATH_NORMAL);
	REQUIRE(a->isEffectivelyDead());
	CHECK(a->getAIUpdateInterface()->isAiInDeadState());
	CHECK_FALSE(w.inPathfinder(id));
	const float x = a->getPosition()->x;
	w.frames(5);
	CHECK(a->getPosition()->x == doctest::Approx(x)); // it does not walk on
	CHECK_FALSE(a->getAIUpdateInterface()->aiAttackObject(a, CMD_FROM_PLAYER));
}

TEST_CASE("combat ai: two swordsmen ordered onto each other fight to the death of one; the survivor is idle and hurt; every hit is 5 (10 SLASH through 50%)")
{
	CombatWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	Object *b = w.unit("Swordsman", 'B', 330, 300);
	w.frames(2);
	const ObjectID ia = a->getID(), ib = b->getID();
	a->getAIUpdateInterface()->aiAttackObject(b, CMD_FROM_PLAYER);
	b->getAIUpdateInterface()->aiAttackObject(a, CMD_FROM_PLAYER);
	w.runUntil([&] { return w.byId(ia) == nullptr || w.byId(ib) == nullptr || (w.byId(ia)->isEffectivelyDead() || w.byId(ib)->isEffectivelyDead()); }, 600);
	const bool aDead = !w.byId(ia) || w.byId(ia)->isEffectivelyDead();
	const bool bDead = !w.byId(ib) || w.byId(ib)->isEffectivelyDead();
	CHECK(aDead != bDead); // exactly one dies first (the other was in the middle of its swing)
	Object *survivor = aDead ? w.byId(ib) : w.byId(ia);
	REQUIRE(survivor != nullptr);
	CHECK(w.health(survivor) > 0.0f);
	CHECK(w.health(survivor) < 100.0f);
	const float lost = 100.0f - w.health(survivor);
	CHECK(std::fmod(lost, 5.0f) == doctest::Approx(0.0f)); // whole 5s
	w.frames(4);
	CHECK(survivor->getAIUpdateInterface()->isIdle());
}

TEST_CASE("combat ai: idle and attack-move acquisition use runtime vision and computer mood")
{
	CombatWorld w;
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	Object *enemy = w.unit("Dummy", 'B', 540, 300);
	AIUpdateInterface *ai = a->getAIUpdateInterface();
	w.playerOf('A')->setPlayerType(PLAYER_COMPUTER, true);
	w.logic->settings().alertRangeModifier = 2.0f;
	w.logic->settings().aggressiveRangeModifier = 3.0f;
	CHECK(ai->nextMoodTarget() == nullptr);
	CHECK(ai->attackMoveTarget() == nullptr);
	ai->setAttitude(1);
	CHECK(ai->nextMoodTarget() == enemy);
	CHECK(ai->attackMoveTarget() == enemy);
	ai->setAttitude(-2);
	CHECK(ai->nextMoodTarget() == nullptr);
	CHECK(ai->attackMoveTarget() == nullptr);
	// Human control ignores AI attitude range scaling.
	w.playerOf('A')->setPlayerType(PLAYER_HUMAN, true);
	ai->setAttitude(2);
	CHECK(ai->nextMoodTarget() == nullptr);
}

TEST_CASE("combat ai: attack-move respects hold fire and ability busy acquisition gates")
{
	CombatWorld w;
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	Object *enemy = w.unit("Dummy", 'B', 380, 300);
	AIUpdateInterface *ai = a->getAIUpdateInterface();
	CHECK(ai->attackMoveTarget() == enemy);
	for (const char *name : { "NO_AUTO_ACQUIRE", "SPECIAL_ABILITY_PACKING_UNPACKING_OR_USING", "IGNORE_AI_COMMAND" })
	{
		const unsigned status = (unsigned)CombatNames::status(name);
		a->setStatus(status, true);
		CHECK(ai->nextMoodTarget() == nullptr);
		CHECK(ai->attackMoveTarget() == nullptr);
		a->setStatus(status, false);
		CHECK(ai->attackMoveTarget() == enemy);
	}
}

TEST_CASE("skirmish ai: pending tactic teams survive production delays; destroyed handed-over teams end")
{
	CombatWorld w;
	SkirmishAIStore store(w.w.keys);
	ArmyDefinition army;
	AISkirmishPlayer ai;
	ai.playerIndex = w.playerIndex("Alice");
	ai.store = &store;
	ai.army = &army;
	AITactic tactic;
	tactic.kind = "SimpleAttack";
	tactic.teams.emplace_back();
	ai.brain.tactics.push_back(tactic);
	for (int frame = 0; frame < 60; ++frame)
	{
		AITacticalAI::updateBrain(*w.logic, ai, &ai);
		w.frames(1);
	}
	REQUIRE(ai.brain.tactics.size() == 1);
	REQUIRE(ai.brain.tactics[0].teams.size() == 1);
	CHECK(ai.brain.tactics[0].teams[0].wantsMembers);
	CHECK_FALSE(ai.brain.tactics[0].started);
	// Once a team was created and launched, losing its last member ends the tactic.
	ai.brain.tactics[0].teams[0].handedOver = true;
	ai.brain.tactics[0].teams[0].wantsMembers = false;
	ai.brain.tactics[0].started = true;
	AITacticalAI::updateBrain(*w.logic, ai, &ai);
	CHECK(ai.brain.tactics[0].ended);
}

TEST_CASE("combat ai: acquisition includes large targets whose edge is in vision range")
{
	CombatWorld w("Object LargeTarget\n KindOf = STRUCTURE IMMOBILE\n Geometry = CYLINDER\n GeometryMajorRadius = 200\n GeometryHeight = 20\n Body = ActiveBody ModuleTag_Body\n MaxHealth = 1000\n End\nEnd\n");
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	Object *target = w.unit("LargeTarget", 'B', 630, 300);
	// Centre is outside vision + 80; its edge is only 130 away (vision 150).
	CHECK(a->getAIUpdateInterface()->attackMoveTarget() == target);
}

TEST_CASE("combat ai: idle-only acquisition flags do not enable idle scanning without YES")
{
	// Give a copy of the swordsman a different acquisition flag through the INI parser.
	std::string objects = kCombatObjects;
	objects = objects.substr(objects.find("Object Swordsman"));
	objects.resize(objects.find("Object Dummy"));
	objects.replace(objects.find("Object Swordsman"), std::string("Object Swordsman").size(), "Object FlaggedSwordsman");
	const auto flag = objects.find("AutoAcquireEnemiesWhenIdle = Yes");
	REQUIRE(flag != std::string::npos);
	objects.replace(flag, std::string("AutoAcquireEnemiesWhenIdle = Yes").size(), "AutoAcquireEnemiesWhenIdle = ATTACK_BUILDINGS");
	CombatWorld w(objects.c_str());
	Object *a = w.unit("FlaggedSwordsman", 'A', 300, 300);
	Object *enemy = w.unit("Dummy", 'B', 380, 300);
	CHECK(a->getAIUpdateInterface()->moodCheckInterval() == 0);
	CHECK(a->getAIUpdateInterface()->nextMoodTarget() == nullptr);
	// An explicit attack-move still fights even if idle acquisition is disabled.
	CHECK(a->getAIUpdateInterface()->attackMoveTarget() == enemy);
}

TEST_CASE("combat ai: a runtime VISION buff expands nearby acquisition and removing it restores range")
{
	CombatWorld w;
	AttributeModifierStore modifiers;
	modifiers.registerBlock(w.w.fx.env.blocks);
	AttributeModifierPool::registerClass(w.w.modules);
	struct RestoreStore
	{
		AttributeModifierStore *saved = TheAttributeModifierStore;
		~RestoreStore() { TheAttributeModifierStore = saved; }
	} restore;
	TheAttributeModifierStore = &modifiers;
	REQUIRE(w.w.load("ModifierList FarSight\n Category = BUFF\n Duration = 0\n Modifier = VISION 100%\nEnd\n"
		"ChildObject Scout Swordsman\n Behavior = AttributeModifierPoolUpdate ModuleTag_Pool\n End\nEnd\n").empty());
	Object *scout = w.unit("Scout", 'A', 300, 300);
	Object *enemy = w.unit("Dummy", 'B', 540, 300);
	AIUpdateInterface *ai = scout->getAIUpdateInterface();
	CHECK(ai->nextMoodTarget() == nullptr);
	REQUIRE(scout->addAttributeModifier("FarSight", -1));
	w.frames(1);
	CHECK(ai->nextMoodTarget() == enemy);
	CHECK(ai->attackMoveTarget() == enemy);
	scout->removeAttributeModifier("FarSight");
	CHECK(ai->nextMoodTarget() == nullptr);
	CHECK(ai->attackMoveTarget() == nullptr);
}
