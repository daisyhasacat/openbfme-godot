// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameClient/GUI/AptScreens/AptOnlineShell.h (lane ONLINE-4).

#include "GameClient/GUI/AptScreens/AptOnlineShell.h"

#include "GameClient/GUI/AptScreens/AptOnlineHub.h"
#include "GameClient/GUI/PlatformIcons.h"
#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/Gadget.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/GameWindow.h"
#include "GameClient/GUI/ProfileSwitch.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GUI/Skirmish/SkirmishGameSetup.h"
#include "GameNetwork/OnlineLobbyClient.h"
#include "GameNetwork/OnlineStart.h"
#include "GameNetwork/Transport.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <iterator>
#include <map>
#include <set>

namespace
{
std::string narrow(const std::u16string &s)
{
	return OnlineLobbyClient::utf16ToUtf8(s);
}
UnicodeString wide(const std::string &s)
{
	return UnicodeString(s.begin(), s.end());
}
std::u16string trim(std::u16string s)
{
	while (!s.empty() && (s.back() == u' ' || s.back() == u'\t'))
	{
		s.pop_back();
	}
	size_t i = 0;
	while (i < s.size() && (s[i] == u' ' || s[i] == u'\t'))
	{
		++i;
	}
	return s.substr(i);
}
// the map cache key's display form (as AptLanLobby's list): the file name without its folder and ".map"
std::string mapLeaf(const std::string &key)
{
	std::string map = key;
	const size_t slash = map.find_last_of("/\\");
	map = slash == std::string::npos ? map : map.substr(slash + 1);
	if (map.size() > 4 && map.compare(map.size() - 4, 4, ".map") == 0)
	{
		map.resize(map.size() - 4);
	}
	return map;
}
// OpenBFME's own texts (no retail label exists for them; the online service is OpenBFME's)
const char16_t *const kNoMotd = u"The lobby has no message of the day.";
const char16_t *const kNoRelayTitle = u"Online";
const char16_t *const kNoRelay = u"Online games can't start yet: no relay server is available.";
const char16_t *const kRejoinTitle = u"Rejoin";
const char16_t *const kModeTitle = u"Performance mode"; // lane UBERJOIN-1
const char16_t *const kOnlineTitle = u"Online";          // lane ONLINEUX-1: the title of the screens' own messages (GUI:GSErrorTitle is empty in RotWK)
constexpr size_t kWrap = 58;                              // lane ONLINEUX-1: the characters of a box line (the box's text field squeezes one long line)
} // namespace

const std::vector<std::string> &AptOnlineShell::stopLines()
{
	static const std::vector<std::string> lines = {
		"[S-4070] online shell: the opener's RW 0x6230B6(1) is taken as the front-end background; GameSpy's setup (RW 0x9CCC06) and its misc preference "
		"InAdvMode (OnlineAdvMode) are not ported (the mode is kept for the session)",
		"[S-4071] online sign-in: the lobby's guest sign-in only: the nickname is the guest name; e-mail, password, remember-me, register, delete nickname, "
		"terms of service, the locale pop-up and the official site button are disabled or report not available; the former GameSpy button opens OpenBFME",
		"[S-4072] online home: the message of the day is the lobby's /v1/motd (none: said plainly); the ticker and the ladder site are not ported",
		"[S-4073] online game list: the lobby's games (name, the map's display name (a map not in the cache: its file name), players, lock); no ping, "
		"settings or user-map icons, no lobby rooms; the sort order "
		"(a second press reverses) is OpenBFME's (RotWK's sorter RW 0x988BB5 not read)",
		"[S-4074] online staging room: the states between the retail facts (the update's moves, the GameSpy NAT negotiation, the countdown) are OpenBFME's: "
		"the host's Play Game asks the lobby for the START (no relay: refused, told to the player); loading a saved online game is not available",
		"[S-4075] online pages not ported: Strategic (War of the Ring online), the Messenger",
		"[S-4870] Quick Match (the TOURNAMENT page): the platform's matchmaker (same mode and size, oldest first): Ladder = the size, Army, Color; "
		"ConnectionSpeed disabled, Widen Search says the search is as wide as it gets",
		"[S-4871] Quick Match room: the match's host makes a private lobby room by the match's room code on an official map for that many players "
		"picked by the match id; the others join by the code, take the match's team, say ready; the host starts",
		"[S-4873] tournaments: no RotWK screen (retail's Tournament was the ranked automatch): a panel of the game's list box skin on the TOURNAMENT "
		"page; register / withdraw / join through GuiFX's Yes / No box; the first player of a match hosts its room",
		"[S-4874] STATS page cards: the player's games and win rate with each faction (RotWK: GameSpy ranks and points)",
		"[S-4875] STATS page tabs: OPEN PLAY = the player's own statistics (this installation's games), TOURNAMENT = all players' (the platform), "
		"WAR OF THE RING = not kept",
		"[S-4877] Quick Match failures: the platform unreachable, a search timed out or cancelled, a room that does not start in 90 seconds: told in a "
		"box and the page's status line, the page comes back",
		"[S-4076] online Create-a-Hero: the lobby service does not carry a slot's Create-a-Hero record: online games are played without them",
		"[S-4550] online Connections screen: not shown (retail's lists the GameSpy connection between every two players; OpenBFME's players reach each "
		"other only through the relay): the button says so and the staging room comes back",
		"[S-4551] online staging room chat: the Players list names the room's players (retail's fill, the chat's recipients, was not read) and every "
		"line goes to everyone (no private lines)",
		"[S-4552] online staging room rows: Hero (no Create-a-Hero through the lobby, S-4076) and Handicap (the online start carries no handicap: OWNERFIX-2 locks its combo) are shown "
		"locked; the lobby carries no handicap",
		"[S-4553] online ready rules: the lobby unreadies every player on any change of the room (a join, a leave, a kick, a slot or setup change); "
		"RotWK's own reset (the game's vslot 0x38 after a rule change, RW 0x8421E0) was read only for the host's rule changes; the clan game's checks "
		"before a ready (CLAN:ErrorMissingClanAffiliation / CLAN:ErrorOnlyTwoClans) are not ported",
		"[S-4860] online / LAN performance mode: the list tags an Uber game, Join on a game of the other mode asks in GuiFX's Yes / No box and Yes "
		"switches the profile for that game (restored when the room is left or the game ended; the Options.ini setting is never written); OpenBFME's "
		"own (retail has one simulation and no such box)",
		"[S-4554] online staging room transport: polls held by the lobby (long polling) and the sender's own chat line shown at its sending are "
		"OpenBFME's (retail: GameSpy's peer chat)",
		"[S-4077] online rejoin / watch: RotWK's movies have no Rejoin or Watch button: the player's running game is offered in GuiFX's Yes / No box at "
		"sign-in and is a REJOIN row of the Open Play list, other running games are WATCH rows; Join Game on such a row asks the lobby for the rejoin "
		"or spectator START (state 13, OpenBFME's)",
		// lane ONLINEUX-1
		"[S-5140] online shell back over a covering screen (Options): the movie's own ShowButtonBar brings the nav, LOG OUT and OPTIONS back "
		"(INFERENCE: retail reloads the covered shell's movie, whose first frame does it; this port hides and shows it, S-171)",
		"[S-5141] online server browser: the MAP panel shows the selected game's map (retail's GameInfo fill, RW 0x9AA4C3, not read); the RULES tab "
		"keeps the player's own (the lobby's list carries no rules)",
		"[S-5142] online home ticker: the Date, Time, Players Online and Games In Progress rows of retail's thirteen (RW 0x9C8694, labels RW 0xDB9290); "
		"no ladder ranks, no faction win rates; NumTickerFields 4; the rows' texts are given as the movie's TickerInfo class would (registerClass "
		"unported, S-104)",
		"[S-5143] online screens' own texts: the TOURNAMENT page's and the home page's lines name Quick Match and OpenBFME's tournaments (no ranked "
		"ladder); the lobby's refusals are said as what happened and what to do (RotWK's own texts where it has them)",
	};
	return lines;
}

AptOnlineShell::~AptOnlineShell()
{
	for (auto &p : m_pages)
	{
		for (const std::string &n : p.second.commands)
		{
			windows().unregisterCommand(n);
		}
		for (const std::string &n : p.second.providers)
		{
			windows().unregisterProvider(n);
		}
		for (const std::string &n : p.second.screenRefs)
		{
			windows().closeAptScreen(n); // _bfme_closeAptScreen
		}
	}
}

AptOnlineShell::AptOnlineShell(WindowManager &windows, Shell &shell, ShellEnvironment &environment)
	: AptSkirmish(windows, shell, environment, "OnlineShell.apt", "AptOnline", true), m_ctx(environment.online),
	  m_client(environment.online ? environment.online->client : nullptr)
{
	// RW 0x91D7A3: RW 0x6230B6(1) after the push. INFERENCE (S-4070): the front-end background as the LAN opener's (RW 0x847180)
	windows.setBackground(1);
	if (!m_client)
	{
		windows.note("provider-unwired", "OnlineShell.apt: the host gave no online lobby client [S-4070]");
	}
	for (const std::string &line : stopLines())
	{
		windows.note("online-stops", line);
	}
	// RW 0x91E133: the page title text APT:OnlineOrNetwork = APT:Online
	windows.setAptText("APT:OnlineOrNetwork", narrow(text("APT:Online")));
	registerCommand("AptOnline::OnInitialized", [](const std::string &) {}); // RW 0x9F3A3C (an empty callback)
	registerCommand("AptOnline::ShellLoadScreen", [this](const std::string &name) { loadPage(name); });
	registerCommand("AptOnline::ShellUnloadScreen", [this](const std::string &name) { unloadPage(name); });
	registerCommand("AptOnline::Options", [this](const std::string &) {
		AptOptionsScreen::open(this->shell(), true, false, true, false); // RW 0x91D6BA (BFME2 0x516EC0: OpenScreen(1, 0, 1, 0))
	});
	registerCommand("AptOnline::ShellExit", [this](const std::string &) {
		// RW 0x91D6CD (BFME2 0x516ED3): the online shell is left (BFME2 0x516E92). The lobby game, if any, is left first
		if (m_client && m_client->inGame())
		{
			m_client->leaveGame();
		}
		this->windows().requestShellPop();
	});
	// lane ONLINE-5: the MESSENGER button of the shell's frame (MpGameSetup.apt's MessengerButton: its release sends AptMessenger::OnMessengerBttn and
	// its look follows the extern AptMessenger::IsOpen). The Messenger (GameSpy's buddy chat, AptMessenger) is not ported (S-4075): the press used to
	// reach no handler at all; now the player is told, and the button shows the Messenger closed
	registerCommand("AptMessenger::OnMessengerBttn", [this](const std::string &) { notAvailable("the Messenger", "[S-4075]"); });
	registerProvider("AptMessenger::IsOpen", [](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = "0";
		}
		return true;
	});
	registerProvider("OnlineShellStartScreen", [this](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = m_currentName; // RW 0x91D9F9 query 0 (read only)
		}
		return true;
	});
	registerProvider("OnlineAdvMode", [this](const std::string &, std::string &value, bool setting) {
		// query 1: GameSpy's misc preference InAdvMode; kept for the session only (GameSpy's preference file is not written: S-4070)
		if (setting)
		{
			m_advMode = value == "1" || value == "t" || value == "true" ? "1" : "0";
		}
		else
		{
			value = m_advMode;
		}
		return true;
	});
	m_hub = std::make_unique<AptOnlineHub>(*this); // lane ONLINEHUB-1
	windows.addUpdateListener(this, [this]() { update(); });
	// lane ONLINEUX-1: the shell made again after an online game (the score screen's Continue pushes the shell's start screens: S-1771; retail kept this
	// shell under the game). The game the player played is over for them: the lobby is told (it stopped offering a finished or forfeited game for a
	// rejoin only when its player left it) and it is never offered again in this run. The screens resume on the page the player was on: the movie's
	// first frame loads extern OnlineShellStartScreen (RW 0x91D9F9 query 0, the shell object's current page; OnlineShell.apt frame 1:
	// LoadScreen(extern.OnlineShellStartScreen, true)), which retail's kept shell answers with that page, not the login
	if (m_ctx)
	{
		if (!m_ctx->leftGames)
		{
			m_ctx->leftGames = std::make_shared<std::set<std::string>>();
		}
		if (m_client)
		{
			const std::string played = m_client->finishGame();
			if (!played.empty())
			{
				m_ctx->leftGames->insert(played);
				windows.note("online-game-finished", "game " + played + " is over for this player: the lobby is told (leave), no rejoin is offered for it");
			}
			if (m_client->signedIn() && !m_ctx->lastPage.empty())
			{
				m_currentName = m_ctx->lastPage;
				windows.note("online-resume", "signed in already: the screens resume on " + m_currentName);
			}
		}
	}
}

std::vector<std::string> AptOnlineShell::loadedPages() const
{
	std::vector<std::string> out;
	for (const auto &p : m_pages)
	{
		out.push_back(p.first);
	}
	return out;
}

AptMessageBox &AptOnlineShell::box()
{
	if (!m_box)
	{
		m_box = std::make_unique<AptMessageBox>(windows(), shell());
	}
	return *m_box;
}

std::u16string AptOnlineShell::text(const std::string &label) const
{
	return fetchOrMissing(const_cast<AptOnlineShell *>(this)->environment().gameText, label);
}

void AptOnlineShell::showBox(const std::u16string &title, const std::u16string &body, const std::string &record)
{
	m_boxes.push_back(record);
	windows().note("online-box", record);
	box().show(AptMessageBox::TYPE_OK, title, body);
}

void AptOnlineShell::showError(const std::string &titleLabel, const std::string &textLabel)
{
	// GSMessageBoxOk(TheGameText->fetch(title), TheGameText->fetch(text)) (BFME2 0x5A57BA, 0x570D36 ...)
	showBox(text(titleLabel), text(textLabel), titleLabel + "|" + textLabel);
}

void AptOnlineShell::notAvailable(const std::string &what, const std::string &stop, const std::u16string &playerText)
{
	windows().note("unported", what + " is not available in OpenBFME's online play " + stop);
	// lane MENUWORK-1: a box titled as OpenBFME's (the game text GUI:GSErrorTitle is GameSpy's error title), in the player's words when given
	showBox(u"OpenBFME", playerText.empty() ? u"Not available in OpenBFME's online play yet: " + std::u16string(what.begin(), what.end()) + u"." : playerText,
		"GUI:GSErrorTitle|not available: " + what);
}

std::u16string AptOnlineShell::unavailablePageText(const std::string &page)
{
	// lane MENUWORK-1: the online home's pages OpenBFME does not have (S-4075), in the player's words
	if (page == "OnlineStrategic")
	{
		return u"War of the Ring online games are not available yet: its strategy map game is still being rebuilt.";
	}
	return std::u16string();
}

void AptOnlineShell::callChild(const std::string &function, const std::vector<std::string> &args)
{
	// the login page's calls (BFME2 0x572632: TheWindowManager's invoke of "CallChild" with the function's name on the shell's movie + 0x274): the
	// page's own entry point HandleGameCall(name) runs them (OnlineLogin.apt traces "HandleGameCall:<name>"). INFERENCE (S-4071): the shell's CallChild
	// forwards to it; here the page's HandleGameCall is called at its path
	std::vector<std::string> all{ function };
	all.insert(all.end(), args.begin(), args.end());
	std::string err;
	if (level() < 0 || !windows().invokeASAt(level(), m_currentName, "HandleGameCall", all, nullptr, &err))
	{
		windows().note("movie-function-missing", m_currentName + ".HandleGameCall(" + function + "): " + err);
	}
}

void AptOnlineShell::callPage(const std::string &function, const std::vector<std::string> &args)
{
	// the custom match screen calls its page's functions at its own Apt path (vslot 0x28 composes it: RW 0x9AFAA4 .. 0x9AFAB6 for OnStartLobby)
	std::string err;
	if (level() < 0 || !windows().invokeASAt(level(), "OnlineOpenPlay", function, args, nullptr, &err))
	{
		windows().note("movie-function-missing", "OnlineOpenPlay." + function + ": " + err);
	}
}

// ---- the pages ------------------------------------------------------------------------------------------------------------------------------------------

void AptOnlineShell::pageCommand(const std::string &page, const std::string &name, std::function<void(const std::string &)> fn)
{
	if (windows().registerCommand(name, std::move(fn)))
	{
		m_pages[page].commands.push_back(name);
	}
	else
	{
		windows().note("register-duplicate", name);
	}
}

void AptOnlineShell::pageProvider(const std::string &page, const std::string &name, WindowManager::ProviderFn fn)
{
	if (windows().registerProvider(name, std::move(fn)))
	{
		m_pages[page].providers.push_back(name);
	}
}

void AptOnlineShell::pageScreenRef(const std::string &page, const std::string &name, WindowManager::ScreenRefFn fn)
{
	if (windows().registerScreenRef(name, std::move(fn)))
	{
		m_pages[page].screenRefs.push_back(name);
	}
}

void AptOnlineShell::loadPage(const std::string &name)
{
	// BFME2 0x517724: the page table 0xDD1568 by name; a factory makes nothing while its page is up
	if (m_pages.count(name))
	{
		m_currentName = name;
		return;
	}
	if (name == "OnlineLogin")
	{
		m_pages[name];
		registerLogin();
	}
	else if (name == "OnlineHome")
	{
		m_pages[name];
		registerHome();
	}
	else if (name == "OnlineOpenPlay")
	{
		m_pages[name];
		registerOpenPlay();
	}
	else if (AptOnlineHub::isHubPage(name))
	{
		// lane ONLINEHUB-1: OnlineQuickMatch (the TOURNAMENT button) and Stats (STATS)
		m_pages[name];
		m_hub->loadPage(name);
	}
	else
	{
		// OnlineStrategic: not ported [S-4075]. Lane MENUWORK-1: the box says why in the player's words, and the page the
		// player came from is loaded again (the movie's LoadScreen) instead of leaving them on an empty page
		m_pages[name];
		notAvailable(name, "[S-4075]", unavailablePageText(name));
		if (!m_currentName.empty() && m_currentName != name)
		{
			loadChildScreen(m_currentName);
			navTo(m_currentName); // lane ONLINEUX-1: the nav's highlight goes back too (it stayed on the refused page's button)
			return;
		}
	}
	m_currentName = name;
	if (m_ctx && name != "OnlineLogin")
	{
		m_ctx->lastPage = name; // lane ONLINEUX-1: the page the screens resume on after a game
	}
}

void AptOnlineShell::navTo(const std::string &page)
{
	// lane ONLINEUX-1: OnlineShell.apt's ButtonNav clip (Toggles.ButtonNav) has one frame label per page, _Home, _QuickMatch, _OpenPlay, _Strategic and
	// _Profile (the STATS button), whose frame plays "_hit" on that page's Select* button and "_up" on the others, then stops and loads its targetScreen
	// when one is set (the movie's frame scripts, decoded from the player's OnlineShell.apt). A refused page (OnlineStrategic) left the refused button
	// highlighted over the page the player is back on; the clip goes to that page's label (no targetScreen is set: nothing is loaded by it)
	static const std::map<std::string, std::string> labels = { { "OnlineHome", "_Home" }, { "OnlineQuickMatch", "_QuickMatch" },
		{ "OnlineOpenPlay", "_OpenPlay" }, { "OnlineStrategic", "_Strategic" }, { "Stats", "_Profile" } };
	const auto it = labels.find(page);
	if (it == labels.end())
	{
		return;
	}
	std::string err;
	if (level() < 0 || !windows().invokeASAt(level(), "Toggles.ButtonNav", "gotoAndPlay", { it->second }, nullptr, &err))
	{
		windows().note("movie-function-missing", "Toggles.ButtonNav.gotoAndPlay(" + it->second + "): " + err);
	}
}

void AptOnlineShell::runInit()
{
	AptSkirmish::runInit();
	// lane ONLINEUX-1: the shell shown again after a screen covered it (Options: the shell's OPTIONS button runs the movie's HideButtonBar("Options"),
	// which hides the nav bar, the Options and the Exit button, ButtonBarOn false). OnlineShell.apt shows them again only in ShowButtonBar, which its
	// LoadScreen calls when a page is loaded for the first time, as the movie's first frame does (LoadScreen(extern.OnlineShellStartScreen, true)); the
	// retail binary names neither function (no ShowButtonBar / HideButtonBar string in game.dat): retail's movie runs its first frame again when the
	// shell comes back. INFERENCE (S-5140): retail reloads a covered screen's movie; this port only hides and shows it (S-171), so the bar stayed
	// hidden and the online screens had no way out but quitting the game (ONLINEUX-1 audit). The return calls the movie's own ShowButtonBar
	if (m_shownOnce)
	{
		m_barDue = true;
	}
}

void AptOnlineShell::unloadPage(const std::string &name)
{
	// BFME2 0x5171A3: the page of that name is deleted
	auto it = m_pages.find(name);
	if (it == m_pages.end())
	{
		return;
	}
	for (const std::string &n : it->second.commands)
	{
		windows().unregisterCommand(n);
	}
	for (const std::string &n : it->second.providers)
	{
		windows().unregisterProvider(n);
	}
	for (const std::string &n : it->second.screenRefs)
	{
		windows().closeAptScreen(n); // _bfme_closeAptScreen
	}
	m_pages.erase(it);
	if (AptOnlineHub::isHubPage(name))
	{
		m_hub->unloadPage(name); // lane ONLINEHUB-1
	}
	if (name == "OnlineLogin")
	{
		m_nickname = m_email = m_password = m_remember = nullptr;
		m_loginHideUpdates = 0;
		m_loginDisablesDue = 0;
	}
	else if (name == "OnlineHome")
	{
		m_motd = nullptr;
	}
	else if (name == "OnlineOpenPlay")
	{
		m_gameList = m_lobbies = m_gameInfo = m_gameName = m_gamePassword = nullptr;
		if (m_client && m_client->inGame())
		{
			m_client->leaveGame(); // the custom match screen's teardown leaves the staging room (BFME2 0x59FC45's path)
		}
		m_state = 0;
	}
}

// ---- OnlineLogin -----------------------------------------------------------------------------------------------------------------------------------------

void AptOnlineShell::registerLogin()
{
	const std::string page = "OnlineLogin";
	windows().note("online-login", "[S-4750] the login page shows the nickname only: the account rows and buttons are hidden (no accounts yet)");
	pageCommand(page, "AptOnline::Login::Login", [this](const std::string &) { login(); });
	pageCommand(page, "AptOnline::Login::DeleteNickname", [this](const std::string &) { notAvailable("Delete nickname", "[S-4071]"); });
	pageCommand(page, "AptOnline::Login::Register", [this](const std::string &) { notAvailable("Register", "[S-4071]"); });
	pageCommand(page, "AptOnline::Login::ServiceTerms", [this](const std::string &) { notAvailable("GameSpy's terms of service", "[S-4071]"); });
	pageCommand(page, "AptOnline::Login::AcceptLocale", [this](const std::string &) { notAvailable("The locale", "[S-4071]"); });
	pageCommand(page, "AptOnline::Login::OfficialSite", [this](const std::string &) { notAvailable("The official site (URL:LotrHome)", "[S-4071]"); });
	pageCommand(page, "AptOnline::Login::GameSpy", [this](const std::string &) { windows().services().request({ ShellAction::OpenProjectWebsite, "" }); });
	pageCommand(page, "AptOnline::Login::CancelLogin", [this](const std::string &) {
		// RW 0x977B0F: the online shell is left (as ShellExit)
		this->windows().requestShellPop();
	});
	pageScreenRef(page, "AptOnlineLogin::InitGadgets", [this](const std::string &name, GameWindow *w) {
		// RW 0x97A413
		if (!w)
		{
			return;
		}
		if (name == "OnlineLogin::Password")
		{
			// 16 characters; disabled and hidden: the guest sign-in has no password [S-4071] (lane MENUWORK-1: a field that does nothing is not shown)
			candidate(m_password, w);
			GadgetTextEntrySetText(w, UnicodeString());
			w->winEnable(false);
			w->winHide(true);
		}
		else if (name == "OnlineLogin::Nickname")
		{
			// a combo of the cached nicknames, 15 characters (RW 0x97A494): here the guest name the player used last
			candidate(m_nickname, w);
			GadgetComboBoxReset(w);
			GadgetComboBoxSetMaxChars(w, 15);
			GadgetComboBoxSetIsEditable(w, true);
			const std::u16string last = m_ctx ? m_ctx->rememberedName : std::u16string();
			if (!last.empty())
			{
				GadgetComboBoxAddEntry(w, last, 0xFFFFFFFFu);
				GadgetComboBoxSetSelectedPos(w, 0, false);
				GadgetComboBoxSetText(w, last);
			}
		}
		else if (name == "OnlineLogin::Email")
		{
			candidate(m_email, w);
			GadgetComboBoxReset(w);
			w->winEnable(false); // no e-mail sign-in [S-4071]
			w->winHide(true);
		}
		else if (name == "OnlineLogin::RememberInfo")
		{
			candidate(m_remember, w); // "remember me" kept the password; a guest has none: the name is always remembered [S-4071]
			w->winEnable(false);
			w->winHide(true);
		}
		else if (name == "OnlineLogin::CountryList")
		{
			// the locale pop-up's list (WOL:Locale01 .. 37); the pop-up is never opened [S-4071]
		}
		// the account features' buttons are disabled through the movie's own DisableButton* (BFME2 0x572632 calls them at the login) once the page
		// shows its fields (update): the shell's CallChild reaches the page only after it became the shell's current movie
		m_loginDisablesDue = 15;
	});
}

// lane MENUWORK-1 (S-4750): the owner wants no account features yet (2026-10-14): the login page shows the nickname, a line saying what it is for, Log In
// and Main Menu, alongside OpenBFME's website seal. The movie's account parts (the e-mail / password / remember-me rows, Register, Delete nickname,
// Service terms, the retail official site and the two tooltips of the hidden rows) are hidden every update while the page is up (the movie may place them again); the
// e-mail row's label shows the line. Not retail: retail's page is GameSpy's account sign-in (RW 0x97C819). A part the movie does not have is noted
// once ("online-login"), never skipped silently
const std::vector<std::string> &AptOnlineShell::loginAccountParts()
{
	static const std::vector<std::string> parts = {
		"OnlineLogin.Login.textAndBorders.InputLabels.Password",
		"OnlineLogin.Login.textAndBorders.InputFields#2",
		"OnlineLogin.Login.textAndBorders.innerBorder#3",
		"OnlineLogin.Login.textAndBorders.innerBorder#9",
		"OnlineLogin.Login.textAndBorders.innerBorder#11",
		"OnlineLogin.Login.textAndBorders.InputLabels.RememberMe",
		"OnlineLogin.Login.Register",
		"OnlineLogin.Login.DeleteNickname",
		"OnlineLogin.Login.ServiceTerms",
		"OnlineLogin.Login.OfficialSite",
		"OnlineLogin.tooltipOnlineEAAccountName",
		"OnlineLogin.tooltipRememberMe",
	};
	return parts;
}

// the password frame's place (155.7, 96.65) moved down by the nickname row's distance (the placeholders OnlineLogin::Password y 94.8 and ::Nickname
// y 173.3: 78.5) and widened from the password entry's 269 to the combo's 340 pixels (126.4 %); its content starts 145 units left of its origin, so
// the widening is made up by 145 * 0.264 = 38.4 to the right (the left ends of the rows line up again; measured on the 2560 x 1440 proof)
const float AptOnlineShell::kNicknameFrameX = 155.7f + 38.4f;
const float AptOnlineShell::kNicknameFrameY = 96.65f + 78.5f;
const float AptOnlineShell::kNicknameFrameXScale = 126.4f;
const char *const AptOnlineShell::kNicknameLine = "Online play needs no account: your nickname is the name other players see in the game list and in games.";

void AptOnlineShell::showNicknameOnly()
{
	if (level() < 0 || !windows().hasAptPath(level(), "OnlineLogin.Login.textAndBorders"))
	{
		return; // the page's clips are not placed yet
	}
	// the page's timeline places its buttons over its first frames: a part still missing after 60 updates is noted
	const bool settled = ++m_loginHideUpdates > 60;
	for (const std::string &part : loginAccountParts())
	{
		std::string err;
		if (!windows().setAptVisible(level(), part, false, &err) && settled && m_loginPartsMissing.insert(part).second)
		{
			windows().note("online-login", "the account part " + part + " was not hidden: " + err + " [S-4750]");
		}
	}
	// the nickname row's frame: the movie draws the e-mail and nickname frames as one shape (InputFields depth 2, hidden with the e-mail row); the
	// password row's frame (depth 3, a clip of its own) is moved to the nickname row and widened to the combo's width
	const std::string frame = "OnlineLogin.Login.textAndBorders.InputFields#3";
	std::string frameErr;
	if ((!windows().setAptMember(level(), frame, "_x", AptValue::number(kNicknameFrameX), &frameErr) ||
			!windows().setAptMember(level(), frame, "_y", AptValue::number(kNicknameFrameY), &frameErr) ||
			!windows().setAptMember(level(), frame, "_xscale", AptValue::number(kNicknameFrameXScale), &frameErr)) &&
		settled && m_loginPartsMissing.insert(frame).second)
	{
		windows().note("online-login", "the nickname frame was not placed: " + frameErr + " [S-4750]");
	}
	const std::string label = "OnlineLogin.Login.textAndBorders.InputLabels.AccountName";
	std::string err;
	if (!windows().setAptFieldText(level(), label, kNicknameLine, &err) && settled && m_loginPartsMissing.insert(label).second)
	{
		windows().note("online-login", "the nickname line has no place: " + err + " [S-4750]");
	}
}

void AptOnlineShell::login()
{
	// RW 0x97C5C6 (BFME2 0x572632 -> 0x57179D): the fields are read, a missing one is a box, else the request goes out and the buttons are disabled
	if (m_loginSent)
	{
		return;
	}
	prune();
	const std::u16string name = m_nickname ? trim(GadgetComboBoxGetText(m_nickname)) : std::u16string();
	if (name.empty())
	{
		showError("GUI:GSErrorTitle", "GUI:GSNoLoginInfoNickname");
		// lane ONLINEUX-1: the movie's Login button disables itself when pressed; it comes back with a name (RW 0x97A668 below), never stays dead
		applyLoginButton(true);
		return;
	}
	if (!m_client)
	{
		showBox(text("GUI:GSErrorTitle"), u"No online lobby is configured.", "GUI:GSErrorTitle|no lobby");
		return;
	}
	callChild("DisableButtonLogin");
	callChild("DisableButtonDeleteNickname");
	m_loginButton = 0; // lane ONLINEUX-1
	m_loginSent = true;
	m_loginName = name;
	// lane ONLINE-4: the session a restarted game kept (ONLINE-3's user://online_session.txt) signs in as the same player when the name is its own: the
	// lobby then knows the running game to rejoin. Another name is another guest
	if (m_ctx && !m_ctx->sessionToken.empty() && OnlineLobbyClient::sessionName(m_ctx->sessionToken) == name)
	{
		windows().note("online-session", "the kept lobby session of " + narrow(name) + " is used");
		m_client->useSession(m_ctx->sessionToken);
		return;
	}
	m_client->signInGuest(name);
}

// lane ONLINEUX-1: RW 0x97A668 (BFME2 0x5706D4, tier A, decomp AptOnlineLogin.cpp rva005706D4): the login page's buttons follow its fields. Login is
// enabled only while the fields can sign in (retail: an e-mail, a nickname with a printable character (iswgraph) and a password); the movie is told
// only when that changes (EnableButtonLogin / DisableButtonLogin through CallChild). Its callers: the gadget handler (BFME2 0x57166C) on a selection
// (0x4026) or a text change (0x402E) of the nickname, the page's fill, and a refused sign-in (loginEnabled cleared, then this). OpenBFME (S-4071):
// the guest sign-in has the nickname only, so Login follows the nickname alone. Before, an empty-name press left the button disabled for good (the
// movie disables it on its press and nothing enabled it again: the ONLINEUX-1 audit's dead end)
void AptOnlineShell::applyLoginButton(bool force)
{
	if (m_currentName != "OnlineLogin" || !m_pages.count("OnlineLogin"))
	{
		return;
	}
	prune();
	const std::u16string name = m_nickname ? GadgetComboBoxGetText(m_nickname) : std::u16string();
	const bool printable = std::any_of(name.begin(), name.end(), [](char16_t c) { return c > 0x20 && c != 0x7F && c != 0xA0; });
	const int enabled = printable && !m_loginSent ? 1 : 0;
	if (!force && enabled == m_loginButton)
	{
		return;
	}
	m_loginButton = enabled;
	callChild(enabled ? "EnableButtonLogin" : "DisableButtonLogin");
}

// lane ONLINEUX-1: the player's words for a refusal. The lobby's own messages are written for the logs (raw profile digests, "wrong password", a
// transport's result number): a refusal the player can act on is said as what happened and what to do; RotWK's own texts where it has them
// (GUI:JoinFailedBadPassword, GUI:JoinFailedRoomFull); anything else is the lobby's text. Every message is broken into lines (wrapLines)
std::u16string AptOnlineShell::playerMessage(const std::string &request, const std::string &code, const std::u16string &lobbyText) const
{
	if (code == "transport")
	{
		std::string host = m_ctx ? m_ctx->lobbyUrl : std::string();
		const size_t scheme = host.find("://");
		host = scheme == std::string::npos ? host : host.substr(scheme + 3);
		host = host.substr(0, host.find('/'));
		return u"Can't reach the OpenBFME online lobby" + (host.empty() ? std::u16string() : u" (" + wide(host) + u")") +
			u". Check your internet connection, then try again. If it keeps failing, the lobby may be down: the OpenBFME Discord says when it is.";
	}
	if (code == "profile")
	{
		return u"You can't join this game: it was made with a different OpenBFME version or different game files. Both players need the same "
			   u"OpenBFME version and unmodified RotWK 2.01 files.";
	}
	if (code == "password")
	{
		return text("GUI:JoinFailedBadPassword");
	}
	if (code == "full")
	{
		return text("GUI:JoinFailedRoomFull");
	}
	if (code == "started")
	{
		return u"This game has started already: it can't be joined any more.";
	}
	if (code == "no_game")
	{
		return request == "join" ? text("GUI:HostLeft") : u"This game is gone: its host closed it, or it has ended.";
	}
	if (code == "signed_out")
	{
		return u"Your online session has expired. Log out and log in again.";
	}
	if (code == "rate_limited")
	{
		return u"Too many requests at once: wait a moment, then try again.";
	}
	if (code == "protocol")
	{
		return u"The online lobby sent an answer this OpenBFME version does not understand (" + lobbyText + u"). Check for an OpenBFME update.";
	}
	return lobbyText;
}

std::u16string AptOnlineShell::wrapLines(const std::u16string &text, size_t width)
{
	// at the last space before `width` characters (a word longer than a line stays whole); the text's own line breaks are kept
	std::u16string out;
	size_t lineStart = 0;
	for (size_t i = 0; i < text.size(); ++i)
	{
		out += text[i];
		if (text[i] == u'\n')
		{
			lineStart = out.size();
			continue;
		}
		if (out.size() - lineStart > width)
		{
			const size_t space = out.rfind(u' ');
			if (space != std::u16string::npos && space >= lineStart)
			{
				out[space] = u'\n';
				lineStart = space + 1;
			}
		}
	}
	return out;
}

bool AptOnlineShell::gameLeft(const std::string &id) const
{
	if (m_ctx && m_ctx->leftGames && m_ctx->leftGames->count(id))
	{
		return true;
	}
	// lane ONLINEUX-1 r2 (Sol 4): a game left in an earlier run (this run's set is empty after a restart): the lobby's /v1/mine names it
	if (!m_client)
	{
		return false;
	}
	const std::vector<std::string> &left = m_client->mineLeft();
	return std::find(left.begin(), left.end(), id) != left.end();
}

void OnlineShellContext::gameFailed(const std::u16string &why)
{
	pendingNotice = why;
	if (client)
	{
		client->forgetPlayedGame();
	}
}

// ---- OnlineHome ------------------------------------------------------------------------------------------------------------------------------------------

void AptOnlineShell::registerHome()
{
	const std::string page = "OnlineHome";
	pageCommand(page, "AptOnline::OnlineHome::OnOpened", [this](const std::string &) { fillMotd(); }); // RW 0x9C8ED2
	pageCommand(page, "AptOnline::OnlineHome::OfficialSite", [this](const std::string &) { notAvailable("The ladder site (URL:LotrLadder)", "[S-4072]"); });
	pageScreenRef(page, "AptOnlineHome::InitGadgets", [this](const std::string &name, GameWindow *w) {
		// RW 0x9C8EDA (BFME2 0x5B9B73): the list box is reset; OnlineHome::MessageOfTheDay is kept and filled
		if (!w)
		{
			return;
		}
		GadgetListBoxReset(w);
		if (name == "OnlineHome::MessageOfTheDay")
		{
			candidate(m_motd, w);
			fillMotd();
		}
	});
	pageProvider(page, "OnlineHome::NumTickerFields", [](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = "4"; // RW 0x9C8350 answers 12; lane ONLINEUX-1 (S-5142): the four rows fillTicker fills (the others showed the movie's placeholder)
		}
		return true;
	});
	m_tickerRows.clear();
	m_tickerClipsDone = 0;
	fillTicker();
	// lane ONLINEUX-1 (S-5143): the home page's line for the TOURNAMENT button (APT:HomeTournamentDesc) promised a "stat-tracked ladder match against
	// other ranked Tournament competitors": that button is where Quick Match and OpenBFME's tournaments are; the line says so
	windows().setAptText("APT:HomeTournamentDesc", "Tournament - Quick Match: the server pairs you with players looking for the same game; and "
												   "OpenBFME's scheduled tournaments.");
	// the constructor's message-of-the-day request (peer request 0x17)
	if (m_client)
	{
		m_client->fetchMotd();
	}
}

void AptOnlineShell::fillMotd()
{
	prune();
	m_motdLines.clear();
	if (!m_client)
	{
		m_motdLines.push_back("No online lobby is configured.");
	}
	else if (!m_client->motdKnown() && !m_motdError.empty())
	{
		m_motdLines.push_back(narrow(u"The lobby's message of the day could not be read: " + m_motdError)); // Sol r1: kept for a later fill
	}
	else if (!m_client->motdKnown())
	{
		m_motdLines.push_back("..."); // the lobby has not answered yet (the answer refills the list)
	}
	else if (!m_client->hasMotd())
	{
		m_motdLines.push_back(narrow(kNoMotd));
	}
	else
	{
		std::u16string motd = m_client->motd();
		size_t at = 0;
		while (at <= motd.size())
		{
			const size_t nl = motd.find(u'\n', at);
			m_motdLines.push_back(narrow(motd.substr(at, nl == std::u16string::npos ? std::u16string::npos : nl - at)));
			if (nl == std::u16string::npos)
			{
				break;
			}
			at = nl + 1;
		}
	}
	if (!m_motd)
	{
		return;
	}
	GadgetListBoxReset(m_motd);
	for (const std::string &line : m_motdLines)
	{
		std::u16string w;
		OnlineStart::utf8ToUtf16(line, w);
		GadgetListBoxAddEntryText(m_motd, w, 0xFFFFFFFFu, -1, 0);
	}
}

// ---- OnlineOpenPlay (AptOnlineCustomMatch) ----------------------------------------------------------------------------------------------------------------

void AptOnlineShell::registerOpenPlay()
{
	const std::string page = "OnlineOpenPlay";
	m_state = 0;
	m_listDirty = true;
	pageCommand(page, "AptOnline::CustomMatch::CancelPopUpJoin", [this](const std::string &) {
		if (m_state == 10) // RW 0x9A8483
		{
			callPage("ClosePassword");
			m_state = 1;
			m_popUp = false;
			restoreMode(); // lane UBERJOIN-1: the join was cancelled after a switch
		}
	});
	pageCommand(page, "AptOnline::CustomMatch::CreateGame", [this](const std::string &) {
		if (m_state == 2 || m_state == 3) // RW 0x9A8423
		{
			prune();
			if (m_gameName)
			{
				m_gameName->winEnable(false);
			}
			if (m_gamePassword)
			{
				m_gamePassword->winEnable(false);
			}
			m_state = 4;
		}
	});
	pageCommand(page, "AptOnline::CustomMatch::JoinGame", [this](const std::string &) {
		if (m_state == 2) // RW 0x9A845E
		{
			m_state = 9;
		}
	});
	pageCommand(page, "AptOnline::CustomMatch::LoadGame", [this](const std::string &) { notAvailable("Loading a saved online game", "[S-4074]"); });
	pageCommand(page, "AptOnline::CustomMatch::AcceptJoinGame", [this](const std::string &) {
		// RW 0x9AEE9C (BFME2 AptOnlineCustomMatchAcceptJoinGame.cpp)
		if (m_state != 10)
		{
			return;
		}
		prune();
		const std::string password = m_gamePassword ? narrow(GadgetTextEntryGetText(m_gamePassword)) : std::string();
		m_popUp = false;
		if (password.empty())
		{
			callPage("ClosePassword");
			m_state = 1;
			showError("GUI:JoinFailedDefault", "GUI:JoinFailedBadPassword");
			return;
		}
		const auto it = std::find(m_rowIds.begin(), m_rowIds.end(), m_joinId);
		if (it == m_rowIds.end())
		{
			callPage("ClosePassword");
			m_state = 1;
			showError("GUI:JoinFailedDefault", "GUI:HostLeft");
			return;
		}
		requestJoin(m_joinId, password);
	});
	pageCommand(page, "AptOnline::CustomMatch::PlayGame", [this](const std::string &) { m_state = 7; }); // RW 0x9A8416
	pageCommand(page, "AptOnline::CustomMatch::Cancel", [this](const std::string &) {
		// BFME2 0x59FC45: state 8 only clears the start flag; else the room is left and the state 0
		if (m_state == 8)
		{
			return;
		}
		if (m_client && m_client->inGame())
		{
			m_client->leaveGame();
		}
		backToList();
	});
	pageCommand(page, "AptOnline::CustomMatch::Refresh", [this](const std::string &) {
		if (m_client) // RW 0x9AA76A: the list asked for again
		{
			m_client->refreshList(true);
			m_client->fetchMine();
		}
	});
	pageCommand(page, "AptOnline::OnOpenCreateDialog", [this](const std::string &) {
		// RW 0x9AA774 (BFME2 0x5A0F1F): FillGameNameTextEntry(true), the focus to the entry, state 3
		fillCreateEntries();
		m_state = 3;
	});
	pageCommand(page, "AptOnline::CustomMatch::CancelPopUpCreate", [this](const std::string &) {
		m_state = 1; // RW 0x9A84C9
		m_popUp = false;
	});
	pageCommand(page, "AptOnline::CustomMatch::CancelPopUpHost", [this](const std::string &) {
		callPage("ClosePassword"); // RW 0x9A84DD
		m_state = 1;
		m_popUp = false;
	});
	// lane ONLINE-5: the CONNECTIONS button: the page's OpenConnectionScreen closes the room's panels, loads Connections.swf and sends
	// OnOpenConnectionsScreen. Retail's screen shows the GameSpy connection (NAT negotiation) between every two players; OpenBFME's players do not
	// connect to each other (every one talks to the relay), so there is nothing for it to show [S-4550]: the player is told, and the room's panels
	// come back through the page's own close (StartCloseConnectionsScreen reopens them, OnConnectionsScreenClosed unloads the movie). The handler
	// used to do nothing: the room's panels went and an empty screen stayed
	pageCommand(page, "AptOnline::CustomMatch::OnOpenConnectionsScreen", [this](const std::string &) {
		m_closeConnections = 2; // the page's own functions run from the next updates (not inside the button's script)
		notAvailable("the Connections screen (online games go through the relay server, players do not connect to each other)", "[S-4550]");
	});
	pageCommand(page, "AptOnline::CustomMatch::OnClosingConnectionsScreen", [](const std::string &) {});
	// lane MENUWORK-1 (S-4550): the Connections movie draws its player grid by calling Connection::RedrawGrid (a game.dat name of the screen's own
	// registrations, not ported: the screen is replaced by the box above); the call reached no handler and was only a "command-unhandled" note
	pageCommand(page, "Connection::RedrawGrid", [this](const std::string &) {
		windows().note("unported", "Connection::RedrawGrid: the Connections screen is not available in OpenBFME's online play [S-4550]");
	});
	// GameSorter (RW 0x98821C ..): the columns of the list
	pageCommand(page, "GameSorter::OnSortName", [this](const std::string &) { sortBy(0); });
	pageCommand(page, "GameSorter::OnSortMap", [this](const std::string &) { sortBy(1); });
	pageCommand(page, "GameSorter::OnSortPlayers", [this](const std::string &) { sortBy(2); });
	pageCommand(page, "GameSorter::OnSortPing", [this](const std::string &) { sortBy(3); });
	pageCommand(page, "GameSorter::OnSortStatus", [this](const std::string &) { sortBy(4); });
	pageScreenRef(page, "AptOnlineCustomMatch::InitGadgets", [this](const std::string &name, GameWindow *w) {
		// RW 0x9AA4C3
		if (!w)
		{
			return;
		}
		if (name == "GameList")
		{
			candidate(m_gameList, w);
			m_listDirty = true;
		}
		else if (name == "Lobbies")
		{
			candidate(m_lobbies, w); // GameSpy's group rooms: one lobby here [S-4073]
			// lane ONLINEUX-1: the drop-down showed the movie's placeholder "Entry" and opened an empty list: it lists the one room every player of
			// the lobby is in (retail's PopulateLobbyComboBox, BFME2 0x5A1EEE's call, lists GameSpy's group rooms). The movie enables its components
			// after this (EnableComponents), so the disable below does not hold: the drop-down opens on that one room
			if (BitTest(w->winGetStyle(), GWS_COMBO_BOX))
			{
				GadgetComboBoxReset(w);
				GadgetComboBoxAddEntry(w, u"OpenBFME", 0xFFFFFFFFu);
				GadgetComboBoxSetSelectedPos(w, 0, false);
				GadgetComboBoxSetText(w, u"OpenBFME");
			}
			w->winEnable(false);
		}
		else if (name == "GameInfo")
		{
			candidate(m_gameInfo, w);
		}
		else if (name == "GameName" || name == "GamePassword")
		{
			GameWindow *&slot = name == "GameName" ? m_gameName : m_gamePassword;
			candidate(slot, w);
			if (EntryData *entry = static_cast<EntryData *>(w->winGetUserData()))
			{
				entry->maxTextLen = 20; // RW 0x81606D(entry, 0x14)
			}
			// the pop-ups' own entries come after OnOpenCreateDialog / EnterPassword (the page places them as it shows the pop-up): RW 0x9AA5AB calls
			// RW 0x9AA0A7 at every GameName / GamePassword placement. INFERENCE (S-4074): it fills them as FillGameNameTextEntry (BFME2 0x5A0DC2) does
			if (m_state == 3)
			{
				fillCreateEntries();
			}
			else if (m_state == 10)
			{
				fillJoinEntries();
			}
		}
	});
	if (m_client)
	{
		m_client->refreshList(true);
		if (m_client->signedIn())
		{
			m_client->fetchMine(); // lane ONLINE-4: the player's running games are rows too (REJOIN)
		}
	}
}

// lane ONLINEUX-1: the server browser's MAP / RULES panel is MpGameSetup's, read only outside a room (lobbyEditable). It showed the player's own last
// map whatever game was selected, and its map list took clicks that changed nothing: the panel follows the game selected in the list instead (its map
// chosen in the panel's model, the panel redrawn: the preview, the description, the list's highlight), as retail's custom match page shows the
// selected game's information (GameInfo, RW 0x9AA4C3). The player's own map comes back when they create a game. INFERENCE (S-5141): the retail
// page's fill of the panel for a selection is not read; the lobby's list carries the map, not the rules (the RULES tab keeps the player's own)
void AptOnlineShell::showSelectedGameMap()
{
	prune();
	SkirmishGameSetup *model = setupModel();
	if (!m_gameList || !model || !m_client || (m_client->inGame() && m_state != 2))
	{
		return;
	}
	int selected = -1;
	GadgetListBoxGetSelected(m_gameList, &selected);
	if (selected < 0 || selected >= (int)m_rowIds.size())
	{
		return;
	}
	std::string map;
	for (const std::vector<OnlineLobbyClient::Listing> *list : { &m_client->mine(), &m_client->games() })
	{
		for (const OnlineLobbyClient::Listing &g : *list)
		{
			map = g.id == m_rowIds[(size_t)selected] ? g.map : map;
		}
	}
	if (map.empty() || !model->findMap(map))
	{
		return; // a map the player does not have (the list shows its file name): nothing to show
	}
	if (m_selectedMap.empty())
	{
		m_ownMap = model->info().mapName; // the player's own, for a game they create
	}
	if (model->info().mapName != map)
	{
		SkirmishGameInfo info = model->info();
		info.mapName = map;
		model->replaceInfo(info);
		refreshLobby();
	}
	m_selectedMap = map;
}

// lane ONLINEUX-1: the home page's ticker. Retail's rows (RW 0x9C8694, BFME2 0x5B9378 byte-matched): APT:TickerField_<i> = the game text of the i-th
// label of the table RW 0xDB9290 with ":" appended, APT:TickerValue_<i> = the value; its update (RW 0x9C881D, BFME2 0x5B9501) writes the date (row 0)
// and time (row 1) every time, and the 1v1 / 2v2 ladder ranks (rows 2, 3) when it has them; GameSpy's answers filled the players online and games in
// progress (rows 4, 5, peer response 21) and each faction's win rate (rows 6 .. 12). The movie shows OnlineHome::NumTickerFields rows (retail 12): the
// rows it was never given showed the movie's placeholder ("Men of The West 99.99%"). OpenBFME (S-5142) has the date, the time and the lobby's counts
// (GET /v1/stats: players online, games running); no ladder and no community win rates on this page (the STATS page has the community's): four rows,
// in retail's order of their labels
void AptOnlineShell::fillTicker()
{
	const std::time_t now = std::time(nullptr);
	std::tm local{};
	if (const std::tm *t = std::localtime(&now)) // the UI thread's only use
	{
		local = *t;
	}
	char date[32], clock[16];
	std::strftime(date, sizeof(date), "%Y-%m-%d", &local); // INFERENCE: retail formats with the system's locale (RW 0x6DBFAD's GetDateFormat): ISO here
	std::strftime(clock, sizeof(clock), "%H:%M", &local);
	auto count = [](int n) { return n >= 0 ? std::to_string(n) : std::string("..."); };
	const std::pair<const char *, std::string> rows[] = { { "APT:Date", date }, { "APT:Time", clock },
		{ "APT:PlayersOnline", count(m_ctx ? m_ctx->playersOnline : -1) }, { "APT:GamesInProgress", count(m_ctx ? m_ctx->gamesRunning : -1) } };
	std::vector<std::string> shown;
	for (size_t i = 0; i < std::size(rows); ++i)
	{
		const std::string field = narrow(text(rows[i].first)) + ":";
		shown.push_back(field + "|" + rows[i].second);
		if (i < m_tickerRows.size() && m_tickerRows[i] == shown.back())
		{
			continue;
		}
		windows().setAptText("APT:TickerField_" + std::to_string(i), field);
		windows().setAptText("APT:TickerValue_" + std::to_string(i), rows[i].second);
	}
	m_tickerRows = shown;
	// the rows' clips: OnlineHome.apt's InitTicker attaches TickerInfo_<i> (its export TickerInfo) and the class registered for it
	// (Object.registerClass("TickerInfo", TickerInfoObject)) sets Field.text = "$TickerField_" + i and Value.text = "$TickerValue_" + i (the player's
	// movie, decoded). This port's interpreter does not run registered classes (registerClass is unported: S-104), so the rows kept the clip's own
	// placeholder text; the screens give each row the two texts that constructor gives it (S-5142)
	if (level() < 0 || m_tickerClipsDone >= (int)std::size(rows))
	{
		return;
	}
	std::function<AptCharacterInst *(AptCharacterInst *, const std::string &)> find = [&](AptCharacterInst *c, const std::string &suffix) -> AptCharacterInst * {
		if (!c)
		{
			return nullptr;
		}
		const std::string path = c->targetPath();
		if (path.size() >= suffix.size() && path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0)
		{
			return c;
		}
		if (AptSpriteInst *sp = c->asSprite())
		{
			for (AptCharacterInst *k : sp->children())
			{
				if (AptCharacterInst *f = find(k, suffix))
				{
					return f;
				}
			}
		}
		return nullptr;
	};
	AptCharacterInst *root = windows().apt().level(level());
	while (m_tickerClipsDone < (int)std::size(rows))
	{
		const std::string index = std::to_string(m_tickerClipsDone);
		AptCharacterInst *clip = find(root, ".TickerInfo_" + index);
		if (!clip)
		{
			return; // not attached yet: the next update
		}
		std::string path = clip->targetPath();
		path = path.substr(path.find('.') + 1); // the level's own path ("_levelN." off)
		std::string err;
		if (!windows().setAptFieldText(level(), path + ".Field", "$TickerField_" + index, &err) ||
			!windows().setAptFieldText(level(), path + ".Value", "$TickerValue_" + index, &err))
		{
			windows().note("online-ticker", "ticker row " + index + " has no Field / Value: " + err + " [S-5142]");
		}
		++m_tickerClipsDone;
	}
}

void AptOnlineShell::fillCreateEntries()
{
	// BFME2 0x5A0DC2 FillGameNameTextEntry(true): the game name is the player's (GameModePreferences' last name for it, rva0044DAA8: here the account's
	// name), the password the preferences' (here empty), both entries enabled
	prune();
	if (m_gameName)
	{
		GadgetTextEntrySetText(m_gameName, m_client ? m_client->accountName() : std::u16string());
		m_gameName->winEnable(true);
	}
	if (m_gamePassword)
	{
		GadgetTextEntrySetText(m_gamePassword, UnicodeString());
		m_gamePassword->winEnable(true);
	}
}

void AptOnlineShell::fillJoinEntries()
{
	// BFME2 0x5A0DC2 FillGameNameTextEntry(false): the game to join's name, the name entry disabled; the password entry empty and enabled
	prune();
	std::u16string name;
	if (m_client)
	{
		for (const OnlineLobbyClient::Listing &g : m_client->games())
		{
			name = g.id == m_joinId ? g.name : name;
		}
	}
	if (m_gameName)
	{
		GadgetTextEntrySetText(m_gameName, name);
		m_gameName->winEnable(false);
	}
	if (m_gamePassword)
	{
		GadgetTextEntrySetText(m_gamePassword, UnicodeString());
		m_gamePassword->winEnable(true);
	}
}

GameWindow *AptOnlineShell::gameList()
{
	prune();
	return m_gameList;
}
GameWindow *AptOnlineShell::nicknameWindow()
{
	prune();
	return m_nickname;
}
GameWindow *AptOnlineShell::gameNameWindow()
{
	prune();
	return m_gameName;
}
GameWindow *AptOnlineShell::gamePasswordWindow()
{
	prune();
	return m_gamePassword;
}
GameWindow *AptOnlineShell::lobbiesWindow()
{
	prune();
	return m_lobbies;
}

void AptOnlineShell::candidate(GameWindow *&role, GameWindow *w)
{
	role = w;
	m_candidates[&role].push_back(w);
}

void AptOnlineShell::prune()
{
	// the pages place their gadgets twice (a CreateComponents frame, then the page's own; the login page in two clips): the windows of a removed clip
	// stay, hidden (WindowManager::componentInstanceDestroyed). A role takes the last live window that is shown, else the last live one (as
	// AptLanLobby::pruneLan follows a gadget's live twin)
	AptGadgetLayer *layer = windows().gadgetLayer();
	if (!layer)
	{
		return;
	}
	const std::vector<GameWindow *> live = layer->gadgets().allWindows();
	const std::set<const GameWindow *> alive(live.begin(), live.end());
	for (GameWindow **w : { &m_nickname, &m_email, &m_password, &m_remember, &m_motd, &m_gameList, &m_lobbies, &m_gameInfo, &m_gameName, &m_gamePassword })
	{
		std::vector<GameWindow *> &cands = m_candidates[w];
		cands.erase(std::remove_if(cands.begin(), cands.end(), [&](GameWindow *c) { return !alive.count(c); }), cands.end());
		GameWindow *best = cands.empty() ? nullptr : cands.back();
		for (GameWindow *c : cands)
		{
			if (!c->winIsHidden())
			{
				best = c; // the last one shown
			}
		}
		if (*w != best)
		{
			if (w == &m_gameList)
			{
				m_listDirty = true;
			}
			*w = best;
		}
	}
	pruneDeadGadgets();
}

void AptOnlineShell::sortBy(int column)
{
	// GameSorter: a second press of the same column reverses it (INFERENCE: RotWK's sorter RW 0x988BB5 not read, S-4073)
	m_sortDescending = m_sortColumn == column ? !m_sortDescending : false;
	m_sortColumn = column;
	m_listDirty = true;
}

void AptOnlineShell::fillGamesList()
{
	prune();
	if (!m_gameList || !m_client)
	{
		return;
	}
	m_listDirty = false;
	int selected = -1;
	GadgetListBoxGetSelected(m_gameList, &selected);
	const std::string keep = selected >= 0 && selected < (int)m_rowIds.size() ? m_rowIds[(size_t)selected] : std::string();
	// lane ONLINE-4 (S-4077): the player's own running games first (REJOIN), then the lobby's games: open ones (Join Game) and running ones of others
	// (WATCH: a spectator START)
	// lane ONLINEUX-1: a game the player left is neither theirs to rejoin nor to watch (the lobby refuses a player of a game as its spectator): not listed
	std::vector<OnlineLobbyClient::Listing> games;
	for (const OnlineLobbyClient::Listing &g : m_client->mine())
	{
		if (!gameLeft(g.id))
		{
			games.push_back(g);
			games.back().state = "rejoin";
		}
	}
	for (const OnlineLobbyClient::Listing &g : m_client->games())
	{
		if (!gameLeft(g.id) && std::none_of(games.begin(), games.end(), [&](const OnlineLobbyClient::Listing &m) { return m.id == g.id; }))
		{
			games.push_back(g);
			games.back().state = g.state == "started" ? "watch" : "join";
		}
	}
	std::map<std::string, std::u16string> titles;
	for (const OnlineLobbyClient::Listing &g : games)
	{
		if (!titles.count(g.map))
		{
			titles[g.map] = mapTitle(g.map);
		}
	}
	if (m_sortColumn >= 0)
	{
		const int col = m_sortColumn;
		const bool desc = m_sortDescending;
		std::stable_sort(games.begin(), games.end(), [col, desc, &titles](const OnlineLobbyClient::Listing &a, const OnlineLobbyClient::Listing &b) {
			int c = 0;
			switch (col)
			{
			case 0: c = a.name < b.name ? -1 : (b.name < a.name ? 1 : 0); break;
			case 1: c = titles.at(a.map) < titles.at(b.map) ? -1 : (titles.at(b.map) < titles.at(a.map) ? 1 : 0); break;
			case 2: c = a.players - b.players; break;
			case 4: c = (int)a.password - (int)b.password; break;
			default: c = 0; break; // the lobby gives no ping [S-4073]
			}
			return desc ? c > 0 : c < 0;
		});
	}
	GadgetListBoxReset(m_gameList);
	if (GadgetListBoxGetNumColumns(m_gameList) < 7)
	{
		static const int widths[7] = { 4, 5, 5, 36, 33, 10, 7 }; // BFME2 0x59F496
		GadgetListBoxSetColumnWidths(m_gameList, 7, widths);
	}
	m_gameRows.clear();
	m_rowIds.clear();
	m_rowKinds.clear();
	m_rowModes.clear();
	for (const OnlineLobbyClient::Listing &g : games)
	{
		const Color c = g.state == "join" ? 0xFFFFFFFFu : 0xFFFFD27Fu; // a running game's row in the gold of the movie's highlighted text
		std::u16string label = g.state == "rejoin" ? g.name + u"  - REJOIN" : g.state == "watch" ? g.name + u"  - WATCH" : g.name;
		if (g.mode == SimulationProfile::Uber)
		{
			label += u"  - UBER"; // lane UBERJOIN-1: the game's performance mode, in the style of the REJOIN / WATCH tags (a Normal game has no tag)
		}
		const int row = GadgetListBoxAddEntryText(m_gameList, label, c, -1, 3);
		if (g.hostPlatform != PlatformKind::None) // lane PLATFORMICON-1: the host's system before the game's name
		{
			if (MappedImageCollection *images = m_gameList->manager().mutableImages())
			{
				registerPlatformIcons(*images);
			}
			GadgetListBoxSetCellBadge(m_gameList, row, 3, platformImageName(g.hostPlatform));
		}
		if (g.password)
		{
			GadgetListBoxAddEntryImage(m_gameList, "AptLock", row, 2, 20, 20, true); // BFME2 0x59F496: the lock of a locked game
		}
		const std::u16string &map = titles.at(g.map);
		const std::string players = std::to_string(g.players) + "/" + std::to_string(g.players + g.openSlots);
		GadgetListBoxAddEntryText(m_gameList, map, c, row, 4);
		GadgetListBoxAddEntryText(m_gameList, wide(players), c, row, 5);
		GadgetListBoxAddEntryText(m_gameList, UnicodeString(), c, row, 6);
		m_gameRows.push_back(narrow(g.name) + "|" + narrow(map) + "|" + players + "|" + (g.password ? "lock" : "") + (g.state == "join" ? "" : "|" + g.state) +
			(g.mode == SimulationProfile::Uber ? "|uber" : ""));
		m_rowIds.push_back(g.id);
		m_rowKinds.push_back(g.state);
		m_rowModes.push_back(g.mode);
		if (!keep.empty() && g.id == keep)
		{
			GadgetListBoxSetSelected(m_gameList, row);
		}
	}
}

std::u16string AptOnlineShell::mapTitle(const std::string &key)
{
	// BFME2 0x59F496 (the list's mode 0): the game's map found in the map cache (AptMapPreview 0x57D922) and its
	// MapMetaData::bfme_getBaseDisplayName (RotWK 0x00300AEA: the map.str / game text label), as the skirmish map list shows it
	if (const SkirmishGameSetup *s = setup())
	{
		if (const MapCacheEntry *m = s->findMap(key))
		{
			return s->mapBaseDisplayName(*m);
		}
	}
	// a map the player does not have (retail's lookup has no answer for it): its file name, noted once
	if (m_unknownMaps.insert(key).second)
	{
		windows().note("online-map-unknown", "the listed map " + key + " is not in this player's map cache: shown by its file name");
	}
	return wide(mapLeaf(key));
}

bool AptOnlineShell::startUsable(const std::string &kind, std::string *why) const
{
	// Sol r1: the START is read as the host program will read it (GodotGameWorld::net_online: OnlineStart::parse, a spectator's as one) before it is
	// handed on: a malformed one is a lobby error the player is told about, never the game's end
	if (!m_client)
	{
		*why = "no lobby client";
		return false;
	}
	ProfileIdentity own;
	own.digest = m_client->options().profileDigest;
	OnlineStart::Limits limits;
	limits.playerTemplateCount = m_client->options().playerTemplateCount;
	limits.colorCount = m_client->options().colorCount;
	LobbyStart start;
	return OnlineStart::parse(m_client->startJson(), own, limits, start, why, kind == "spectate" ? OnlineStart::Kind::Spectate : OnlineStart::Kind::Start);
}

void AptOnlineShell::startFailed(const std::string &why)
{
	const std::string safe = OnlineLobbyClient::redact(why);
	windows().note("online-start-failed", safe);
	m_rejoinPending = false;
	if (m_client)
	{
		m_client->dropStart(); // no leave: the player keeps the slot (a running game is offered for a rejoin)
	}
	const std::u16string body = u"The online game could not start: " + std::u16string(safe.begin(), safe.end());
	if (m_pages.count("OnlineOpenPlay"))
	{
		backToList();
	}
	m_connecting = false;
	showBox(text("GUI:GSErrorTitle"), body, "GUI:GSErrorTitle|" + narrow(body));
}

void AptOnlineShell::joinSelected()
{
	// BFME2 0x5A57BA (fromInvite false)
	prune();
	m_state = 1;
	int selected = -1;
	if (m_gameList)
	{
		GadgetListBoxGetSelected(m_gameList, &selected);
	}
	if (selected < 0 || selected >= (int)m_rowIds.size())
	{
		showError("GUI:Error", "GUI:NoGameSelected");
		return;
	}
	const std::string id = m_rowIds[(size_t)selected];
	const std::string kind = selected < (int)m_rowKinds.size() ? m_rowKinds[(size_t)selected] : std::string("join");
	// lane UBERJOIN-1: a game of the other performance mode asks first (Yes switches and comes back here, No stays in the list)
	if (selected < (int)m_rowModes.size() && !modeAllows(m_rowModes[(size_t)selected], [this]() { joinSelected(); }))
	{
		return;
	}
	if (kind == "rejoin" || kind == "watch")
	{
		// lane ONLINE-4 (S-4077): Join Game on the player's running game asks for a rejoin START, on another running game for a spectator START
		startRunning(id, kind == "rejoin");
		return;
	}
	const auto &games = m_client->games();
	const auto it = std::find_if(games.begin(), games.end(), [&](const OnlineLobbyClient::Listing &g) { return g.id == id; });
	if (it == games.end())
	{
		showError("GUI:Error", "GUI:NoGameInfo");
		return;
	}
	// the exe / INI CRC check is the lobby's profile comparison at the join (a refusal is its "profile" error)
	if (it->openSlots == 0)
	{
		showError("GUI:JoinFailedDefault", "GUI:JoinFailedRoomFull");
		return;
	}
	m_joinId = id;
	if (it->password)
	{
		m_popUp = true;
		callPage("EnterPassword");
		m_state = 10;
		fillJoinEntries();
		return;
	}
	requestJoin(id, std::string());
}

void AptOnlineShell::startRunning(const std::string &id, bool rejoin)
{
	if (!m_client)
	{
		return;
	}
	// lane UBERJOIN-1: a running game of the other performance mode asks first
	SimulationProfile mode = m_client->profile();
	for (const std::vector<OnlineLobbyClient::Listing> *list : { &m_client->mine(), &m_client->games() })
	{
		const auto it = std::find_if(list->begin(), list->end(), [&](const OnlineLobbyClient::Listing &l) { return l.id == id; });
		if (it != list->end())
		{
			mode = it->mode;
			break;
		}
	}
	if (!modeAllows(mode, [this, id, rejoin]() { startRunning(id, rejoin); }))
	{
		return;
	}
	windows().note("online-rejoin", std::string(rejoin ? "rejoin" : "watch") + " game " + id + " [S-4077]");
	m_runningId = id; // lane ONLINEUX-1
	if (rejoin)
	{
		m_client->rejoinGame(id);
	}
	else
	{
		m_client->spectateGame(id);
	}
	m_rejoinPending = true;
	if (m_pages.count("OnlineOpenPlay"))
	{
		m_state = 13; // OpenBFME's (S-4077): waiting for the running game's START
		m_connecting = true;
		callPage("PopUpConnectingOpen");
	}
}

bool AptOnlineShell::modeAllows(SimulationProfile gameMode, std::function<void()> proceed)
{
	// lane UBERJOIN-1 (S-4860): a game in the other performance mode is joined by switching to its mode for the game, never refused silently and never
	// with an error box: the game's own Yes / No box asks, Yes switches (the player's setting is restored when the room is left or the game ended)
	if (!m_client || gameMode == m_client->profile())
	{
		return true;
	}
	const std::u16string body = performanceModeSwitchPrompt(gameMode);
	m_boxes.push_back("mode-switch|" + narrow(body));
	windows().note("online-box", "mode-switch|" + narrow(body));
	box().show(AptMessageBox::TYPE_YES_NO, kModeTitle, body, [this, gameMode, proceed](int button) {
		if (button != 2) // No: stay in the list
		{
			m_state = 1;
			return;
		}
		std::string why;
		if (!switchMode(gameMode, &why))
		{
			const std::u16string text = u"The performance mode could not be switched: " + std::u16string(why.begin(), why.end());
			showBox(kModeTitle, text, "mode-switch-failed|" + narrow(text));
			m_state = 1;
			return;
		}
		proceed();
	});
	return false;
}

bool AptOnlineShell::switchMode(SimulationProfile mode, std::string *why)
{
	ProfileSwitch *ps = environment().profileSwitch;
	if (!ps || !ps->complete())
	{
		*why = "this build cannot switch the performance mode";
		return false;
	}
	if (!ps->request(mode, why))
	{
		return false;
	}
	return m_client->setProfile(mode, ps->digestOf(mode), why);
}

void AptOnlineShell::syncMode()
{
	// the client follows the host program's mode (the player's setting, or the one switched to for a game, undone when the game ended)
	ProfileSwitch *ps = environment().profileSwitch;
	if (!ps || !ps->complete() || !m_client || m_client->inGame())
	{
		return;
	}
	const SimulationProfile now = ps->current();
	std::string why;
	if (now != m_client->profile() && !m_client->setProfile(now, ps->digestOf(now), &why))
	{
		windows().note("online-mode", "the lobby client could not follow the performance mode: " + why);
	}
}

void AptOnlineShell::restoreMode()
{
	// the room was left (or the join failed or was cancelled): the player's own setting again
	if (ProfileSwitch *ps = environment().profileSwitch; ps && ps->complete() && m_client && !m_client->inGame())
	{
		ps->restore();
		syncMode();
	}
}

void AptOnlineShell::offerRejoin()
{
	// lane ONLINE-4 (S-4077): RotWK's movies have no Rejoin button; on sign-in the player's running game is offered in GuiFX's own Yes / No box
	if (!m_client || m_rejoinOffered)
	{
		return;
	}
	// lane ONLINEUX-1: a game this player left (finished, forfeited, quit) is not theirs to rejoin, even where the lobby still lists it (a lobby
	// before ONLINEUX-1 keeps a started game's slot after its player left)
	const auto mine = std::find_if(m_client->mine().begin(), m_client->mine().end(), [&](const OnlineLobbyClient::Listing &l) { return !gameLeft(l.id); });
	if (mine == m_client->mine().end())
	{
		return;
	}
	m_rejoinOffered = true;
	const OnlineLobbyClient::Listing g = *mine;
	const std::u16string body = u"Your game \"" + g.name + u"\" is still running. Rejoin it now?";
	m_boxes.push_back("rejoin-offer|" + narrow(body));
	windows().note("online-box", "rejoin-offer|" + narrow(body));
	box().show(AptMessageBox::TYPE_YES_NO, kRejoinTitle, body, [this, id = g.id](int button) {
		if (button == 2) // Yes
		{
			startRunning(id, true);
		}
	});
}

void AptOnlineShell::requestJoin(const std::string &id, const std::string &password)
{
	// BFME2 0x5A2C77 RequestJoinGame: the request, state 11
	m_client->joinGame(id, password);
	m_state = 11;
	m_popUp = true;
}

void AptOnlineShell::syncModel()
{
	SkirmishGameSetup *model = setupModel();
	if (!model || !m_client || !m_client->inGame())
	{
		return;
	}
	model->setNetwork(m_client->localSlot());
	model->replaceInfo(m_client->gameInfo());
	m_syncedSeq = m_client->viewSeq();
	refreshLobby();
}

void AptOnlineShell::enterSetup(bool host)
{
	// BFME2 0x5A20A7: the staging room: state 5 -> GotoAndPlay("_host"), 6; state 11 -> ClosePassword, GotoAndPlay("_join"), 12
	setHostModeText(host ? "1" : "0");
	m_ready = false;
	m_startSent = false;
	m_chat.clear();
	syncModel();
	if (m_client && m_client->gameMode() == SimulationProfile::Uber)
	{
		systemChat(u"This room uses Uber performance mode."); // lane UBERJOIN-1: the room's mode (a Normal room says nothing)
	}
	if (host)
	{
		callPage("GotoAndPlay", { "_host" });
		m_state = 6;
	}
	else
	{
		callPage("ClosePassword");
		callPage("GotoAndPlay", { "_join" });
		m_state = 12;
	}
	m_popUp = false;
}

void AptOnlineShell::backToList()
{
	// RW 0x9AB748 (BFME2 0x5A1EEE, byte-matched): the room left, ClosePassword, GotoAndPlay("_lobby"), EnableButtonCreateGame, DisableButtonJoinGame,
	// state 1
	// lane ONLINEUX-1: the room is left first (TheGameSpyInfo->leaveStagingRoom(), the function's first call); the port's callers left it themselves,
	// except the page's state 0 (the custom match page made again after a game: RW 0x9AFAD5), where the player stayed in the started game's room
	if (m_client && m_client->inGame())
	{
		m_client->leaveGame();
	}
	restoreMode(); // lane UBERJOIN-1
	callPage("ClosePassword");
	callPage("GotoAndPlay", { "_lobby" });
	callPage("EnableButtonCreateGame");
	callPage("DisableButtonJoinGame");
	m_state = 1;
	m_ready = false;
	m_startSent = false;
	m_popUp = false;
	if (m_connecting)
	{
		callPage("PopUpConnectingClose");
		m_connecting = false;
	}
	setHostModeText("1");
	m_listDirty = true;
	if (m_client)
	{
		m_client->refreshList(true);
	}
}

void AptOnlineShell::systemChat(const std::u16string &line)
{
	addChatLine(line, 0xFFFFFFFFu);
	m_chat.push_back(narrow(line));
}

void AptOnlineShell::startPressed()
{
	// state 7 (PlayGame): the host checks the setup as the LAN lobby does (RW 0x8431B8) and asks the lobby for the START; a joiner's Play Game is its ready
	if (!m_client || !m_client->inGame())
	{
		return;
	}
	if (!m_client->amIHost())
	{
		onReadyPress("PlayGame");
		return;
	}
	if (SkirmishGameSetup *model = setupModel())
	{
		// lane ONLINE-5: with the screen's own flags (0x20: a player who is not ready refuses the start with GUI:NotifiedStartIntent, RW 0x8431B8's
		// last test). The owner's vslot 0x44 (BFME2 0x59F043 MpOwnerPrintMessage): kind 2 is a line every player of the room gets, the others are local
		const SkirmishGameSetup::StartCheck check = model->checkStart(setupFlags());
		for (const SkirmishGameSetup::StartCheck::Line &line : check.lines)
		{
			if (line.kind == 2)
			{
				m_client->chat(line.text);
			}
			else
			{
				systemChat(line.text);
			}
		}
		if (!check.ok)
		{
			if (!check.refusal.empty())
			{
				m_startRefusals.push_back(check.refusalLabel);
			}
			windows().note("online-start-refused", check.refusalLabel + ": " + narrow(check.refusal));
			callPage("EnableButtonPlayGame");
			return;
		}
	}
	m_startSent = true;
	m_connecting = true;
	callPage("PopUpConnectingOpen"); // BFME2 0x59F37C: the connecting pop-up while the game is set up
	m_client->start();
}

// ---- the lobby client -----------------------------------------------------------------------------------------------------------------------------------

void AptOnlineShell::pumpClient()
{
	if (!m_client)
	{
		return;
	}
	m_client->update(NetMilliseconds());
	for (const OnlineLobbyClient::Event &e : m_client->takeEvents())
	{
		if (m_hub && m_hub->lobbyEvent(e))
		{
			continue; // lane ONLINEHUB-1: a Quick Match / tournament room's event (the hub makes and starts that room)
		}
		switch (e.kind)
		{
		case OnlineLobbyClient::Event::SignedIn:
			m_loginSent = false;
			if (m_ctx)
			{
				m_ctx->sessionToken = m_client->sessionToken();
				if (m_ctx->rememberSession)
				{
					m_ctx->rememberSession(m_ctx->sessionToken);
				}
			}
			if (!e.flag)
			{
				m_client->fetchMine(); // a kept session fetched them already
			}
			if (m_ctx && m_ctx->rememberName)
			{
				m_ctx->rememberName(e.text);
				m_ctx->rememberedName = e.text;
			}
			else
			{
				windows().note("online-name-not-kept", "the host keeps no guest name [S-4071]");
			}
			loadChildScreen("OnlineHome"); // BFME2 0x5700A0: a login that succeeded loads the home page
			break;
		case OnlineLobbyClient::Event::Mine:
			m_listDirty = true;
			offerRejoin();
			break;
		case OnlineLobbyClient::Event::Motd:
			fillMotd();
			break;
		case OnlineLobbyClient::Event::GameList:
			m_listDirty = true;
			break;
		case OnlineLobbyClient::Event::GameCreated:
			if (m_state == 4 || m_state == 5)
			{
				m_state = 5;
				enterSetup(true);
			}
			break;
		case OnlineLobbyClient::Event::GameJoined:
			if (m_state == 11)
			{
				enterSetup(false);
			}
			break;
		case OnlineLobbyClient::Event::GameUpdated:
			if (m_pollTrouble && m_client->inGame())
			{
				m_pollTrouble = false; // Sol r1: the lobby answers again
				systemChat(u"The lobby answers again.");
			}
			// a poll brings the view every second: the gadgets follow it only when the lobby changed it (its seq moved)
			if ((m_state == 6 || m_state == 12 || m_state == 7) && m_client->viewSeq() != m_syncedSeq)
			{
				syncModel();
			}
			break;
		case OnlineLobbyClient::Event::Chat:
		{
			// a player's line "[name] text", the lobby's own lines as system lines (as LANAPI::OnChat, RW 0x649E5C)
			const std::u16string line = e.flag ? e.text : u"[" + e.from + u"] " + e.text;
			addChatLine(line, 0xFFFFFFFFu);
			m_chat.push_back(narrow(line));
			break;
		}
		case OnlineLobbyClient::Event::Removed:
			if (e.code == "kicked")
			{
				showError("GUI:GSErrorTitle", "GUI:GSKicked"); // as the LAN lobby's state 9
				backToList();
			}
			else if (e.code == "closed")
			{
				showError("GUI:GSErrorTitle", "GUI:HostLeft");
				backToList();
			}
			break;
		case OnlineLobbyClient::Event::GameStart:
		{
			std::string why;
			if (!startUsable(e.code, &why))
			{
				startFailed(why);
				break;
			}
			if (environment().services)
			{
				// lane ONLINEUX-1: the game this player plays (a spectator's is not theirs): left when the screens come back after it
				m_client->setPlayedGame(e.code == "spectate" ? std::string() : e.code == "rejoin" ? m_runningId : m_client->gameId());
				environment().services->request(ShellRequest{ ShellAction::OnlineGameStart, m_client->startJson() });
			}
			m_box.reset(); // the box goes with the shell (its GuiFX.apt level is the game's, as the LAN lobby's start)
			break;
		}
		case OnlineLobbyClient::Event::Refused:
		{
			const std::string record = e.request + ": " + e.code + ": " + narrow(e.text);
			windows().note("online-refused", record);
			if (e.request == "session")
			{
				// the kept session is not this lobby's any more (expired, another lobby): a new guest of the same name (it has no running game)
				windows().note("online-session", "the kept lobby session was refused (" + e.code + "): signing in as a new guest");
				if (m_ctx)
				{
					m_ctx->sessionToken.clear();
				}
				m_client->signInGuest(m_loginName);
			}
			else if (e.request == "rejoin" || e.request == "spectate")
			{
				m_runningId.clear();
				m_rejoinPending = false;
				restoreMode(); // lane UBERJOIN-1: a refused rejoin / watch after a mode switch
				if (m_connecting)
				{
					callPage("PopUpConnectingClose");
					m_connecting = false;
				}
				if (m_state == 13)
				{
					m_state = 1;
				}
				showBox(kRejoinTitle, wrapLines(playerMessage(e.request, e.code, e.text), kWrap), "rejoin-refused|" + narrow(e.text));
			}
			else if (e.request == "signin")
			{
				m_loginSent = false;
				m_loginButton = -1; // lane ONLINEUX-1: RW 0x97A668 after a refused sign-in (loginEnabled cleared, then the fields decide)
				applyLoginButton(true);
				showBox(kOnlineTitle, wrapLines(playerMessage(e.request, e.code, e.text), kWrap), "GUI:GSErrorTitle|" + narrow(e.text));
			}
			else if (e.request == "motd")
			{
				m_motdError = e.text;
				fillMotd();
			}
			else if (e.request == "create")
			{
				prune();
				if (m_gameName)
				{
					m_gameName->winEnable(true);
				}
				if (m_gamePassword)
				{
					m_gamePassword->winEnable(true);
				}
				m_state = 3;
				showBox(kOnlineTitle, wrapLines(playerMessage(e.request, e.code, e.text), kWrap), "GUI:GSErrorTitle|" + narrow(e.text));
			}
			else if (e.request == "join")
			{
				callPage("ClosePassword");
				m_state = 1;
				m_popUp = false;
				restoreMode(); // lane UBERJOIN-1
				showBox(text("GUI:JoinFailedDefault"), wrapLines(playerMessage(e.request, e.code, e.text), kWrap), "GUI:JoinFailedDefault|" + narrow(e.text));
			}
			else if (e.request == "start")
			{
				m_startSent = false;
				if (m_connecting)
				{
					callPage("PopUpConnectingClose");
					m_connecting = false;
				}
				callPage("EnableButtonPlayGame");
				if (e.code == "no_relay")
				{
					// docs/ONLINE.md section 5 step 5: a lobby without a relay refuses the START; the player is told plainly [S-4074]
					showBox(kNoRelayTitle, kNoRelay, "no_relay|" + narrow(kNoRelay));
				}
				else
				{
					showBox(kOnlineTitle, wrapLines(playerMessage(e.request, e.code, e.text), kWrap), "GUI:GSErrorTitle|" + narrow(e.text));
				}
			}
			else if (e.request == "list")
			{
				showBox(kOnlineTitle, wrapLines(playerMessage(e.request, e.code, e.text), kWrap), "GUI:GSErrorTitle|" + narrow(e.text));
			}
			else if (e.request == "poll")
			{
				// Sol r1: the staging room lost the lobby (it keeps asking every second): the player is told once, in the room's chat
				if (!m_pollTrouble && m_client->inGame())
				{
					m_pollTrouble = true;
					systemChat(u"The online lobby does not answer (still trying): " + playerMessage(e.request, e.code, e.text)); // lane ONLINEUX-1
				}
			}
			else if (e.request != "leave")
			{
				// a slot / setup / ready / chat / kick the lobby refused: a system line in the staging room
				systemChat(e.text);
			}
			break;
		}
		}
	}
}

void AptOnlineShell::update()
{
	prune();
	pumpClient();
	if (level() >= 0 && !isHidden())
	{
		m_shownOnce = true;
		if (m_barDue)
		{
			m_barDue = false;
			std::string err;
			if (!windows().invokeAS(level(), "ShowButtonBar", {}, nullptr, &err))
			{
				windows().note("movie-function-missing", "_root.ShowButtonBar(): " + err);
			}
			else
			{
				windows().note("online-button-bar", "the shell is back over a covering screen: the movie's ShowButtonBar [S-5140]");
			}
		}
		if (m_ctx && !m_ctx->pendingNotice.empty() && m_currentName != "OnlineLogin" && m_pages.count(m_currentName))
		{
			// lane ONLINEUX-1: an online game that could not go on (game.gd: a watched game the relay closed, a rejoin with nothing to catch up from)
			const std::u16string notice = m_ctx->pendingNotice;
			m_ctx->pendingNotice.clear();
			showBox(kOnlineTitle, wrapLines(notice, kWrap), "online-notice|" + narrow(notice));
		}
	}
	if (m_hub)
	{
		m_hub->update(); // lane ONLINEHUB-1
	}
	syncMode(); // lane UBERJOIN-1
	serviceReadyBoxes(); // lane ONLINE-5: RW 0x846BA4 (the setup panel's update: + 0x2B9)
	if (m_box)
	{
		m_box->update();
	}
	// BFME2 0x51805B: a page asked for goes to the movie's LoadScreen
	if (!m_pendingScreen.empty())
	{
		std::string err;
		if (level() < 0 || !windows().invokeAS(level(), "LoadScreen", { m_pendingScreen }, nullptr, &err))
		{
			windows().note("movie-function-missing", "_root.LoadScreen(" + m_pendingScreen + "): " + err);
		}
		m_pendingScreen.clear();
	}
	if (m_loginDisablesDue > 0 && m_currentName == "OnlineLogin" && m_nickname && !m_nickname->winIsHidden() && --m_loginDisablesDue == 0)
	{
		// OpenBFME (S-4071): the guest sign-in has no account: Create, Delete nickname, Register and Service terms stay disabled
		windows().note("online-login", "the account buttons are disabled (S-4071)");
		callChild("DisableButtonCreate");
		callChild("DisableButtonDeleteNickname");
		callChild("DisableButtonRegister");
		callChild("DisableButtonServiceTerms");
		applyLoginButton(true); // lane ONLINEUX-1: the page's fill (RW 0x97A668 runs after the cached names are in)
	}
	if (m_currentName == "OnlineLogin" && m_pages.count("OnlineLogin") && m_nickname && !m_nickname->winIsHidden())
	{
		// once the page shows its fields (its clips are placed: the same moment the account buttons' DisableButton* calls wait for)
		showNicknameOnly(); // lane MENUWORK-1 (S-4750)
	}
	if (m_pages.count("OnlineHome"))
	{
		fillTicker(); // lane ONLINEUX-1: RW 0x9C881D runs every update of the home page (the clock, the counts the host keeps)
	}
	if (!m_pages.count("OnlineOpenPlay"))
	{
		return;
	}
	if (m_closeConnections > 0 && --m_closeConnections == 0)
	{
		invokeRoot("StartCloseConnectionsScreen"); // lane ONLINE-5 (S-4550): the room's panels back (the page defines both on _root)
		invokeRoot("OnConnectionsScreenClosed");
	}
	switch (m_state)
	{
	case 0:
		// RW 0x9AFAD5: once GameSpy is connected ([0xDEBE6C] == 1, here: the lobby client) the room is left (RW 0x9AB748), which does its work only
		// once the GameList and Lobbies gadgets exist (+ 0x48C, + 0x490): ClosePassword, GotoAndPlay("_lobby"), ..., state 1
		prune();
		if (m_client && m_gameList && m_lobbies)
		{
			backToList();
		}
		break;
	case 1:
		// RW 0x9AFA99: the page's OnStartLobby, state 2 (the list)
		callPage("OnStartLobby");
		m_state = 2;
		m_listDirty = true;
		break;
	case 2:
	case 3:
		if (m_listDirty)
		{
			fillGamesList();
		}
		break;
	case 4:
	{
		// the create pop-up's accept (RW 0x9A8423 -> state 4): the lobby game with the player's name and the password
		prune();
		std::u16string name = m_gameName ? trim(GadgetTextEntryGetText(m_gameName)) : std::u16string();
		if (name.empty() && m_client)
		{
			name = m_client->accountName();
		}
		const std::string password = m_gamePassword ? narrow(GadgetTextEntryGetText(m_gamePassword)) : std::string();
		SkirmishGameSetup *model = setupModel();
		if (!m_client || !model)
		{
			windows().note("online-create-failed", "no lobby client or no setup model");
			m_state = 2;
			break;
		}
		// the host's slot 0, every other slot open (as the LAN lobby's host start)
		SkirmishGameInfo info = model->info();
		if (!m_ownMap.empty())
		{
			info.mapName = m_ownMap; // lane ONLINEUX-1: the player's own map, not the last game the browser showed
			m_ownMap.clear();
			m_selectedMap.clear();
		}
		info.slots[0].state = SLOT_PLAYER;
		for (int s = 1; s < MAX_SLOTS; ++s)
		{
			info.slots[s] = SkirmishGameSlot();
			info.slots[s].state = SLOT_OPEN;
		}
		m_client->createGame(name, password, info, 0);
		m_state = 5;
		break;
	}
	case 7:
		startPressed();
		m_state = m_client && m_client->amIHost() ? 6 : 12;
		break;
	case 9:
		joinSelected();
		break;
	default:
		break;
	}
}

// ---- the staging room (MpGameSetup on the lobby game) -----------------------------------------------------------------------------------------------------

WindowMsgHandledType AptOnlineShell::gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData data1, WindowMsgData data2)
{
	prune();
	GameWindow *gadget = reinterpret_cast<GameWindow *>(data1);
	if (m_hub && m_hub->gadgetMessage(gadget, msg, data1, data2))
	{
		return MSG_HANDLED; // lane ONLINEHUB-1: the tournaments panel's lists, the STATS page's filters
	}
	if (gadget && gadget == m_gameList)
	{
		if (msg == GLM_SELECTED || msg == GLM_DOUBLE_CLICKED)
		{
			showSelectedGameMap(); // lane ONLINEUX-1
		}
		if (msg == GLM_DOUBLE_CLICKED && m_state == 2)
		{
			if ((int)(std::intptr_t)data2 >= 0)
			{
				GadgetListBoxSetSelected(m_gameList, (int)(std::intptr_t)data2);
			}
			m_state = 9; // as JoinGame
		}
		return MSG_HANDLED;
	}
	if (gadget && gadget == m_nickname && (msg == GCM_UPDATE_TEXT || msg == GCM_SELECTED))
	{
		applyLoginButton(false); // lane ONLINEUX-1: BFME2 0x57166C (0x402E / 0x4026 on the nickname): RW 0x97A668
		return MSG_HANDLED;
	}
	if (gadget && (gadget == m_nickname || gadget == m_motd || gadget == m_lobbies || gadget == m_gameInfo || gadget == m_gameName || gadget == m_gamePassword))
	{
		return MSG_HANDLED;
	}
	return AptSkirmish::gadgetMessage(from, msg, data1, data2);
}

void AptOnlineShell::lobbyChanged()
{
	// the gadgets changed the model: the differences go to the lobby (the host's setup and the slots that are not players', a player's own slot)
	SkirmishGameSetup *model = setupModel();
	if (!m_client || !model || !m_client->inGame())
	{
		return;
	}
	const SkirmishGameInfo &now = model->info();
	const SkirmishGameInfo &was = m_client->gameInfo();
	if (m_client->amIHost() && (now.mapName != was.mapName || now.startingCash != was.startingCash || now.superweaponRestriction != was.superweaponRestriction ||
								   now.legendaryHeroes != was.legendaryHeroes || now.peaceTimeMinutes != was.peaceTimeMinutes || now.rules != was.rules))
	{
		m_client->setSetup(now);
	}
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		const SkirmishGameSlot &a = now.slots[i];
		const SkirmishGameSlot &b = was.slots[i];
		const bool changed = a.state != b.state || a.playerTemplate != b.playerTemplate || a.color != b.color || a.startPos != b.startPos ||
			a.teamNumber != b.teamNumber;
		if (!changed)
		{
			continue;
		}
		if (i == m_client->localSlot())
		{
			m_client->setOwnSlot(a.playerTemplate, a.color, a.startPos, a.teamNumber);
		}
		else if (m_client->amIHost() && !b.isHuman())
		{
			if (a.state == SLOT_PLAYER)
			{
				continue; // a player's place is taken by a join, never set
			}
			m_client->setOtherSlot(i, a.state, a.playerTemplate, a.color, a.startPos, a.teamNumber);
		}
	}
}

bool AptOnlineShell::lobbyEditable(int slot) const
{
	if (!m_client || !m_client->inGame())
	{
		return false;
	}
	if (slot < 0)
	{
		return m_client->amIHost();
	}
	if (slot == m_client->localSlot())
	{
		return true;
	}
	return m_client->amIHost() && !m_client->gameInfo().slots[slot].isHuman();
}

void AptOnlineShell::onReadyPress(const std::string &argument)
{
	// MpGameSetup::OnReadyPress (RW 0x841590 region; BFME2 0x43EFD3, decomp MpGameSetupSlots.cpp AptMpGameSetup::OnReadyPress): without an argument the
	// movie's Ready boxes were just made and get their states; with one, the slot it names (1 .. 7: the row of the box pressed) asks to toggle its ready
	// state and the answer goes to the owner (BFME2 0x5A1DCE MpOwnerOnReadyChecked: a joined player's READY / UNREADY; the host has none). Only the local
	// player's own box takes a press in the movie (RW 0x84211D: the others are "_disabled..."); a press naming another slot is refused by name.
	// NOT PORTED: the clan game's checks before a ready (rules mode 1: CLAN:ErrorMissingClanAffiliation / CLAN:ErrorOnlyTwoClans): no clans here
	if (argument.empty() || argument == "undefined")
	{
		readyBoxesMade();
		return;
	}
	if (!m_client || !m_client->inGame())
	{
		return;
	}
	const int local = m_client->localSlot();
	int index = local;
	if (argument != "PlayGame") // startPressed: a joiner's Play Game command is its own Ready
	{
		char *end = nullptr;
		const long v = std::strtol(argument.c_str(), &end, 10);
		if (end == argument.c_str() || *end != '\0' || v <= 0 || v >= MAX_SLOTS)
		{
			windows().note("online-ready-refused", "MpGameSetup::OnReadyPress(" + argument + "): not a player slot (1 .. 7)");
			return;
		}
		index = (int)v;
	}
	if (index != local || m_client->amIHost())
	{
		windows().note("online-ready-refused", "MpGameSetup::OnReadyPress(" + argument + "): only the local player's own Ready box (slot " + std::to_string(local) +
													 ") takes a press, and the host has none");
		updateReadyIcon(index, true); // the box pressed goes back to what the lobby says
		return;
	}
	m_ready = !m_client->slotViews()[(size_t)local].ready;
	m_client->setReady(m_ready);
}

bool AptOnlineShell::applySlotHero(int slot, const CreateAHeroHero *hero)
{
	(void)slot;
	if (hero)
	{
		windows().note("create-a-hero-lobby", "a Create-a-Hero in an online game: the lobby service does not carry the record yet [S-4076]");
	}
	return false;
}

void AptOnlineShell::onKickPlayer(const std::string &argument)
{
	if (!m_client || !m_client->amIHost() || argument.empty())
	{
		return;
	}
	m_client->kick(std::atoi(argument.c_str()));
}

void AptOnlineShell::onChatSend(const std::u16string &line)
{
	if (!m_client || !m_client->inGame())
	{
		windows().note("command-ignored", "AptMpChat::Send outside an online game");
		return;
	}
	m_client->chat(line);
}

bool AptOnlineShell::showChat() const
{
	return m_client && m_client->inGame();
}

void AptOnlineShell::ruleNotice(const std::u16string &line)
{
	if (m_client && m_client->inGame())
	{
		m_client->chat(line);
	}
	else
	{
		addChatLine(line, 0xFFFFFFFFu);
	}
}

void AptOnlineShell::showRuleWarning(const std::u16string &title, const std::u16string &body)
{
	showBox(title, body, narrow(title) + "|" + narrow(body));
}
