// OpenBFME. GPL-3.0.
//
// The obfme:Emotions block (lane SAVE-4c, docs/SAVES.md section 8): the game's side of TheEmotionSystem. OpenBFME native layout (stop S-4220): retail keeps
// the SCARY / HERO id list in TheEmotionSystem (RW 0xDE8C88 + 0x24) and its save list has no emotion block (GameState::init RW 0x6DF73E registers none:
// docs/SAVES-RETAIL.md section 2), so retail rebuilds the list from the loaded objects. OpenBFME saves it: its order is history (RW 0x835B0A moves the last id
// into a removed one's place), and the fear and hero scans walk it in that order. The per-object nuggets are the EmotionTrackerUpdate module's (SAVE-4b).

#include "GameLogic/System/EmotionSystem.h"

#include "Common/Thing/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <set>

// version 1: the ids in order, then the report's counters (diagnostics, saved so a loaded game's report goes on counting)
void EmotionWorld::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	if (!x.isLoading())
	{
		std::vector<ObjectID> ids = m_ids;
		x.xferVectorSize(ids, 4);
		for (ObjectID &id : ids)
		{
			x.xferObjectID(&id);
		}
		for (unsigned long long c : { m_unportedAI, m_unportedLua, m_unportedFlags, m_tauntScans })
		{
			std::uint64_t w = c;
			x.xferUnsignedInt64(&w);
		}
		return;
	}
	std::unique_ptr<PendingLoad> p = std::make_unique<PendingLoad>();
	x.xferVectorSize(p->ids, 4);
	for (ObjectID &id : p->ids)
	{
		x.xferObjectID(&id);
	}
	for (unsigned long long &c : p->counters)
	{
		std::uint64_t w = 0;
		x.xferUnsignedInt64(&w);
		c = w;
	}
	m_pendingLoad = std::move(p);
}

// every id checked first (rule 4: validate before linking), then the list is replaced
void EmotionWorld::loadPostProcess()
{
	if (!m_pendingLoad)
	{
		return;
	}
	std::unique_ptr<PendingLoad> p = std::move(m_pendingLoad);
	static const int kScary = ObjectTemplateInfoBuilder::kindOfIndex("SCARY"), kHero = ObjectTemplateInfoBuilder::kindOfIndex("HERO");
	std::set<ObjectID> seen;
	for (ObjectID id : p->ids)
	{
		const Object *obj = m_logic.findObjectByID(id);
		if (!obj)
		{
			throw XferException("emotions: the scary / hero list names object " + std::to_string(id) + ", which the loaded game lacks");
		}
		if (!((kScary >= 0 && obj->isKindOf((unsigned)kScary)) || (kHero >= 0 && obj->isKindOf((unsigned)kHero))))
		{
			throw XferException("emotions: the scary / hero list names object " + std::to_string(id) + " (" + obj->getTemplate()->getName() +
				"), which is neither SCARY nor HERO");
		}
		if (!seen.insert(id).second)
		{
			throw XferException("emotions: the scary / hero list names object " + std::to_string(id) + " twice");
		}
	}
	m_ids = std::move(p->ids);
	m_unportedAI = p->counters[0];
	m_unportedLua = p->counters[1];
	m_unportedFlags = p->counters[2];
	m_tauntScans = p->counters[3];
}
