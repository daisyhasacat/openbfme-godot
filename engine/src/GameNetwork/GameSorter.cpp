// OpenBFME. GPL-3.0.
// Lane MENUWORK-1: GameSorter (see GameSorter.h for the target facts and the stop S-4751).
#include "GameNetwork/GameSorter.h"

#include <algorithm>

namespace
{
// _wcsicmp / _strcmpi (the C locale): both sides lowered (A-Z only), then compared as unsigned units
template <typename S> int compareNoCase(const S &a, const S &b)
{
	const size_t n = std::min(a.size(), b.size());
	for (size_t i = 0; i < n; ++i)
	{
		unsigned x = (unsigned)(typename std::make_unsigned<typename S::value_type>::type)a[i];
		unsigned y = (unsigned)(typename std::make_unsigned<typename S::value_type>::type)b[i];
		x = (x >= 'A' && x <= 'Z') ? x + 32 : x;
		y = (y >= 'A' && y <= 'Z') ? y + 32 : y;
		if (x != y)
		{
			return x < y ? -1 : 1;
		}
	}
	return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}
} // namespace

const char *GameSorter::stopLine()
{
	return "[S-4751] game list sort: RotWK's GameSorter (RW 0x9888B7 / 0x9881DE / 0x988418); its last tie (the GameInfo pointers' order) is the "
		   "list's own order, and War of the Ring games (mode 1) do not occur on OpenBFME's LAN";
}

void GameSorter::select(int key)
{
	// RW 0x9881DE (BFME2 0x980182, Rva00580172.cpp)
	const int old = m_primary;
	if ((key | KEY_DESCENDING) == (old | KEY_DESCENDING))
	{
		m_primary = old ^ KEY_DESCENDING;
	}
	else
	{
		m_secondary = old;
		m_primary = key;
	}
	m_changed = true;
	if (key == KEY_STATUS)
	{
		++m_statusCycle;
	}
}

bool GameSorter::takeChanged()
{
	const bool changed = m_changed;
	m_changed = false;
	return changed;
}

bool GameSorter::before(const Row &a, size_t ia, const Row &b, size_t ib) const
{
	// RW 0x988418 (BFME2 0x9803C0, Rva005803C0Compare.cpp)
	const int diff09 = a.openOrOccupied - b.openOrOccupied;
	const int diff08 = a.players - b.players;
	const int names = compareNoCase(a.name, b.name);
	const int diff21 = a.ping - b.ping;
	const int maps = a.mode == b.mode ? compareNoCase(a.map, b.map) : 0;
	// RW 0x98824E: the icons from the status cycle on
	int status[kIcons];
	for (int i = 0; i < kIcons; ++i)
	{
		const int index = (m_statusCycle + i) % kIcons;
		const bool second = b.icon[index];
		const bool first = a.icon[index];
		status[i] = first != second ? (first ? 1 : -1) : 0;
	}
	int mode = m_primary;
	for (int pass = 0; pass < 2; ++pass, mode = m_secondary)
	{
		const bool descending = (mode & KEY_DESCENDING) == KEY_DESCENDING;
		switch (mode & ~KEY_DESCENDING)
		{
		case KEY_NAME:
			if (names)
			{
				return descending ? names > 0 : names < 0;
			}
			break;
		case KEY_STATUS:
			for (const int value : status)
			{
				if (value)
				{
					return value > 0;
				}
			}
			break;
		case KEY_MAP:
			if (maps)
			{
				return descending ? maps > 0 : maps < 0;
			}
			break;
		case KEY_PING:
			if (diff21)
			{
				return descending ? diff21 > 0 : diff21 < 0;
			}
			break;
		case KEY_PLAYERS:
			if (diff09)
			{
				return descending ? diff09 > 0 : diff09 < 0;
			}
			if (diff08)
			{
				return diff08 < 0;
			}
			break;
		default:
			break;
		}
	}
	if (names)
	{
		return names < 0;
	}
	if (diff09)
	{
		return diff09 < 0;
	}
	if (maps)
	{
		return maps < 0;
	}
	if (diff21)
	{
		return diff21 < 0;
	}
	return ia < ib; // retail: a < b, the GameInfo pointers (INFERENCE S-4751: the list's order)
}

std::vector<size_t> GameSorter::order(const std::vector<Row> &rows) const
{
	std::vector<size_t> out(rows.size());
	for (size_t i = 0; i < out.size(); ++i)
	{
		out[i] = i;
	}
	std::sort(out.begin(), out.end(), [this, &rows](size_t ia, size_t ib) { return before(rows[ia], ia, rows[ib], ib); });
	return out;
}
