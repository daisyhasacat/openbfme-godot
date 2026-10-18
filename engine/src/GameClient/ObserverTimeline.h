// OpenBFME. GPL-3.0.
#pragma once

#include "Common/ObserverEvent.h"
#include "GameLogic/SimMath.h"
#include <map>
#include <tuple>
#include <vector>

// Presentation-only event index. Damage is grouped into 300-unit cells and five-second windows.
class ObserverTimeline
{
public:
	void add(const ObserverEvent &event)
	{
		if (event.kind == ObserverEvent::Damage)
		{
			const unsigned window = event.frame / 25;
			if (window != m_window)
			{
				m_damage.clear();
				m_window = window;
			}
			const auto key = std::make_tuple(SimMath::floorToInt(SimMath::divf32(event.position.x, 300.0f)),
				SimMath::floorToInt(SimMath::divf32(event.position.y, 300.0f)), event.player);
			auto found = m_damage.find(key);
			if (found != m_damage.end())
			{
				ObserverEvent &prior = m_events[found->second];
				prior.weight = SimMath::addf32(prior.weight, event.weight);
				prior.base = prior.base || event.base;
				return;
			}
			m_damage[key] = m_events.size();
		}
		m_events.push_back(event);
	}
	const std::vector<ObserverEvent> &events() const { return m_events; }

private:
	unsigned m_window = 0;
	std::map<std::tuple<int, int, int>, size_t> m_damage;
	std::vector<ObserverEvent> m_events;
};
