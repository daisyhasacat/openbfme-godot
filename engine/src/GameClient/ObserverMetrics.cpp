// OpenBFME. GPL-3.0.
#include "GameClient/ObserverMetrics.h"

#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"

void ObserverMetrics::capture(GameLogic &logic)
{
	if (m_captured && m_frame == logic.getFrame())
	{
		return;
	}
	m_captured = true;
	m_frame = logic.getFrame();
	m_players.clear();
	std::map<int, size_t> indices;
	const UnsignedInt window = 60 * LOGICFRAMES_PER_SECOND;
	const UnsignedInt first = m_frame > window ? m_frame - window : 0;
	const auto &income = logic.economy().incomeLog();
	const bool incomeComplete = logic.economy().incomePayments() <= income.size() ||
		(!income.empty() && income.front().frame <= first);
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		const Player *p = logic.players().getNthPlayer(i);
		const PlayerTemplate *pt = p ? p->getPlayerTemplate() : nullptr;
		if (!pt || !pt->m_playableSide || pt->m_isObserver)
		{
			continue;
		}
		indices[i] = m_players.size();
		m_players.emplace_back();
		auto &v = m_players.back();
		v.player = i;
		v.money = p->getMoney()->countMoney();
		v.cpUsed = p->commandPoints().getUsage();
		v.cpLimit = p->commandPointLimit();
		v.powerPoints = p->science().getSciencePurchasePoints();
		v.unitsLost = p->getScoreKeeper().unitsLost();
		v.incomeComplete = incomeComplete;
		for (const HeroRecord &h : p->heroes().records())
		{
			if (h.dead || h.startFrame != -1)
			{
				ObserverPlayerMetrics::Hero hero;
				hero.name = h.templateName;
				hero.rank = h.rank;
				hero.state = h.startFrame != -1 ? (h.dead ? "reviving" : "recruiting") : "dead";
				v.heroes.push_back(std::move(hero));
			}
		}
	}
	for (const auto &payment : income)
	{
		const auto it = indices.find(payment.playerIndex);
		if (it != indices.end() && payment.frame > first && payment.frame <= m_frame)
		{
			m_players[it->second].resourceIncome += payment.amount;
		}
	}
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		const Player *p = o->getControllingPlayer();
		const auto it = indices.find(p ? p->getPlayerIndex() : -1);
		if (it == indices.end() || o->isEffectivelyDead() || o->isDestroyed())
		{
			continue;
		}
		auto &v = m_players[it->second];
		const ThingTemplate &tt = *o->getTemplate()->getFinalOverride();
		if (!o->isKindOfName("STRUCTURE") && !o->isKindOfName("DOZER") && !o->isKindOfName("PROJECTILE") &&
			!o->isKindOfName("HORDE_MEMBER") && !(o->getContainedBy() && o->getContainedBy()->isKindOfName("HORDE")) &&
			(o->getAIUpdateInterface() || o->isKindOfName("CAN_ATTACK") || o->isKindOfName("HORDE") || o->isKindOfName("HERO")))
		{
			auto &type = v.army[tt.getName()];
			++type.count;
			if (const FieldValue *label = tt.findField("DisplayName"))
			{
				if (const auto *s = std::get_if<std::string>(label))
				{
					type.label = *s;
				}
			}
			const FieldValue *cost = tt.findField("BuildCost");
			if (!cost || !std::holds_alternative<long long>(*cost))
			{
				v.armyComplete = false;
			}
			else
			{
				v.armyValue += BuildAssistant::buildCost(tt);
			}
		}
		if (o->isKindOfName("HERO"))
		{
			ObserverPlayerMetrics::Hero h;
			h.object = o->getID();
			h.name = tt.getName();
			h.state = "alive";
			h.rank = o->getExperienceTracker() ? o->getExperienceTracker()->getRank() : 0;
			if (const auto *body = o->getBodyModule())
			{
				h.health = body->getHealth();
				h.maxHealth = body->getMaxHealth();
			}
			v.heroes.push_back(std::move(h));
		}
		if (const auto *pu = o->getProductionUpdate())
		{
			for (const ProductionEntry *e = pu->firstProduction(); e; e = pu->nextProduction(e))
			{
				ObserverPlayerMetrics::Production q;
				q.producer = o->getID();
				q.percent = e->percentComplete;
				q.remaining = e->quantityRemaining();
				q.kind = "unresolved";
				if (e->type == PRODUCTION_BUILD_INDEX)
				{
					if (const HeroRecord *h = p->heroes().findByProductionID(e->productionID))
					{
						q.name = h->templateName;
						q.kind = h->dead ? "revive" : "hero";
						const int index = (int)(h - p->heroes().records().data());
						q.percent = (float)(100.0 * p->heroes().progressAt(logic, *p, index, o));
					}
				}
				else if (e->objectToProduce)
				{
					q.name = e->objectToProduce->getName();
					q.kind = "unit";
				}
				else if (e->upgradeToResearch)
				{
					q.name = e->upgradeToResearch->getUpgradeName();
					q.kind = "upgrade";
				}
				v.production.push_back(std::move(q));
			}
		}
	}
	if (m_history.empty() || m_frame % LOGICFRAMES_PER_SECOND == 0)
	{
		Point point;
		point.frame = m_frame;
		for (const auto &p : m_players)
		{
			point.players.push_back(p);
		}
		m_history.push_back(std::move(point));
	}
}
