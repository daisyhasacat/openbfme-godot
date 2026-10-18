// OpenBFME. GPL-3.0.
#pragma once

#include "Common/GameCommon.h"
#include "GameLogic/ObjectTypes.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class GameLogic;

struct ObserverCounters
{
	int player = -1;
	std::int64_t money = 0, armyValue = 0, resourceIncome = 0;
	int cpUsed = 0, cpLimit = 0, powerPoints = 0, unitsLost = 0;
	bool armyComplete = true, incomeComplete = true;
};

struct ObserverPlayerMetrics : ObserverCounters
{
	struct ArmyType
	{
		std::string label;
		int count = 0;
	};
	struct Production
	{
		ObjectID producer = INVALID_ID;
		std::string name, kind;
		float percent = 0;
		int remaining = 0;
	};
	struct Hero
	{
		ObjectID object = INVALID_ID;
		std::string name, state;
		int rank = 0;
		float health = 0, maxHealth = 0;
	};
	std::map<std::string, ArmyType> army;
	std::vector<Production> production;
	std::vector<Hero> heroes;
};

// Caster statistics, not simulation state. Capture only on the simulation owner between frames.
class ObserverMetrics
{
public:
	struct Point
	{
		UnsignedInt frame = 0;
		std::vector<ObserverCounters> players;
	};
	void capture(GameLogic &logic);
	UnsignedInt frame() const { return m_frame; }
	const std::vector<ObserverPlayerMetrics> &players() const { return m_players; }
	const std::vector<Point> &history() const { return m_history; }

private:
	bool m_captured = false;
	UnsignedInt m_frame = 0;
	std::vector<ObserverPlayerMetrics> m_players;
	std::vector<Point> m_history;
};
