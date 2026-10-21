// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GUI/AptScreens/AptStats.h (lane ONLINEHUB-1).

#include "GameClient/GUI/AptScreens/AptStats.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/Gadget.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/ShellEnvironment.h"
#include "GameClient/GUI/Skirmish/SkirmishSetup.h"
#include "GameClient/GUI/WindowManager.h"
#include "GameNetwork/OnlineLobbyClient.h"
#include "GameNetwork/OnlineStart.h"
#include "GameNetwork/PlatformApiClient.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace
{
std::u16string u16(const std::string &utf8)
{
	std::u16string out;
	if (!OnlineStart::utf8ToUtf16(utf8, out))
	{
		out.clear();
		for (char c : utf8)
		{
			out.push_back((unsigned char)c < 0x80 ? (char16_t)c : u'?');
		}
	}
	return out;
}

std::u16string num(std::int64_t n)
{
	return u16(std::to_string(n));
}

// a share as a whole percentage, rounded half up in integers (display only)
std::int64_t percent(std::int64_t part, std::int64_t whole)
{
	return whole > 0 ? (part * 200 + whole) / (whole * 2) : 0;
}

std::u16string pct(std::int64_t part, std::int64_t whole)
{
	return whole > 0 ? num(percent(part, whole)) + u"%" : std::u16string(u"-");
}

std::u16string rate(double r)
{
	return num((std::int64_t)(r * 100.0 + 0.5)) + u"%";
}

std::u16string duration(double seconds)
{
	const std::int64_t s = (std::int64_t)(seconds + 0.5);
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%lld:%02lld", (long long)(s / 60), (long long)(s % 60));
	return u16(buf);
}

// the map's leaf name ("maps/map mp fords of isen/map mp fords of isen.map" -> "map mp fords of isen") when the player's map cache has no name for it
std::string mapLeaf(const std::string &key)
{
	std::string leaf = key.substr(key.find_last_of("/\\") + 1);
	const std::size_t dot = leaf.rfind(".map");
	return dot == std::string::npos ? leaf : leaf.substr(0, dot);
}

const char *const kComboSkin = "window/apt/combobox.wnd";
// the filters' place: the list's empty header cell (Stats.swf: the list box's header band, stage x 75 .. 495, y 385 .. 415 at 1024 x 768)
constexpr int kFilterTop = 388;
constexpr int kFilterHeight = 24;
constexpr int kModeLeft = 80, kModeWidth = 200;
constexpr int kPeriodLeft = 290, kPeriodWidth = 200;
const char16_t *const kModeItems[3] = { u"All modes", u"Normal", u"Uber" };
const char *const kModeValues[3] = { "", "faithful", "uber" };
const char16_t *const kPeriodItems[3] = { u"Last 7 days", u"Last 30 days", u"All time" };
const char *const kPeriodValues[3] = { "7d", "30d", "all" };
const char16_t *const kUnavailable = u"Online services unavailable";
} // namespace

// RW 0xDA3AC0 (the loop RW 0x9F02C6 reads entries 0 .. 7)
const char *const AptStats::kSideTable[AptStats::kSides] = { "Men", "Elves", "Dwarves", "Isengard", "Mordor", "Wild", "Angmar", "Arnor" };

AptStats::AptStats(WindowManager &windows, ShellEnvironment &environment, Host host, GameWindow *owner)
	: m_windows(windows), m_env(environment), m_host(host), m_owner(owner)
{
	// BFME2 0x5DD609: the cards are the playable, non-observer PlayerTemplates in their order
	if (m_env.skirmish)
	{
		for (const SkirmishFaction &f : m_env.skirmish->factions())
		{
			if (f.playableSide && !f.isObserver)
			{
				m_cardTemplates.push_back(f.templateName);
			}
		}
	}
	else
	{
		m_windows.note("provider-unwired", "Stats.swf: the host gave no PlayerTemplates: the faction cards stay empty [S-4874]");
	}
	if (m_host == Host::Skirmish)
	{
		m_tab = 1; // the Skirmish page has no tabs: the player's own statistics
	}
	registerAll();
	fillCards();
}

AptStats::~AptStats()
{
	for (const std::string &n : m_commands)
	{
		m_windows.unregisterCommand(n);
	}
	for (const std::string &n : m_providers)
	{
		m_windows.unregisterProvider(n);
	}
	for (const std::string &n : m_screenRefs)
	{
		m_windows.closeAptScreen(n); // BFME2 0x5DD06D: _bfme_closeAptScreen("AptStats::InitGadgets")
	}
	showFilters(false);
}

std::u16string AptStats::text(const std::string &label) const
{
	return fetchOrMissing(m_env.gameText, label);
}

std::u16string AptStats::factionName(const std::string &templateName) const
{
	if (m_env.skirmish)
	{
		for (const SkirmishFaction &f : m_env.skirmish->factions())
		{
			if (f.templateName == templateName)
			{
				return text(f.displayLabel);
			}
		}
	}
	return u16(templateName); // a faction this installation does not know (another mod's): its template name
}

void AptStats::registerAll()
{
	// RW 0x9F021E (BFME2 0x5DD22B)
	if (m_windows.registerScreenRef("AptStats::InitGadgets", [this](const std::string &name, GameWindow *w) {
			if (name != "AptStats::StatsList" || !w)
			{
				return;
			}
			m_list = w;
			const int widths[3] = { 50, 25, 25 }; // BFME2 0x5DD14D
			GadgetListBoxSetColumnWidths(w, 3, widths);
			fillList();
		}))
	{
		m_screenRefs.push_back("AptStats::InitGadgets");
	}
	if (m_windows.registerCommand("Stats::OnSelectFaction", [this](const std::string &argument) { selectFaction(std::atoi(argument.c_str())); }))
	{
		m_commands.push_back("Stats::OnSelectFaction");
	}
	// RW 0x9F02C6: FactionColor<i> / FactionLevel<i> for the eight sides
	for (int i = 0; i < kSides; ++i)
	{
		const std::string color = "FactionColor" + std::to_string(i);
		const std::string level = "FactionLevel" + std::to_string(i);
		if (m_windows.registerProvider(color, [i](const std::string &, std::string &value, bool setting) {
				if (!setting)
				{
					// RW 0x9EFFB7
					static const char *const colors[7] = { "0x0E75D6", "0x12AC7F", "0xC0B60A", "0x646464", "0xD00303", "0xF5772A", "0x80B4FF" };
					value = i < 7 ? colors[i] : "0xFFFFFFFF";
				}
				return true;
			}))
		{
			m_providers.push_back(color);
		}
		if (m_windows.registerProvider(level, [this, i](const std::string &, std::string &value, bool setting) {
				if (setting)
				{
					return true;
				}
				// RW 0x9F001C: the percentage to the next rank. OpenBFME has no ranks: the win rate with the faction, the player's own on the page of
				// their games, everyone's on the community's [S-4874]
				std::int64_t games = 0, wins = 0;
				value = cardNumbers(i, games, wins) ? std::to_string(percent(wins, games)) : std::string("0");
				return true;
			}))
		{
			m_providers.push_back(level);
		}
	}
	if (m_host == Host::Online)
	{
		// BFME2 0x5B8F04
		if (m_windows.registerCommand("AptOnline::Stats::CurrentTab", [this](const std::string &tab) {
				const int t = tab == "Tournament" ? 0 : tab == "OpenPlay" ? 1 : tab == "Strategic" ? 2 : -1;
				if (t < 0)
				{
					m_windows.note("online-stats", "AptOnline::Stats::CurrentTab(" + tab + "): not a tab (ignored, as retail)");
					return;
				}
				m_tab = t;
				fillList();
			}))
		{
			m_commands.push_back("AptOnline::Stats::CurrentTab");
		}
	}
}

// the numbers a card shows: the player's own games with its faction (the page of their games, the Skirmish page), or everyone's picks and wins of it
// (the community page: the platform's list, for the filters shown). false: none yet (the community's answer has not come, or the service is down)
bool AptStats::cardNumbers(int card, std::int64_t &games, std::int64_t &wins) const
{
	games = wins = 0;
	if (card < 0 || card >= (int)m_cardTemplates.size() || card >= kCards)
	{
		return m_tab == 1 || m_host == Host::Skirmish; // a side with no card: nothing to count (the player's own: a known zero)
	}
	const std::string &faction = m_cardTemplates[(std::size_t)card];
	if (m_host == Host::Skirmish || m_tab == 1)
	{
		const std::vector<GameRecord> *records = m_env.onlineHub ? m_env.onlineHub->records : nullptr;
		for (const GameRecord &r : records ? *records : std::vector<GameRecord>())
		{
			const GameRecord::Player *me = r.me();
			if (me && me->faction == faction)
			{
				++games;
				wins += r.won() ? 1 : 0;
			}
		}
		return true;
	}
	if (m_tab != 0)
	{
		return false; // War of the Ring: nothing is kept
	}
	PlatformApiClient *p = m_env.onlineHub ? m_env.onlineHub->platform : nullptr;
	if (!p || !p->globalStats().known || p->globalStats().mode + "|" + p->globalStats().period != filterMode() + "|" + filterPeriod())
	{
		return false;
	}
	for (const PlatformApiClient::FactionStat &f : p->globalStats().humans.factions)
	{
		if (!f.overflow && f.faction == faction)
		{
			games = f.picks;
			wins = f.wins;
		}
	}
	return true;
}

void AptStats::fillCards()
{
	// RW 0x9F02C6: the names and pictures of the eight sides; BFME2 0x5DD48C: the rank lines (OpenBFME: games and win rate, the player's own or everyone's by
	// the page's tab [S-4874])
	for (int i = 0; i < kSides; ++i)
	{
		const std::string side = kSideTable[i];
		m_windows.setAptText("Stats:FactionName_" + std::to_string(i), OnlineLobbyClient::utf16ToUtf8(text("SIDE:" + side)));
		m_windows.setAptImage("Stats:FactionIcon_" + std::to_string(i), "AptIcon" + side);
		m_windows.setAptImage("Stats:FactionImage_" + std::to_string(i), "Apt" + side + "Image");
		std::int64_t games = 0, wins = 0;
		const bool known = cardNumbers(i, games, wins);
		const char *noun = m_tab == 0 && m_host == Host::Online ? (games == 1 ? " pick, " : " picks, ") : (games == 1 ? " game, " : " games, ");
		const std::string line = !known ? std::string() : !games ? std::string("No games") : std::to_string(games) + noun + std::to_string(percent(wins, games)) + "% won";
		m_windows.setAptText("Stats:LevelName_" + std::to_string(i), line);
		m_windows.setAptText("Stats:PointsNextLevel_" + std::to_string(i), known && games ? std::to_string(percent(wins, games)) + "% won" : std::string());
	}
	m_cardLines.clear();
	for (int i = 0; i < kCards; ++i)
	{
		m_cardLines.push_back(*m_windows.aptText("Stats:LevelName_" + std::to_string(i)));
		m_cardLines.push_back(*m_windows.aptText("Stats:PointsNextLevel_" + std::to_string(i)));
	}
	writeCardFields();
}

void AptStats::writeCardFields()
{
	// Stats.swf's card script assigns its fields through a variable named like the field (LevelName = "$Stats:LevelName_" + _name; Points = ...)
	// and the field is bound to that variable; this Apt player resolves the name to the field instance itself and draws its path (an Apt player
	// gap, S-4874): the lines are written into the cards' own text fields (card clips named "0" .. "6", fields LevelName and Points; RotWK 2.01's
	// cards show LevelName only)
	if (m_level < 0 || m_cardLines.size() != (std::size_t)kCards * 2)
	{
		return;
	}
	std::vector<AptCharacterInst *> stack{ m_windows.apt().level(m_level) };
	while (!stack.empty())
	{
		AptCharacterInst *inst = stack.back();
		stack.pop_back();
		if (!inst)
		{
			continue;
		}
		if (inst->type() == AptCharacterInst::Type::EditText && (inst->instName() == "LevelName" || inst->instName() == "Points") && inst->parent())
		{
			const std::string &card = inst->parent()->instName();
			if (card.size() == 1 && card[0] >= '0' && card[0] < '0' + kCards)
			{
				AptTextInst *t = static_cast<AptTextInst *>(inst);
				const std::string &want = m_cardLines[(std::size_t)(card[0] - '0') * 2 + (inst->instName() == "Points" ? 1 : 0)];
				// the field is bound to the variable "LevelName" of its card, which this player resolves to the field itself (its path is drawn):
				// unbound, the field draws its own text
				t->variable.clear();
				if (t->text != want)
				{
					t->text = want;
					t->textAssigned = true;
				}
			}
		}
		if (AptSpriteInst *sp = inst->asSprite())
		{
			for (AptCharacterInst *k : sp->children())
			{
				stack.push_back(k);
			}
		}
	}
}

void AptStats::selectFaction(int focus)
{
	// BFME2 0x5DD1D0 -> 0x5DD0EB: the focus, the list filled again
	m_focus = focus;
	fillList();
}

void AptStats::refresh()
{
	fillList();
}

void AptStats::row(const std::u16string &name, const std::u16string &faction, const std::u16string &overall)
{
	m_rows.push_back({ name, faction, overall });
}

std::vector<std::string> AptStats::rowLines() const
{
	std::vector<std::string> out;
	for (const auto &r : m_rows)
	{
		out.push_back(OnlineLobbyClient::utf16ToUtf8(r[0]) + "|" + OnlineLobbyClient::utf16ToUtf8(r[1]) + "|" + OnlineLobbyClient::utf16ToUtf8(r[2]));
	}
	return out;
}

void AptStats::fillList()
{
	m_rows.clear();
	if (m_tab == 1)
	{
		yourRows();
	}
	else if (m_tab == 0)
	{
		communityRows();
	}
	else
	{
		row(u"War of the Ring statistics are not kept: War of the Ring online is not available yet.", u"", u"");
	}
	showFilters(m_tab == 0);
	fillCards(); // the cards follow the tab and the filters (the community's numbers are everyone's, never the player's own)
	if (!m_list)
	{
		return;
	}
	// AptTimeLineStats' refill (RW's 0x5DE433 shape): the top row kept
	const int top = GadgetListBoxGetTopVisibleEntry(m_list);
	GadgetListBoxReset(m_list);
	for (std::size_t i = 0; i < m_rows.size(); ++i)
	{
		for (int c = 0; c < 3; ++c)
		{
			GadgetListBoxAddEntryText(m_list, m_rows[i][(std::size_t)c], 0xFFFFFFFFu, (int)i, c);
		}
	}
	GadgetListBoxSetTopVisibleEntry(m_list, top);
}

void AptStats::yourRows()
{
	const OnlineHubContext *hub = m_env.onlineHub;
	if (!hub || !hub->records)
	{
		row(u"No games are recorded yet.", u"", u"");
		return;
	}
	const std::string focused = m_focus >= 0 && m_focus < (int)m_cardTemplates.size() && m_focus < kCards ? m_cardTemplates[(std::size_t)m_focus] : "";
	std::vector<GameRecord> mine;
	for (const GameRecord &r : *hub->records)
	{
		if (!focused.empty() && r.me() && r.me()->faction == focused)
		{
			mine.push_back(r);
		}
	}
	const PlayerStats all = GameRecords::summarise(*hub->records);
	const PlayerStats f = GameRecords::summarise(mine);
	const bool hasFaction = !focused.empty();
	auto fv = [&](const std::u16string &v) { return hasFaction ? v : std::u16string(); };
	row(u"YOUR GAMES", u"", u""); // player sync merges the games of every device (game_records.jsonl)
	row(u"Games", fv(num(f.games)), num(all.games));
	row(u"Wins", fv(num(f.wins)), num(all.wins));
	row(u"Losses", fv(num(f.losses)), num(all.losses));
	row(u"Win rate", fv(pct(f.wins, f.games)), pct(all.wins, all.games));
	row(u"Longest win streak", fv(num(f.longestWinStreak)), num(all.longestWinStreak));
	const std::u16string streak = all.currentStreak > 0 ? num(all.currentStreak) + (all.currentStreak == 1 ? u" win" : u" wins")
		: all.currentStreak < 0 ? num(-all.currentStreak) + (all.currentStreak == -1 ? u" loss" : u" losses") : std::u16string(u"-");
	row(u"Current streak", u"", streak);
	row(u"Favourite army", u"", all.favouriteFaction.empty() ? u"-" : factionName(all.favouriteFaction));
	auto mapName = [&](const std::string &key) -> std::u16string {
		if (key.empty())
		{
			return u"-";
		}
		return m_mapNames ? m_mapNames(key) : u16(mapLeaf(key));
	};
	row(u"Favourite map", fv(f.favouriteMap.empty() ? u"-" : mapName(f.favouriteMap)), mapName(all.favouriteMap));
	if (!hub->recordsProblem.empty())
	{
		row(u"Some recorded games could not be read: " + u16(hub->recordsProblem), u"", u"");
	}
	if (!all.last.empty())
	{
		row(u"LAST GAMES", u"", u"");
		for (const PlayerStats::Recent &g : all.last)
		{
			row(mapName(g.map), factionName(g.faction), g.result > 0 ? u"Won" : g.result == 0 ? u"Lost" : u"No winner");
		}
	}
}

std::string AptStats::filterMode() const
{
	return kModeValues[m_modeIndex];
}

std::string AptStats::filterPeriod() const
{
	return kPeriodValues[m_periodIndex];
}

void AptStats::setFilter(int mode, int period)
{
	if (mode < 0 || mode > 2 || period < 0 || period > 2)
	{
		return;
	}
	m_modeIndex = mode;
	m_periodIndex = period;
	if (m_modeBox)
	{
		GadgetComboBoxSetSelectedPos(m_modeBox, mode, false);
	}
	if (m_periodBox)
	{
		GadgetComboBoxSetSelectedPos(m_periodBox, period, false);
	}
	m_communityAsked = false;
	fillList();
}

void AptStats::requestCommunity()
{
	const std::string want = filterMode() + "|" + filterPeriod();
	PlatformApiClient *p = m_env.onlineHub ? m_env.onlineHub->platform : nullptr;
	if (!p || m_communityAsked)
	{
		return;
	}
	std::string err;
	if (p->fetchGlobalStats(filterMode(), filterPeriod(), &err))
	{
		m_communityAsked = true;
		m_communityError.clear();
		m_windows.note("online-stats", "community statistics asked for " + want);
	}
	else
	{
		m_communityError = err;
	}
}

void AptStats::communityRows()
{
	PlatformApiClient *p = m_env.onlineHub ? m_env.onlineHub->platform : nullptr;
	const char16_t *modeText = kModeItems[m_modeIndex];
	const char16_t *periodText = kPeriodItems[m_periodIndex];
	row(u"ALL PLAYERS: " + std::u16string(modeText) + u", " + std::u16string(periodText), u"", u"");
	if (!p)
	{
		row(kUnavailable, u"", u"");
		return;
	}
	if (!m_communityError.empty())
	{
		row(std::u16string(kUnavailable) + u": " + u16(m_communityError), u"", u"");
		return;
	}
	requestCommunity();
	const PlatformApiClient::GlobalStats &g = p->globalStats();
	const std::string want = filterMode() + "|" + filterPeriod();
	if (!g.known || g.mode + "|" + g.period != want)
	{
		row(u"Asking the online services ...", u"", u"");
		return;
	}
	const PlatformApiClient::StatsBucket &b = g.humans;
	const std::string focused = m_focus >= 0 && m_focus < (int)m_cardTemplates.size() && m_focus < kCards ? m_cardTemplates[(std::size_t)m_focus] : "";
	const PlatformApiClient::FactionStat *fs = nullptr;
	for (const auto &s : b.factions)
	{
		if (!s.overflow && s.faction == focused)
		{
			fs = &s;
		}
	}
	row(u"Games counted", u"", num(b.games));
	if (!focused.empty())
	{
		row(u"Picked", fs ? num(fs->picks) + u" (" + rate(fs->pickRate) + u")" : u"0", u"");
		row(u"Wins", fs ? num(fs->wins) + u" (" + rate(fs->winRate) + u")" : u"0", u"");
	}
	const PlatformApiClient::FactionStat *most = nullptr, *best = nullptr;
	for (const auto &s : b.factions)
	{
		if (s.overflow)
		{
			continue; // the service's "other" row is everything past its named armies, never an army
		}
		if (!most || s.picks > most->picks)
		{
			most = &s;
		}
		if (s.picks > 0 && (!best || s.winRate > best->winRate))
		{
			best = &s;
		}
	}
	row(u"Most picked army", u"", most ? factionName(most->faction) + u" (" + rate(most->pickRate) + u")" : u"-");
	row(u"Highest win rate", u"", best ? factionName(best->faction) + u" (" + rate(best->winRate) + u")" : u"-");
	row(u"Average game length", u"", b.games ? duration(b.avgDurationS) : u"-");
	if (!focused.empty())
	{
		for (const auto &m : b.matchups)
		{
			if (!m.overflow && (m.a == focused || m.b == focused))
			{
				const bool isA = m.a == focused;
				const std::int64_t wins = isA ? m.aWins : m.games - m.aWins;
				row(u"Against " + factionName(isA ? m.b : m.a), num(wins) + u" of " + num(m.games) + u" won (" + pct(wins, m.games) + u")", u"");
			}
		}
	}
	if (!b.maps.empty())
	{
		row(u"POPULAR MAPS", u"", u"");
		std::size_t shown = 0;
		std::int64_t others = 0;
		for (const auto &m : b.maps)
		{
			if (m.overflow || shown >= 5)
			{
				others += m.games;
				continue;
			}
			++shown;
			const std::u16string name = m_mapNames ? m_mapNames(m.map) : u16(mapLeaf(m.map));
			row(name, u"", num(m.games) + (m.games == 1 ? u" game" : u" games"));
		}
		if (others)
		{
			row(u"Other maps", u"", num(others) + (others == 1 ? u" game" : u" games"));
		}
	}
	row(u"Games against the computer", u"", num(g.vsAi.games));
}

void AptStats::showFilters(bool show)
{
	AptGadgetLayer *layer = m_windows.gadgetLayer();
	if (!show || !layer || !m_owner)
	{
		if (layer)
		{
			if (m_modeBox)
			{
				layer->destroyEngineGadget(m_modeBox);
			}
			if (m_periodBox)
			{
				layer->destroyEngineGadget(m_periodBox);
			}
		}
		m_modeBox = m_periodBox = nullptr;
		return;
	}
	if (m_modeBox && m_periodBox)
	{
		return;
	}
	// the community tab's filters: two combo boxes of the game's own skin (window\apt\combobox.wnd), as the OpenBFME options page makes them (OPTS-3)
	std::string err;
	m_modeBox = layer->createEngineGadget(kComboSkin, kModeLeft, kFilterTop, kModeWidth, kFilterHeight, m_owner, &err);
	m_periodBox = m_modeBox ? layer->createEngineGadget(kComboSkin, kPeriodLeft, kFilterTop, kPeriodWidth, kFilterHeight, m_owner, &err) : nullptr;
	if (!m_modeBox || !m_periodBox)
	{
		m_windows.note("online-stats", "the community filters could not be made: " + err);
		if (m_modeBox)
		{
			layer->destroyEngineGadget(m_modeBox);
		}
		m_modeBox = m_periodBox = nullptr;
		return;
	}
	for (int i = 0; i < 3; ++i)
	{
		GadgetComboBoxAddEntry(m_modeBox, kModeItems[i], 0xFFFFFFFFu);
		GadgetComboBoxAddEntry(m_periodBox, kPeriodItems[i], 0xFFFFFFFFu);
	}
	GadgetComboBoxSetSelectedPos(m_modeBox, m_modeIndex, false);
	GadgetComboBoxSetSelectedPos(m_periodBox, m_periodIndex, false);
}

bool AptStats::gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData, WindowMsgData)
{
	if (!from || (from != m_modeBox && from != m_periodBox))
	{
		return false;
	}
	if (msg == GCM_SELECTED)
	{
		int index = -1;
		GadgetComboBoxGetSelectedPos(from, &index);
		if (index >= 0 && index < 3)
		{
			const int mode = from == m_modeBox ? index : m_modeIndex;
			const int period = from == m_periodBox ? index : m_periodIndex;
			if (mode != m_modeIndex || period != m_periodIndex)
			{
				setFilter(mode, period);
			}
		}
	}
	return true;
}

void AptStats::communityFailed(const std::string &why)
{
	m_communityAsked = false;
	m_communityError = why;
	if (m_tab == 0)
	{
		fillList();
		m_communityError.clear(); // shown once; the next refill asks again
	}
}

void AptStats::update()
{
	writeCardFields();
	PlatformApiClient *p = m_env.onlineHub ? m_env.onlineHub->platform : nullptr;
	if (m_tab != 0 || !p)
	{
		return;
	}
	// the community answer for the filters shown: the list follows it once
	const PlatformApiClient::GlobalStats &g = p->globalStats();
	const std::string have = g.known ? g.mode + "|" + g.period : std::string();
	if (have != m_communityFor && have == filterMode() + "|" + filterPeriod())
	{
		m_communityFor = have;
		fillList();
	}
}
