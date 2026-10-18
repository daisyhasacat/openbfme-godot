// OpenBFME. GPL-3.0.
//
// PlatformApiClient (lane ONLINEHUB-1): the client of the OpenBFME online platform API v1 (Quick Match, tournaments, statistics, game results), the
// persistent services beside the lobby (the contract: docs/PLATFORM_API.md; the service is the backend's own, copied for the tests as tools/platform_fake,
// source commit 192a0fca: the client's tests run against it, so the wire shapes cannot drift unseen). NOT RETAIL: RotWK's Quick
// Match ("Tournament") and statistics pages ran on GameSpy's matchmaking and persistent storage, which are gone; this client is what the ported pages
// (GameClient/GUI/AptScreens/AptOnlineHub.h) are fed from in their place.
//
// The client does no I/O, as OnlineLobbyClient: it queues HTTP requests (GET / POST / DELETE with a JSON body), the host sends them (the Godot game:
// scripts/online_http.gd pointed at the platform's base URL; the tests: a plain HTTP/1.1 client against tools/platform_fake/fake_server.py) and hands
// every answer back with deliver(). One request is in flight at a time, in the order they were made.
//
// Every answer is checked: a transport error, an answer that is not version 1, a refusal (the service's error code and message) or a field out of range
// reaches an event (never a silent default) and leaves the state as it was. Every answer is bounded: its body (kMaxBodyBytes), its strings and its lists
// (the k* limits below) are checked before anything is kept; enumerations (states, modes, sizes, periods) are closed sets, anything else is refused by
// name. There are no credentials: the identity is the install id (128 random bits, 32 hex digits) and the nickname, as platform-api.md decides ("no
// accounts yet"); the install id is not secret, but it is never written to a log by this client (installTag() is what logs show).
//
// Not simulation code: nothing here reaches the game's logic. A Quick Match ends in an ordinary private lobby room (OnlineLobbyClient), whose START is
// the game's input as any other online game's.

#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

class JsonValue;

class PlatformApiClient
{
public:
	// the bounds of an answer (Hostile input rule 1: checked before anything is kept)
	static constexpr std::size_t kMaxBodyBytes = 128 * 1024;
	static constexpr std::size_t kMaxNickBytes = 24;       ///< a nickname this client sends (the lobby's guest names)
	static constexpr std::size_t kMaxReceivedNickBytes = 64; ///< a nickname another client sent (the service's own limit)
	static constexpr std::size_t kMaxIdBytes = 128;        ///< a game id of a result (the service: 1 .. 128 characters)
	static constexpr std::size_t kMaxRoomCodeBytes = 32;   ///< a room code: [A-Za-z0-9_-]{6,32} (lobby.mjs ROOM_CODE)
	static constexpr std::size_t kMaxNameBytes = 512;      ///< a tournament's name (the service: 128 characters)
	static constexpr std::size_t kMaxRulesBytes = 16384;   ///< a tournament's rules text (the service: 4096 characters)
	static constexpr std::size_t kMaxFactionBytes = 64;    ///< a faction this client sends (a PlayerTemplate name)
	static constexpr std::size_t kMaxLabelBytes = 512;     ///< a faction or map label the service shows (128 NFC characters)
	static constexpr std::size_t kMaxMapBytes = 200;       ///< a map this client sends (the lobby's map key)
	static constexpr std::size_t kMaxMessageBytes = 512;   ///< a refusal's text
	static constexpr std::size_t kMaxVersionBytes = 64;    ///< the client's version tag (the service: 1 .. 64 characters, kept exactly)
	static constexpr std::size_t kMaxTournaments = 100;    ///< one page of the list (the service's page size)
	static constexpr std::size_t kMaxTournamentPages = 5;  ///< pages read for the list (500 tournaments)
	static constexpr std::size_t kMaxRounds = 8;           ///< a bracket of 128 players has 7 rounds
	static constexpr std::size_t kMaxMatches = 255;        ///< a bracket of 128 players has 127 matches (the cap leaves a round of room)
	static constexpr std::size_t kMaxMatchPlayers = 4;     ///< a Quick Match 2v2 has 4 players; a tournament match (1v1) 2
	static constexpr std::size_t kMaxFactions = 32;        ///< a statistics list: the service shows 20 and "other"; 32 is the client's cap
	static constexpr std::size_t kMaxMatchups = 32;
	static constexpr std::size_t kMaxMaps = 32;
	static constexpr int kMaxPollAfterS = 60;               ///< the longest wait between ticket polls the service may ask for
	static constexpr std::int64_t kMaxCount = 1000000000;  ///< a game count

	struct Options
	{
		std::string install;                ///< the install id (32 lowercase hex digits; required: requests are refused without one)
		std::string nick;                   ///< the player's nickname (UTF-8, at most kMaxNickBytes)
		std::string clientPlatform;         ///< "windows" / "linux" / "macos": every write names it (the service refuses a write without client metadata)
		std::string clientVersion;          ///< the game's version tag, 1 .. 64 characters, sent exactly
		std::uint32_t startTimeoutMs = 60 * 1000; ///< the ticket must be made (or refused) within this long of the search starting, else the search fails
		std::uint32_t searchTimeoutMs = 10 * 60 * 1000; ///< a Quick Match search ends after this long without a match (the player is told)
		int pollFailuresAllowed = 3;        ///< ticket polls that may fail in a row before the search ends ("services unavailable")
		std::uint32_t resultRetryMs = 60 * 1000;        ///< a result that could not be sent is sent once more after this long
		bool shareResults = true;           ///< the Options row "Share my game results for statistics": off, no result is queued, retried or sent
	};
	struct HttpRequest
	{
		int id = 0;
		std::string method; ///< "GET" / "POST" / "DELETE"
		std::string path;   ///< "/results", "/qm/tickets" ... (the host prefixes the platform's base URL, which names the version: .../v1)
		std::string body;   ///< JSON ("" for a GET)
	};
	struct Player
	{
		std::string nick;
		int team = -1;
		bool you = false;
	};
	struct Match
	{
		std::string id;
		std::string roomCode;
		std::string hostInstall; ///< Quick Match: the install that creates the room (the first player listed)
		std::string mode;        ///< "faithful" / "uber"
		std::vector<Player> players;
		bool host = false;       ///< this install creates the room
	};
	enum class QmState
	{
		Idle,       ///< no search
		Starting,   ///< the ticket is being made
		Searching,  ///< the ticket is polled
		Matched,    ///< match() holds the match
		Expired,    ///< the service let the ticket expire
		Cancelled,  ///< the player (or the service) cancelled it
		TimedOut,   ///< no match within Options::searchTimeoutMs (the ticket is cancelled)
		Failed      ///< the service refused or could not be reached: qmProblem() says why
	};
	struct Tournament
	{
		std::string id;    ///< 32 lowercase hex digits
		std::string name;
		std::string mode;  ///< "faithful" / "uber"
		std::string size;  ///< "1v1": v1 of the service has no other size (anything else is refused)
		std::string state; ///< "upcoming" / "running" / "finished" / "cancelled"
		std::int64_t start = 0;
		int maxPlayers = 0;
		int registered = 0;
	};
	// a match of a bracket as the service lists it (docs/PLATFORM_API.md): `players` are the two nicknames (empty: not known yet), `roomCode` is there only for
	// a match the asking install plays in
	struct BracketMatch
	{
		std::string id;       ///< <tournament id>.<24 hex digits>: also the game id its result carries
		int round = 0;
		std::string state;    ///< "waiting" / "open" / "ready" / "bye" / "finished" / "admin_resolved" / "no_show" / "disputed"
		std::array<std::string, 2> players;
		std::string winner;   ///< the winner's nick ("" none yet)
		std::string roomCode; ///< "" unless this install plays the match
		std::int64_t openedAt = 0;
		bool playable() const { return (state == "open" || state == "ready") && !roomCode.empty() && !players[0].empty() && !players[1].empty(); }
	};
	struct TournamentDetail
	{
		Tournament info;
		std::string rules;
		std::string reason; ///< why a cancelled tournament was ("" none)
		std::vector<std::vector<std::string>> rounds; ///< the match ids of each round
		std::vector<BracketMatch> matches;            ///< every match, in the service's order (round, index)
		// the first match this install can play now (open or ready, with its room code and both players), null when none
		const BracketMatch *nextMatch() const;
	};
	struct FactionStat
	{
		std::string faction;
		std::int64_t picks = 0, wins = 0;
		double pickRate = 0.0, winRate = 0.0; ///< 0 .. 1
		bool overflow = false;                ///< the service's "other" row (everything past its 20 named entries): never an army of its own
	};
	struct Matchup
	{
		std::string a, b;
		std::int64_t games = 0, aWins = 0;
		bool overflow = false;
	};
	struct MapStat
	{
		std::string map;
		std::int64_t games = 0;
		bool overflow = false;
	};
	struct StatsBucket
	{
		std::int64_t games = 0;
		std::vector<FactionStat> factions;
		std::vector<Matchup> matchups;
		std::vector<MapStat> maps;
		double avgDurationS = 0.0;
	};
	struct GlobalStats
	{
		bool known = false;
		std::string mode, period; ///< what was asked ("" = every mode)
		std::int64_t asOf = 0;
		StatsBucket humans;       ///< games of two or more human players whose reports agree
		StatsBucket vsAi;         ///< games of one human against the computer
	};
	struct Event
	{
		enum Kind
		{
			Health,          ///< the service answered /v1/health (flag = it is up)
			QmChanged,       ///< qmState() changed (text = why, for Expired / Cancelled / TimedOut / Failed)
			Tournaments,     ///< tournaments() changed
			TournamentInfo,  ///< tournament() changed
			Registered,      ///< the registration of tournament `id` was taken (flag) or withdrawn (!flag)
			CheckedIn,       ///< the check-in of match `id` was taken (text = the match's state now)
			GlobalStatsReady,///< globalStats() changed
			ResultSent,      ///< the result of game `id` was taken by the service
			ResultDropped,   ///< the result of game `id` could not be sent (after its retry, refused, or the player stopped sharing: text says why)
			Refused          ///< a request was refused or failed: request = what was asked, code = the service's code (or "transport", "protocol")
		} kind;
		std::string request, code, id, text;
		bool flag = false;
	};
	// a finished game's report (POST /v1/results): built by GameRecords (GameClient/GameRecords.h) from the score screen and the game's setup
	struct ResultReport
	{
		std::string gameId;
		std::string json; ///< the whole body
	};

	explicit PlatformApiClient(const Options &options);

	// the install id's check (32 lowercase hex digits), and the short tag the logs name it by (its first 6 digits)
	static bool validInstall(const std::string &install);
	static std::string installTag(const std::string &install);
	// a room code as the lobby takes it ([A-Za-z0-9_-]{6,32})
	static bool validRoomCode(const std::string &code);
	// the service's id shapes: a ticket and a Quick Match's id (32 lowercase hex digits), a tournament's (32), a tournament match's (<32 hex>.<24 hex>), a
	// result's game id (1 .. 128 of A-Z a-z 0-9 _ . -)
	static bool validTicketId(const std::string &id);
	static bool validTournamentId(const std::string &id);
	static bool validMatchId(const std::string &id);
	static bool validGameId(const std::string &id);
	// the client metadata every write carries; false + *error when the platform is not windows / linux / macos or the version is not 1 .. 64 characters
	static bool validClient(const std::string &platform, const std::string &version, std::string *error = nullptr);
	static bool hasClient(const Options &o) { return validClient(o.clientPlatform, o.clientVersion); }

	// ---- requests ----
	void checkHealth();                                          // GET /v1/health
	// Quick Match: a ticket (POST /v1/qm/tickets) for `mode` ("faithful" / "uber"), `size` ("1v1" / "2v2") and the factions allowed (PlayerTemplate
	// names, or "random"); false + *error when the arguments are refused (nothing is sent) or a search is already running
	bool qmSearch(const std::string &mode, const std::string &size, const std::vector<std::string> &factions, std::string *error);
	void qmCancel();                                             // DELETE /v1/qm/tickets/<ticket>; the state is Cancelled at once
	void qmReset();                                              // back to Idle after a match was used or a search ended
	void listTournaments();                                      // GET /v1/tournaments
	void fetchTournament(const std::string &id);                 // GET /v1/tournaments/<id>?install=<install>
	void registerTournament(const std::string &id);              // POST /v1/tournaments/<id>/register
	void withdrawTournament(const std::string &id);              // DELETE /v1/tournaments/<id>/register
	// POST /v1/tournaments/<id>/matches/<match>/checkin: the player is in the match's room (the room screen); refused after 15 minutes, for a stranger
	void checkIn(const std::string &tournamentId, const std::string &matchId);
	// GET /v1/stats/global?mode=&period= ; mode "" / "faithful" / "uber", period "7d" / "30d" / "all"; false + *error for a value outside them
	bool fetchGlobalStats(const std::string &mode, const std::string &period, std::string *error);
	// POST /v1/results (sent in the background; retried once after Options::resultRetryMs when the service could not be reached or failed)
	void sendResult(const ResultReport &report);
	// the Options row: off, every result still queued or waiting for its retry is dropped (ResultDropped "the player does not share game results") and
	// none is sent until it is on again; a request already on the wire cannot be recalled
	void setShareResults(bool share);

	// moves the client on: the ticket polls, the search timeout, the result retry
	void update(std::uint64_t nowMs);
	std::vector<HttpRequest> takeRequests();
	void deliver(int id, int status, const std::string &body, const std::string &transportError);
	// every event goes to two readers: takeEvents() (the host program: the results' outcome, the log) and takeUiEvents() (the menus up now: the last
	// kMaxUiEvents only)
	std::vector<Event> takeEvents();
	std::vector<Event> takeUiEvents();
	static constexpr std::size_t kMaxUiEvents = 64;
	static constexpr std::size_t kMaxEvents = 256;       ///< events kept for takeEvents() while nobody reads them (the oldest go)
	static constexpr std::size_t kMaxResultsKept = 256;  ///< game ids remembered as sent; also the results waiting for their retry or their turn
	static constexpr std::size_t kMaxQueue = 64;         ///< requests waiting for their turn (the client refuses more by name)

	// ---- state ----
	const Options &options() const { return m_options; }
	void setNick(const std::string &nick) { m_options.nick = nick; }
	int healthKnown() const { return m_health; } ///< -1 not asked / no answer yet, 0 unavailable, 1 up
	QmState qmState() const { return m_qm; }
	static const char *qmStateName(QmState s);
	const std::string &qmProblem() const { return m_qmProblem; }
	const std::string &qmTicket() const { return m_ticket; }
	int qmPollAfterS() const { return (int)(m_pollAfterMs / 1000); } ///< the interval the service asked for (kept across polls that name none)
	const std::string &qmMode() const { return m_qmMode; }
	const std::string &qmSize() const { return m_qmSize; }
	std::uint64_t qmSearchStartedMs() const { return m_qmStarted; }
	const Match &match() const { return m_match; }
	const std::vector<Tournament> &tournaments() const { return m_tournaments; }
	bool tournamentsKnown() const { return m_tournamentsKnown; }
	bool tournamentsTruncated() const { return m_tournamentsTruncated; } ///< the list has more pages than kMaxTournamentPages (the rest is not read)
	bool registeredIn(const std::string &tournamentId) const { return m_registered.count(tournamentId) != 0; } ///< taken or kept by an answer of this session
	const TournamentDetail &tournament() const { return m_tournament; }
	bool tournamentKnown() const { return m_tournamentKnown; }
	const GlobalStats &globalStats() const { return m_global; }
	bool busy() const { return m_inFlight != 0 || !m_queue.empty(); }
	int pendingResults() const { return (int)m_retries.size(); }
	int sentCount(const std::string &kind) const
	{
		auto it = m_sent.find(kind);
		return it == m_sent.end() ? 0 : it->second;
	}

	// the parsers (also the tests'): false + *error names the field that was refused; `out` is untouched then
	// one page of the list; *nextCursor is the cursor of the next page ("" when this is the last)
	static bool parseTournaments(const JsonValue &root, std::vector<Tournament> &out, std::string *nextCursor, std::string *error);
	static bool parseTournament(const JsonValue &root, TournamentDetail &out, std::string *error);
	static bool parseGlobalStats(const JsonValue &root, GlobalStats &out, std::string *error);
	static bool parseTicketState(const JsonValue &root, std::string &state, Match &match, std::string *error);
	static std::string jsonQuote(const std::string &utf8);

private:
	struct Pending
	{
		std::string kind; ///< "health", "qm-create", "qm-poll", "qm-cancel", "tournaments", "tournament", "register", "withdraw", "global", "result"
		std::string ref;  ///< the tournament / game id it is about
		HttpRequest request;
	};
	// false (and a Refused event) when kMaxQueue requests wait already; `front`: before everything that waits
	bool queue(const std::string &kind, const std::string &ref, const std::string &method, const std::string &path, const std::string &body, bool front = false);
	std::string clientJson() const; ///< "client":{"platform":..,"version":..}
	void refuse(const Pending &p, const std::string &code, const std::string &message);
	void setQm(QmState s, const std::string &why);
	void handle(const Pending &p, const JsonValue &root);
	void resultFailed(const Pending &p, const std::string &why, bool retry);
	void push(const Event &e);

	Options m_options;
	int m_health = -1;
	// Quick Match
	QmState m_qm = QmState::Idle;
	std::string m_qmProblem, m_ticket, m_qmMode, m_qmSize;
	std::uint64_t m_qmStarted = 0, m_nextPoll = 0;
	bool m_pollQueued = false;
	int m_pollFailures = 0;
	std::uint64_t m_qmGen = 0;            ///< the number of the search being made: a ticket that comes back for an older one is not this search's (it is deleted)
	std::uint64_t m_pollAfterMs = 2000;   ///< the interval the service named with the ticket; polls that name none keep it
	Match m_match;
	// tournaments and statistics
	std::vector<Tournament> m_tournaments;
	std::vector<Tournament> m_pageSoFar;  ///< the list's earlier pages while the next is out (committed whole, or not at all)
	std::size_t m_pages = 0;
	std::set<std::string> m_registered;   ///< tournaments this session registered in (the answers' `registered`: the public detail names no registrants)
	bool m_tournamentsKnown = false, m_tournamentsTruncated = false;
	TournamentDetail m_tournament;
	bool m_tournamentKnown = false;
	GlobalStats m_global;
	// results: the bodies to send again (game id -> (when, body)); a result is retried once
	struct Retry
	{
		std::string gameId, body;
		std::uint64_t dueMs = 0;
	};
	std::vector<Retry> m_retries;
	std::map<std::string, int> m_resultAttempts; ///< game id -> attempts made (a game's result is sent at most twice, ever); the newest kMaxResultsKept
	std::deque<std::string> m_resultOrder;       ///< the order they came in (the oldest forgotten past the cap)
	// the requests
	std::uint64_t m_now = 0;
	int m_nextId = 1;
	int m_inFlight = 0;
	Pending m_current;
	std::deque<Pending> m_queue;
	std::vector<Event> m_events;
	std::deque<Event> m_uiEvents;
	std::map<std::string, int> m_sent;
};
