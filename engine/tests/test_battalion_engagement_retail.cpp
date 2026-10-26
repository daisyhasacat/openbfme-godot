// OpenBFME unit tests. GPL-3.0.
// Battalion engagement with the retail data: four GondorFighterHordes and four MordorFighterHordes face each other across the clear arena of Evendim
// (the cavalry / archer tests' ground), each horde ordered by its player onto the enemy horde opposite it (the auto acquisition left on, as in a game).
// For 60 game seconds every frame records, per horde, whether its melee began (HordeContain::meleeEngaged: the formation froze on the engaged target,
// RW 0x86D75E), when, and how many of its men attack an enemy (their AI's goal while attacking); the table is printed and every horde must engage.
// SKIPs without the games (ROTWK_INSTALL / BFME2_INSTALL).

#include "doctest.h"

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Thing/ThingFactory.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
int factionIndex(const PlayerTemplateStore &store, const std::string &name)
{
	for (int i = 0; i < store.getPlayerTemplateCount(); ++i)
	{
		if (store.getNthPlayerTemplate(i)->getName() == name)
		{
			return i;
		}
	}
	return -1;
}

Player *playerAtStart(GameLogic &logic, int start)
{
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		Player *p = logic.players().getNthPlayer(i);
		if (p && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide && p->getMultiplayerStartIndex() == start)
		{
			return p;
		}
	}
	return nullptr;
}

// the object a man is attacking (its AI's goal while attacking), or null
Object *attackGoal(Object *m)
{
	AIUpdateInterface *ai = m->getAIUpdateInterface();
	const AIStateMachine *sm = ai ? ai->stateMachineOrNull() : nullptr;
	return ai && ai->isAttacking() && sm ? sm->goalObject() : nullptr;
}

struct Battalion
{
	std::string side;
	Object *horde = nullptr;
	Object *foe = nullptr;     ///< the enemy horde it was ordered onto
	int startMen = 0;
	int firstEngagedFrame = -1; ///< the frame its melee began (-1: never)
	int engagedFrames = 0;     ///< frames it spent engaged
	size_t peakFighting = 0;   ///< the most of its men attacking an enemy in one frame
	std::set<ObjectID> fought; ///< its men that attacked an enemy at least once
	int endMen = 0;
};

HordeContain *contain(Object *horde)
{
	return horde ? dynamic_cast<HordeContain *>(horde->getContain()) : nullptr;
}
} // namespace

TEST_CASE("battalions retail: four Gondor and four Mordor fighter hordes ordered onto each other all engage; the table of who fought is printed")
{
	OPENBFME_REQUIRE_START(s);
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s->mount->fs, cache, &error), error);
	}
	NewGameMessage m;
	m.game.mapName = "maps/map mp evendim/map mp evendim.map";
	m.game.seed = 2;
	m.game.startingCash = 1500;
	const char *factions[2] = { "FactionMen", "FactionMordor" };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = SLOT_PLAYER;
		slot.name = i == 0 ? u"Player0" : u"Player1";
		slot.playerTemplate = factionIndex(s->world->playerTemplates(), factions[i]);
		slot.startPos = i;
		slot.color = i;
		slot.teamNumber = i;
	}
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s->world->playerTemplates(), s->settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	ArchiveW3DFileSource source(*s->mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s->world, *s->mount->fs, assets, s->options);
	LiveGame::Options o;
	o.start = &start;
	REQUIRE_MESSAGE(game.load(o, &error), error);
	GameLogic &logic = game.logic();
	Player *men = playerAtStart(logic, 0);
	Player *mordor = playerAtStart(logic, 1);
	REQUIRE(men != nullptr);
	REQUIRE(mordor != nullptr);

	// two lines of four, 300 apart, the rows 150 apart, each army facing the other
	const int kPerSide = 4;
	const float gondorX = 1550.0f, mordorX = 1850.0f, firstY = 375.0f, rowGap = 150.0f;
	std::vector<Battalion> battalions;
	for (int side = 0; side < 2; ++side)
	{
		for (int i = 0; i < kPerSide; ++i)
		{
			Battalion b;
			b.side = side == 0 ? "Gondor" : "Mordor";
			b.horde = logic.newObject(logic.things().findTemplate(side == 0 ? "GondorFighterHorde" : "MordorFighterHorde"),
				(side == 0 ? men : mordor)->getDefaultTeam(), {});
			REQUIRE(b.horde != nullptr);
			const float x = side == 0 ? gondorX : mordorX, y = firstY + rowGap * (float)i;
			Coord3D p{ x, y, logic.getGroundHeight(x, y) };
			b.horde->setPosition(&p);
			b.horde->setOrientation(side == 0 ? 0.0f : 3.14159265f);
			battalions.push_back(b);
		}
	}
	game.advance(2.0); // the members take their slots
	for (int i = 0; i < kPerSide; ++i)
	{
		battalions[(size_t)i].foe = battalions[(size_t)(kPerSide + i)].horde;
		battalions[(size_t)(kPerSide + i)].foe = battalions[(size_t)i].horde;
	}
	for (Battalion &b : battalions)
	{
		HordeContain *hc = contain(b.horde);
		REQUIRE(hc != nullptr);
		b.startMen = (int)hc->getContainedItemsList()->size();
		REQUIRE(b.startMen >= 10); // tolerance: this engagement scenario needs at least ten starting fighters per horde; casualties and engagement are checked separately.
		REQUIRE(b.horde->getAIUpdateInterface()->aiAttackObject(b.foe, CMD_FROM_PLAYER));
	}

	const int kFrames = 300; // 60 game seconds
	for (int f = 1; f <= kFrames; ++f)
	{
		game.advance(0.2);
		for (Battalion &b : battalions)
		{
			if (b.horde->isDestroyed() || b.horde->isEffectivelyDead())
			{
				continue;
			}
			HordeContain *hc = contain(b.horde);
			if (!hc)
			{
				continue;
			}
			if (hc->meleeEngaged())
			{
				++b.engagedFrames;
				if (b.firstEngagedFrame < 0)
				{
					b.firstEngagedFrame = f;
				}
			}
			size_t fighting = 0;
			for (Object *man : *hc->getContainedItemsList())
			{
				Object *goal = attackGoal(man);
				if (goal && man->getRelationship(*goal) == ENEMIES)
				{
					++fighting;
					b.fought.insert(man->getID());
				}
			}
			b.peakFighting = std::max(b.peakFighting, fighting);
		}
	}

	int engaged = 0, menFought = 0, menTotal = 0;
	std::ostringstream table;
	table << "battalion | men at start | melee began (frame) | frames engaged | peak men attacking | men that fought | men at the end\n";
	for (size_t i = 0; i < battalions.size(); ++i)
	{
		Battalion &b = battalions[i];
		HordeContain *hc = b.horde->isDestroyed() ? nullptr : contain(b.horde);
		b.endMen = hc ? (int)hc->getContainedItemsList()->size() : 0;
		engaged += b.firstEngagedFrame >= 0 ? 1 : 0;
		menFought += (int)b.fought.size();
		menTotal += b.startMen;
		table << b.side << " " << (i % kPerSide + 1) << " | " << b.startMen << " | " << b.firstEngagedFrame << " | " << b.engagedFrames << " | " << b.peakFighting
			  << " | " << b.fought.size() << " | " << b.endMen << "\n";
	}
	MESSAGE(table.str());
	MESSAGE("battalions that engaged: " << engaged << " of " << battalions.size() << "; men that fought: " << menFought << " of " << menTotal);
	CHECK(engaged == (int)battalions.size());
}
