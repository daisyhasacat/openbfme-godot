// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// AptStats (lane ONLINEHUB-1): the engine side of RotWK's statistics page Stats.swf, opened by the online shell's STATS button (OnlineShell.apt nav
// "Profile": targetScreen "Stats", StatsMode "Online") and by the Skirmish profile page (Skirmish.apt's _open_profile into SkirmishStats,
// AptSkirmish::OnStatsMenu). The page shows seven faction cards (name, icon, picture, a level bar and two lines of text) above a list of three
// columns: a row's name, the chosen faction's value and the overall value.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; BFME2 decomp AptStats.cpp, tier A):
//   * the registration RW 0x9F021E (BFME2 0x5DD22B): the screen reference AptStats::InitGadgets (RW 0x9F0060's partner, BFME2 0x5DD14D: the gadget
//     AptStats::StatsList gets three columns of 50 / 25 / 25 % and is filled for the focus) and the command Stats::OnSelectFaction (BFME2 0x5DD1D0:
//     atoi of its argument is the new focus, the list is filled again);
//   * the faction variables RW 0x9F02C6 (BFME2 0x5DD2D3): for i = 0 .. 7 the externs FactionColor<i> (RW 0x9EFFB7: "0x0E75D6", "0x12AC7F", "0xC0B60A",
//     "0x646464", "0xD00303", "0xF5772A", "0x80B4FF" for 0 .. 6, else "0xFFFFFFFF") and FactionLevel<i> (RW 0x9F001C: the percentage done to the next
//     rank, "%d"), the text Stats:FactionName_<i> = SIDE:<side>, the images Stats:FactionIcon_<i> = AptIcon<side> and Stats:FactionImage_<i> =
//     Apt<side>Image, with the sides of the table RW 0xDA3AC0: Men, Elves, Dwarves, Isengard, Mordor, Wild, Angmar, Arnor;
//   * the rank texts (BFME2 0x5DD48C): Stats:LevelName_<i> the rank's name, Stats:RankImage_<i> its picture, Stats:PointsNextLevel_<i> the points
//     still needed; the faction index i is the i-th playable, non-observer PlayerTemplate (the constructor's m_factionToTemplate, BFME2 0x5DD609);
//   * the online page's AptOnline::Stats::CurrentTab (BFME2 0x5B8F04): "Tournament" 0, "OpenPlay" 1, "Strategic" 2, then the page is refilled.
// OpenBFME DIFFERENCES (stops S-4874 .. S-4876): RotWK's numbers were GameSpy's ranks and points (online) and the skirmish profile's records; neither
// exists any more. The page shows OpenBFME's own (GameClient/GameRecords.h, the games this installation played, and the platform's community
// statistics, GameNetwork/PlatformApiClient.h):
//   * the cards: LevelName = "<n> games, <w>% won" ("No games"), PointsNextLevel = "<w>% won", FactionLevel = the win rate (the bar), no rank
//     picture; on the community tab they are everyone's picks and wins of the faction for the filters shown (empty until the answer is in) [S-4874];
//   * the tabs (online): OPEN PLAY = "Your stats" (every game this installation played), TOURNAMENT = the community's statistics (all players' agreeing
//     online and LAN games, platform GET /v1/stats/global) with two filters drawn from the game's own combo box skin (mode, period) in the list's empty
//     header cell, WAR OF THE RING = a line saying War of the Ring statistics are not kept [S-4875]; the Skirmish page has no tabs: Your stats;
//   * the focus beyond the cards (the list's "Overall" column, BFME2's focus value 6) is the overall column; a faction that is not one of the cards
//     counts in the overall column only [S-4876].

#pragma once

#include "GameClient/GameRecords.h"
#include "GameClient/GUI/GameWindow.h"

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class PlatformApiClient;
class WindowManager;
struct ShellEnvironment;

// What the online hub's pages read (ShellEnvironment::onlineHub): the host program fills it (GodotDevice: AptMenuPlayer)
struct OnlineHubContext
{
	const std::vector<GameRecord> *records = nullptr; ///< the games this installation played (null: none kept; the page says so)
	std::string recordsProblem;                       ///< why the record file could not be read in full ("" when it could)
	PlatformApiClient *platform = nullptr;            ///< the platform API (null: "Online services unavailable")
	bool shareResults = true;                         ///< the Options row "Share my game results for statistics"
};

class AptStats
{
public:
	enum class Host
	{
		Skirmish,
		Online
	};
	static constexpr int kCards = 7;   ///< the movie's cards (Stats:FactionName_0 .. 6)
	static constexpr int kSides = 8;   ///< RW 0xDA3AC0's sides (the loop RW 0x9F02C6 runs 8 times)
	static const char *const kSideTable[kSides];

	AptStats(WindowManager &windows, ShellEnvironment &environment, Host host, GameWindow *owner);
	~AptStats();
	AptStats(const AptStats &) = delete;
	AptStats &operator=(const AptStats &) = delete;

	// the data changed (a game was recorded, the community answer came): the cards and the list are filled again
	void refresh();
	// the Apt level the page's movie plays in (the host screen's): the cards' two text lines are written into their fields there (see fillCards)
	void setLevel(int level) { m_level = level; }
	// the map's display name as the host screen shows it (the skirmish list's / the server browser's: MapMetaData's base display name); unset: the
	// map's file name
	void setMapNames(std::function<std::u16string(const std::string &)> fn) { m_mapNames = std::move(fn); }
	// per frame: the community statistics' answer
	void update();
	// the platform refused or could not answer the community statistics (the hub's events): the list says why; a new choice of filter asks again
	void communityFailed(const std::string &why);
	// a gadget message to the owner window: true when it was one of the page's filter combos
	bool gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2);

	// ---- state (tests, the host) ----
	int focus() const { return m_focus; }
	int tab() const { return m_tab; } ///< 0 Tournament (community), 1 Open Play (yours), 2 Strategic
	const std::vector<std::array<std::u16string, 3>> &rows() const { return m_rows; }
	const std::vector<std::string> &cardTemplates() const { return m_cardTemplates; }
	GameWindow *statsList() const { return m_list; }
	GameWindow *modeFilter() const { return m_modeBox; }
	GameWindow *periodFilter() const { return m_periodBox; }
	std::string filterMode() const;   ///< "" every mode, "faithful", "uber"
	std::string filterPeriod() const; ///< "7d" / "30d" / "all"
	void setFilter(int mode, int period); ///< as the player's choice in the two combos (tests)
	// the rows, one line each "name|faction|overall" (tests, reports)
	std::vector<std::string> rowLines() const;

private:
	void registerAll();
	void selectFaction(int focus);
	void fillCards();
	bool cardNumbers(int card, std::int64_t &games, std::int64_t &wins) const;
	void writeCardFields();
	void fillList();
	void yourRows();
	void communityRows();
	void showFilters(bool show);
	void requestCommunity();
	std::u16string text(const std::string &label) const;
	std::u16string factionName(const std::string &templateName) const;
	void row(const std::u16string &name, const std::u16string &faction, const std::u16string &overall);

	WindowManager &m_windows;
	ShellEnvironment &m_env;
	Host m_host;
	GameWindow *m_owner;
	std::vector<std::string> m_commands, m_providers, m_screenRefs, m_texts;
	GameWindow *m_list = nullptr;
	GameWindow *m_modeBox = nullptr, *m_periodBox = nullptr;
	int m_modeIndex = 0, m_periodIndex = 1; ///< every mode, the last 30 days
	int m_focus = 0;
	int m_tab = 0; ///< BFME2 0x5B8F69: 0 (Tournament), as the movie's first tab
	int m_level = -1;
	std::function<std::u16string(const std::string &)> m_mapNames;
	std::vector<std::string> m_cardLines; ///< per card: LevelName, Points (written into the fields every update)
	bool m_communityAsked = false;
	std::string m_communityFor; ///< the filters of the answer shown ("mode|period")
	std::string m_communityError;
	std::vector<std::string> m_cardTemplates; ///< card i -> the PlayerTemplate (the i-th playable one), "" none
	std::vector<std::array<std::u16string, 3>> m_rows;
};
