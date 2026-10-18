// OpenBFME. GPL-3.0.
// See GameNetwork/PlatformApiClient.h (lane ONLINEHUB-1; the contract: docs/PLATFORM_API.md, pinned against the backend's own service in tools/platform_fake).

#include "GameNetwork/PlatformApiClient.h"

#include "Common/MiniJson.h"
#include "GameNetwork/OnlineLobbyClient.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace
{
bool idChar(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

bool idText(const std::string &id, std::size_t minBytes, std::size_t maxBytes)
{
	if (id.size() < minBytes || id.size() > maxBytes)
	{
		return false;
	}
	for (char c : id)
	{
		if (!idChar(c))
		{
			return false;
		}
	}
	return true;
}

bool lowerHex(const std::string &s, std::size_t count)
{
	if (s.size() != count)
	{
		return false;
	}
	for (char c : s)
	{
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
		{
			return false;
		}
	}
	return true;
}

// a text the service sends back for display: within its bound, no control characters (a nick, a name, a map)
bool validText(const std::string &s, std::size_t maxBytes, bool allowEmpty = false)
{
	if ((!allowEmpty && s.empty()) || s.size() > maxBytes)
	{
		return false;
	}
	for (unsigned char c : s)
	{
		if (c < 0x20 || c == 0x7F)
		{
			return false;
		}
	}
	return true;
}

// the rules text may break lines
bool validLongText(const std::string &s, std::size_t maxBytes)
{
	if (s.size() > maxBytes)
	{
		return false;
	}
	for (unsigned char c : s)
	{
		if ((c < 0x20 && c != '\n') || c == 0x7F)
		{
			return false;
		}
	}
	return true;
}

bool oneOf(const std::string &s, std::initializer_list<const char *> set)
{
	for (const char *x : set)
	{
		if (s == x)
		{
			return true;
		}
	}
	return false;
}

std::string quoted(const std::string &s, std::size_t shown = 24)
{
	return "\"" + s.substr(0, shown) + (s.size() > shown ? "..." : "") + "\"";
}

struct Reader
{
	std::string *error;
	bool fail(const std::string &why)
	{
		if (error)
		{
			*error = why;
		}
		return false;
	}
	bool str(const JsonValue &o, const char *key, std::string &out, const std::string &where)
	{
		const JsonValue *v = o.get(key);
		if (!v || !v->isString())
		{
			return fail(where + key + " is missing or not a string");
		}
		out = v->string;
		return true;
	}
	// a string, or null / absent ("")
	bool optStr(const JsonValue &o, const char *key, std::string &out, const std::string &where)
	{
		const JsonValue *v = o.get(key);
		if (!v || v->type == JsonValue::NUL)
		{
			out.clear();
			return true;
		}
		if (!v->isString())
		{
			return fail(where + key + " is not a string");
		}
		out = v->string;
		return true;
	}
	bool integer(const JsonValue &o, const char *key, std::int64_t lo, std::int64_t hi, std::int64_t &out, const std::string &where)
	{
		const JsonValue *v = o.get(key);
		if (!v || !v->isNumber() || std::floor(v->number) != v->number || v->number < (double)lo || v->number > (double)hi)
		{
			return fail(where + key + " is missing or not a whole number in " + std::to_string(lo) + " .. " + std::to_string(hi));
		}
		out = (std::int64_t)v->number;
		return true;
	}
	bool fraction(const JsonValue &o, const char *key, double &out, const std::string &where)
	{
		const JsonValue *v = o.get(key);
		if (!v || !v->isNumber() || !(v->number >= 0.0 && v->number <= 1.0))
		{
			return fail(where + key + " is missing or not a fraction in 0 .. 1");
		}
		out = v->number;
		return true;
	}
	bool optBool(const JsonValue &o, const char *key, bool &out, const std::string &where)
	{
		const JsonValue *v = o.get(key);
		if (!v || v->type == JsonValue::NUL)
		{
			out = false;
			return true;
		}
		if (v->type != JsonValue::BOOL)
		{
			return fail(where + key + " is not true / false");
		}
		out = v->boolean;
		return true;
	}
	const JsonValue *array(const JsonValue &o, const char *key, std::size_t maxItems, const std::string &where)
	{
		const JsonValue *v = o.get(key);
		if (!v || !v->isArray())
		{
			fail(where + key + " is missing or not a list");
			return nullptr;
		}
		if (v->array.size() > maxItems)
		{
			fail(where + key + " has " + std::to_string(v->array.size()) + " entries (at most " + std::to_string(maxItems) + ")");
			return nullptr;
		}
		return v;
	}
};

bool readTournament(Reader &r, const JsonValue &t, PlatformApiClient::Tournament &out, const std::string &where)
{
	if (!t.isObject())
	{
		return r.fail(where + "is not an object");
	}
	std::int64_t start = 0, maxPlayers = 0, registered = 0;
	if (!r.str(t, "id", out.id, where) || !r.str(t, "name", out.name, where) || !r.str(t, "mode", out.mode, where) || !r.str(t, "size", out.size, where)
		|| !r.str(t, "state", out.state, where) || !r.integer(t, "start", 0, (std::int64_t)1 << 40, start, where)
		|| !r.integer(t, "max_players", 2, 128, maxPlayers, where) || !r.integer(t, "registered", 0, 128, registered, where))
	{
		return false;
	}
	if (!PlatformApiClient::validTournamentId(out.id))
	{
		return r.fail(where + "id " + quoted(out.id) + " is not a tournament id (32 lowercase hex digits)");
	}
	if (!validText(out.name, PlatformApiClient::kMaxNameBytes))
	{
		return r.fail(where + "name is empty, longer than 512 bytes or has control characters");
	}
	if (!oneOf(out.mode, { "faithful", "uber" }))
	{
		return r.fail(where + "mode " + quoted(out.mode, 16) + " is not faithful / uber");
	}
	if (out.size != "1v1")
	{
		return r.fail(where + "size " + quoted(out.size, 16) + " is not 1v1 (platform v1 has no other tournament size)");
	}
	if (!oneOf(out.state, { "upcoming", "running", "finished", "cancelled" }))
	{
		return r.fail(where + "state " + quoted(out.state, 16) + " is not upcoming / running / finished / cancelled");
	}
	if (registered > maxPlayers)
	{
		return r.fail(where + "registered " + std::to_string(registered) + " is above max_players " + std::to_string(maxPlayers));
	}
	out.start = start;
	out.maxPlayers = (int)maxPlayers;
	out.registered = (int)registered;
	return true;
}

// a Quick Match's players: [{nick, team 0 .. 7}]
bool readMatchPlayers(Reader &r, const JsonValue &o, std::vector<PlatformApiClient::Player> &out, const std::string &where)
{
	const JsonValue *list = r.array(o, "players", PlatformApiClient::kMaxMatchPlayers, where);
	if (!list)
	{
		return false;
	}
	out.clear();
	for (std::size_t i = 0; i < list->array.size(); ++i)
	{
		const JsonValue &p = list->array[i];
		const std::string at = where + "players[" + std::to_string(i) + "].";
		if (!p.isObject())
		{
			return r.fail(at + " is not an object");
		}
		PlatformApiClient::Player pl;
		std::int64_t team = 0;
		if (!r.str(p, "nick", pl.nick, at) || !r.integer(p, "team", 0, 7, team, at))
		{
			return false;
		}
		if (!validText(pl.nick, PlatformApiClient::kMaxReceivedNickBytes))
		{
			return r.fail(at + "nick is empty, longer than 64 bytes or has control characters");
		}
		pl.team = (int)team;
		out.push_back(pl);
	}
	return true;
}

bool readBucket(Reader &r, const JsonValue &o, PlatformApiClient::StatsBucket &out, const std::string &where)
{
	if (!o.isObject())
	{
		return r.fail(where + "is not an object");
	}
	PlatformApiClient::StatsBucket b;
	if (!r.integer(o, "games", 0, PlatformApiClient::kMaxCount, b.games, where))
	{
		return false;
	}
	const JsonValue *v = o.get("avg_duration_s");
	if (!v || !v->isNumber() || !(v->number >= 0.0 && v->number <= 7.0 * 24 * 3600))
	{
		return r.fail(where + "avg_duration_s is missing or not a duration in 0 .. 604800 seconds");
	}
	b.avgDurationS = v->number;
	const JsonValue *factions = r.array(o, "factions", PlatformApiClient::kMaxFactions, where);
	const JsonValue *matchups = factions ? r.array(o, "matchups", PlatformApiClient::kMaxMatchups, where) : nullptr;
	const JsonValue *maps = matchups ? r.array(o, "maps", PlatformApiClient::kMaxMaps, where) : nullptr;
	if (!maps)
	{
		return false;
	}
	for (std::size_t i = 0; i < factions->array.size(); ++i)
	{
		const JsonValue &f = factions->array[i];
		const std::string at = where + "factions[" + std::to_string(i) + "].";
		PlatformApiClient::FactionStat s;
		if (!f.isObject())
		{
			return r.fail(at + " is not an object");
		}
		if (!r.str(f, "faction", s.faction, at) || !r.integer(f, "picks", 0, PlatformApiClient::kMaxCount, s.picks, at)
			|| !r.integer(f, "wins", 0, PlatformApiClient::kMaxCount, s.wins, at) || !r.fraction(f, "pick_rate", s.pickRate, at)
			|| !r.fraction(f, "win_rate", s.winRate, at) || !r.optBool(f, "overflow", s.overflow, at))
		{
			return false;
		}
		if (!validText(s.faction, PlatformApiClient::kMaxLabelBytes))
		{
			return r.fail(at + "faction is empty, too long or has control characters");
		}
		if (s.wins > s.picks)
		{
			return r.fail(at + "wins is above picks");
		}
		b.factions.push_back(s);
	}
	for (std::size_t i = 0; i < matchups->array.size(); ++i)
	{
		const JsonValue &m = matchups->array[i];
		const std::string at = where + "matchups[" + std::to_string(i) + "].";
		PlatformApiClient::Matchup s;
		if (!m.isObject())
		{
			return r.fail(at + " is not an object");
		}
		if (!r.str(m, "a", s.a, at) || !r.str(m, "b", s.b, at) || !r.integer(m, "games", 0, PlatformApiClient::kMaxCount, s.games, at)
			|| !r.integer(m, "a_wins", 0, PlatformApiClient::kMaxCount, s.aWins, at) || !r.optBool(m, "overflow", s.overflow, at))
		{
			return false;
		}
		if (!validText(s.a, PlatformApiClient::kMaxLabelBytes) || !validText(s.b, PlatformApiClient::kMaxLabelBytes))
		{
			return r.fail(at + "a / b is empty, too long or has control characters");
		}
		if (s.aWins > s.games)
		{
			return r.fail(at + "a_wins is above games");
		}
		b.matchups.push_back(s);
	}
	for (std::size_t i = 0; i < maps->array.size(); ++i)
	{
		const JsonValue &m = maps->array[i];
		const std::string at = where + "maps[" + std::to_string(i) + "].";
		PlatformApiClient::MapStat s;
		if (!m.isObject())
		{
			return r.fail(at + " is not an object");
		}
		if (!r.str(m, "map", s.map, at) || !r.integer(m, "games", 0, PlatformApiClient::kMaxCount, s.games, at) || !r.optBool(m, "overflow", s.overflow, at))
		{
			return false;
		}
		if (!validText(s.map, PlatformApiClient::kMaxLabelBytes))
		{
			return r.fail(at + "map is empty, too long or has control characters");
		}
		b.maps.push_back(s);
	}
	out = std::move(b);
	return true;
}

bool retryable(int status)
{
	return status == 0 || status == 408 || status == 429 || status >= 500;
}

// the player's words for the service's refusal codes (docs/PLATFORM_API.md); an unknown code is shown as it came
std::string describe(const std::string &code, const std::string &field)
{
	static const std::pair<const char *, const char *> table[] = {
		{ "rate_limited", "Too many requests: wait a moment and try again." },
		{ "queue_full", "The Quick Match queue is full: try again in a moment." },
		{ "active_ticket_conflict", "You are already searching with other choices: cancel that search first." },
		{ "registration_closed", "Registration for this tournament is closed." },
		{ "tournament_full", "This tournament is full." },
		{ "registration_conflict", "You are registered in this tournament under another nickname." },
		{ "tournament_not_found", "This tournament does not exist any more." },
		{ "match_not_found", "This match does not exist any more." },
		{ "checkin_closed", "Check-in for this match is closed." },
		{ "not_a_match_participant", "You do not play in this match." },
		{ "report_conflict", "This game was reported differently before." },
		{ "stats_cache_unavailable", "The statistics are not ready yet: try again later." },
		{ "unavailable", "The online services are not available right now." },
		{ "storage_busy", "The online services are busy: try again in a moment." },
		{ "ticket_not_found", "The search is not known to the online services any more." },
		{ "body_too_large", "The request was too large." },
	};
	for (const auto &e : table)
	{
		if (code == e.first)
		{
			return e.second;
		}
	}
	if (code == "invalid_field" || code == "missing_field")
	{
		return std::string("The online services refused the request: ") + (field.empty() ? "a field" : field) + (code == "missing_field" ? " is missing." : " is not valid.");
	}
	return "The online services refused the request (" + code + ").";
}
} // namespace

PlatformApiClient::PlatformApiClient(const Options &options) : m_options(options) {}

bool PlatformApiClient::validInstall(const std::string &install)
{
	return lowerHex(install, 32);
}

std::string PlatformApiClient::installTag(const std::string &install)
{
	return validInstall(install) ? install.substr(0, 6) + "..." : std::string("(no install id)");
}

bool PlatformApiClient::validRoomCode(const std::string &code)
{
	return idText(code, 6, kMaxRoomCodeBytes);
}

bool PlatformApiClient::validTicketId(const std::string &id)
{
	return lowerHex(id, 32);
}

bool PlatformApiClient::validTournamentId(const std::string &id)
{
	return lowerHex(id, 32);
}

bool PlatformApiClient::validMatchId(const std::string &id)
{
	return id.size() == 32 + 1 + 24 && lowerHex(id.substr(0, 32), 32) && id[32] == '.' && lowerHex(id.substr(33), 24);
}

bool PlatformApiClient::validGameId(const std::string &id)
{
	if (id.empty() || id.size() > kMaxIdBytes)
	{
		return false;
	}
	for (char c : id)
	{
		if (!idChar(c) && c != '.')
		{
			return false;
		}
	}
	return true;
}

bool PlatformApiClient::validClient(const std::string &platform, const std::string &version, std::string *error)
{
	auto fail = [&](const char *why) {
		if (error)
		{
			*error = why;
		}
		return false;
	};
	if (!oneOf(platform, { "windows", "linux", "macos" }))
	{
		return fail("the platform is not windows / linux / macos");
	}
	if (version.empty() || version.size() > kMaxVersionBytes || !validText(version, kMaxVersionBytes))
	{
		return fail("the version tag is empty, longer than 64 bytes or has control characters");
	}
	return true;
}

std::string PlatformApiClient::jsonQuote(const std::string &utf8)
{
	return OnlineLobbyClient::jsonQuote(utf8);
}

const char *PlatformApiClient::qmStateName(QmState s)
{
	switch (s)
	{
	case QmState::Idle: return "idle";
	case QmState::Starting: return "starting";
	case QmState::Searching: return "searching";
	case QmState::Matched: return "matched";
	case QmState::Expired: return "expired";
	case QmState::Cancelled: return "cancelled";
	case QmState::TimedOut: return "timed_out";
	case QmState::Failed: return "failed";
	}
	return "?";
}

const PlatformApiClient::BracketMatch *PlatformApiClient::TournamentDetail::nextMatch() const
{
	for (const BracketMatch &m : matches)
	{
		if (m.playable())
		{
			return &m;
		}
	}
	return nullptr;
}

bool PlatformApiClient::queue(const std::string &kind, const std::string &ref, const std::string &method, const std::string &path, const std::string &body,
	bool front)
{
	Pending p;
	p.kind = kind;
	p.ref = ref;
	p.request.method = method;
	p.request.path = path;
	p.request.body = body;
	if (m_queue.size() >= kMaxQueue)
	{
		refuse(p, "busy", "too many requests are waiting for the online services");
		return false;
	}
	if (front)
	{
		m_queue.push_front(p);
	}
	else
	{
		m_queue.push_back(p);
	}
	return true;
}

void PlatformApiClient::refuse(const Pending &p, const std::string &code, const std::string &message)
{
	Event e{ Event::Refused };
	e.request = p.kind;
	e.code = code;
	e.id = p.ref;
	e.text = message;
	push(e);
}

void PlatformApiClient::setQm(QmState s, const std::string &why)
{
	m_qm = s;
	m_qmProblem = why;
	Event e{ Event::QmChanged };
	e.code = qmStateName(s);
	e.text = why;
	push(e);
}

std::string PlatformApiClient::clientJson() const
{
	return "\"client\":{\"platform\":" + jsonQuote(m_options.clientPlatform) + ",\"version\":" + jsonQuote(m_options.clientVersion) + "}";
}

void PlatformApiClient::checkHealth()
{
	queue("health", "", "GET", "/health", "");
}

bool PlatformApiClient::qmSearch(const std::string &mode, const std::string &size, const std::vector<std::string> &factions, std::string *error)
{
	auto fail = [&](const std::string &why) {
		if (error)
		{
			*error = why;
		}
		return false;
	};
	if (m_qm == QmState::Starting || m_qm == QmState::Searching)
	{
		return fail("a search is already running");
	}
	if (!validInstall(m_options.install))
	{
		return fail("this installation has no install id");
	}
	std::string clientWhy;
	if (!validClient(m_options.clientPlatform, m_options.clientVersion, &clientWhy))
	{
		return fail("this build has no usable client metadata: " + clientWhy);
	}
	if (!validText(m_options.nick, kMaxNickBytes))
	{
		return fail("the nickname is empty or longer than 24 bytes");
	}
	if (!oneOf(mode, { "faithful", "uber" }))
	{
		return fail("the mode " + quoted(mode, 16) + " is not faithful / uber");
	}
	if (!oneOf(size, { "1v1", "2v2" }))
	{
		return fail("the size " + quoted(size, 16) + " is not 1v1 / 2v2");
	}
	// the service takes up to 8 faction names, or exactly ["random"]
	if (factions.empty() || factions.size() > 8)
	{
		return fail("the factions list must have 1 .. 8 entries");
	}
	std::string list;
	for (const std::string &f : factions)
	{
		if (!idText(f, 1, kMaxFactionBytes))
		{
			return fail("the faction " + quoted(f, 16) + " is not a faction name");
		}
		list += (list.empty() ? "" : ",") + jsonQuote(f);
	}
	for (const std::string &f : factions)
	{
		if (f == "random" && factions.size() != 1)
		{
			return fail("\"random\" is the whole list or nothing");
		}
	}
	if (m_queue.size() >= kMaxQueue)
	{
		return fail("too many requests are waiting for the online services");
	}
	++m_qmGen;
	m_qmMode = mode;
	m_qmSize = size;
	m_ticket.clear();
	m_match = Match();
	m_pollFailures = 0;
	m_pollQueued = false;
	m_qmStarted = m_now;
	setQm(QmState::Starting, "");
	// the queue had room (checked above), so the ticket request is always enqueued; a search never starts without its request
	queue("qm-create", std::to_string(m_qmGen), "POST", "/qm/tickets",
		"{\"v\":1,\"install\":" + jsonQuote(m_options.install) + ",\"nick\":" + jsonQuote(m_options.nick) + "," + clientJson() + ",\"mode\":" + jsonQuote(mode)
			+ ",\"size\":" + jsonQuote(size) + ",\"factions\":[" + list + "]}");
	return true;
}

void PlatformApiClient::qmCancel()
{
	if (m_qm != QmState::Starting && m_qm != QmState::Searching)
	{
		return;
	}
	if (!m_ticket.empty())
	{
		queue("qm-cancel", m_ticket, "DELETE", "/qm/tickets/" + m_ticket, "");
	}
	else
	{
		// a ticket not asked for yet is never asked for; one already being made is deleted when it comes back (deliver: it is no longer this search's)
		for (auto it = m_queue.begin(); it != m_queue.end();)
		{
			it = it->kind == "qm-create" ? m_queue.erase(it) : it + 1;
		}
	}
	setQm(QmState::Cancelled, "the search was cancelled");
}

void PlatformApiClient::qmReset()
{
	if (m_qm == QmState::Starting || m_qm == QmState::Searching)
	{
		qmCancel();
	}
	m_qm = QmState::Idle;
	m_qmProblem.clear();
	m_ticket.clear();
	m_match = Match();
}

void PlatformApiClient::listTournaments()
{
	m_pageSoFar.clear();
	m_pages = 0;
	queue("tournaments", "", "GET", "/tournaments", "");
}

void PlatformApiClient::fetchTournament(const std::string &id)
{
	if (!validTournamentId(id))
	{
		Pending p;
		p.kind = "tournament";
		refuse(p, "bad_field", "the tournament id is not 32 hex digits");
		return;
	}
	queue("tournament", id, "GET", "/tournaments/" + id + (validInstall(m_options.install) ? "?install=" + m_options.install : ""), "");
}

void PlatformApiClient::registerTournament(const std::string &id)
{
	Pending p;
	p.kind = "register";
	p.ref = id;
	if (!validTournamentId(id) || !validInstall(m_options.install) || !validText(m_options.nick, kMaxNickBytes) || !hasClient(m_options))
	{
		refuse(p, "bad_field", "the tournament id, the install id, the nickname or the client metadata is missing");
		return;
	}
	queue("register", id, "POST", "/tournaments/" + id + "/register",
		"{\"v\":1,\"install\":" + jsonQuote(m_options.install) + ",\"nick\":" + jsonQuote(m_options.nick) + "," + clientJson() + "}");
}

void PlatformApiClient::withdrawTournament(const std::string &id)
{
	Pending p;
	p.kind = "withdraw";
	p.ref = id;
	if (!validTournamentId(id) || !validInstall(m_options.install) || !hasClient(m_options))
	{
		refuse(p, "bad_field", "the tournament id, the install id or the client metadata is missing");
		return;
	}
	queue("withdraw", id, "DELETE", "/tournaments/" + id + "/register", "{\"v\":1,\"install\":" + jsonQuote(m_options.install) + "," + clientJson() + "}");
}

void PlatformApiClient::checkIn(const std::string &tournamentId, const std::string &matchId)
{
	Pending p;
	p.kind = "checkin";
	p.ref = matchId;
	if (!validTournamentId(tournamentId) || !validMatchId(matchId) || matchId.compare(0, 32, tournamentId) != 0 || !validInstall(m_options.install)
		|| !hasClient(m_options))
	{
		refuse(p, "bad_field", "the tournament id, the match id, the install id or the client metadata is missing");
		return;
	}
	queue("checkin", matchId, "POST", "/tournaments/" + tournamentId + "/matches/" + matchId + "/checkin",
		"{\"v\":1,\"install\":" + jsonQuote(m_options.install) + "," + clientJson() + "}");
}

bool PlatformApiClient::fetchGlobalStats(const std::string &mode, const std::string &period, std::string *error)
{
	if (!oneOf(mode, { "", "faithful", "uber" }) || !oneOf(period, { "7d", "30d", "all" }))
	{
		if (error)
		{
			*error = "the mode must be every / faithful / uber and the period 7d / 30d / all";
		}
		return false;
	}
	queue("global", mode + "|" + period, "GET", "/stats/global?mode=" + mode + "&period=" + period, "");
	return true;
}

void PlatformApiClient::sendResult(const ResultReport &report)
{
	Pending p;
	p.kind = "result";
	p.ref = report.gameId;
	if (!m_options.shareResults)
	{
		Event e{ Event::ResultDropped };
		e.id = report.gameId;
		e.text = "the player does not share game results";
		push(e);
		return;
	}
	if (!validGameId(report.gameId) || report.json.empty() || report.json.size() > 16 * 1024)
	{
		refuse(p, "bad_field", "the result has no valid game id or its body is empty or larger than 16 KiB");
		return;
	}
	if (m_resultAttempts.count(report.gameId))
	{
		return; // a game's result is sent once (and retried once): the same game reported again is not sent again
	}
	if (!queue("result", report.gameId, "POST", "/results", report.json))
	{
		return;
	}
	m_resultAttempts[report.gameId] = 1;
	m_resultOrder.push_back(report.gameId);
	while (m_resultOrder.size() > kMaxResultsKept)
	{
		m_resultAttempts.erase(m_resultOrder.front());
		m_resultOrder.pop_front();
	}
}

void PlatformApiClient::setShareResults(bool share)
{
	m_options.shareResults = share;
	if (share)
	{
		return;
	}
	auto dropped = [this](const std::string &gameId) {
		Event e{ Event::ResultDropped };
		e.id = gameId;
		e.text = "the player does not share game results";
		push(e);
	};
	for (auto it = m_queue.begin(); it != m_queue.end();)
	{
		if (it->kind == "result")
		{
			dropped(it->ref);
			it = m_queue.erase(it);
		}
		else
		{
			++it;
		}
	}
	for (const Retry &r : m_retries)
	{
		dropped(r.gameId);
	}
	m_retries.clear();
}

void PlatformApiClient::resultFailed(const Pending &p, const std::string &why, bool retry)
{
	int &attempts = m_resultAttempts[p.ref];
	if (retry && attempts < 2 && m_options.shareResults && m_retries.size() < kMaxResultsKept)
	{
		++attempts;
		m_retries.push_back({ p.ref, p.request.body, m_now + m_options.resultRetryMs });
		return;
	}
	Event e{ Event::ResultDropped };
	e.id = p.ref;
	e.text = m_options.shareResults ? why : "the player does not share game results";
	push(e);
}

void PlatformApiClient::update(std::uint64_t nowMs)
{
	m_now = nowMs;
	if (m_qm == QmState::Searching)
	{
		if (m_now - m_qmStarted >= m_options.searchTimeoutMs)
		{
			queue("qm-cancel", m_ticket, "DELETE", "/qm/tickets/" + m_ticket, "");
			setQm(QmState::TimedOut, "no opponent was found");
		}
		else if (!m_pollQueued && m_now >= m_nextPoll)
		{
			// a full queue leaves the poll for the next update (the flag is set only when it is waiting)
			m_pollQueued = queue("qm-poll", m_ticket, "GET", "/qm/tickets/" + m_ticket, "");
		}
	}
	else if (m_qm == QmState::Starting && m_now - m_qmStarted >= m_options.startTimeoutMs)
	{
		// the ticket request was never answered: the search fails (a ticket that arrives later is not this search's and is deleted: deliver)
		for (auto it = m_queue.begin(); it != m_queue.end();)
		{
			it = it->kind == "qm-create" ? m_queue.erase(it) : it + 1;
		}
		setQm(QmState::Failed, "the online services did not make the search's ticket in time");
	}
	for (std::size_t i = 0; i < m_retries.size();)
	{
		if (m_now >= m_retries[i].dueMs)
		{
			if (m_options.shareResults)
			{
				queue("result", m_retries[i].gameId, "POST", "/results", m_retries[i].body);
			}
			m_retries.erase(m_retries.begin() + (std::ptrdiff_t)i);
		}
		else
		{
			++i;
		}
	}
}

std::vector<PlatformApiClient::HttpRequest> PlatformApiClient::takeRequests()
{
	std::vector<HttpRequest> out;
	if (m_inFlight != 0 || m_queue.empty())
	{
		return out;
	}
	m_current = m_queue.front();
	m_queue.pop_front();
	m_current.request.id = m_nextId++;
	m_inFlight = m_current.request.id;
	++m_sent[m_current.kind];
	out.push_back(m_current.request);
	return out;
}

void PlatformApiClient::push(const Event &e)
{
	m_events.push_back(e);
	if (m_events.size() > kMaxEvents)
	{
		m_events.erase(m_events.begin());
	}
	m_uiEvents.push_back(e);
	if (m_uiEvents.size() > kMaxUiEvents)
	{
		m_uiEvents.pop_front(); // the menus read only the latest; nobody reading them (no menu up) must not grow the queue
	}
}

std::vector<PlatformApiClient::Event> PlatformApiClient::takeUiEvents()
{
	std::vector<Event> out(m_uiEvents.begin(), m_uiEvents.end());
	m_uiEvents.clear();
	return out;
}

std::vector<PlatformApiClient::Event> PlatformApiClient::takeEvents()
{
	std::vector<Event> out;
	out.swap(m_events);
	return out;
}

namespace
{
// the HTTP statuses a kind of request may be answered with (docs/PLATFORM_API.md); any other 2xx is a protocol refusal
bool successStatus(const std::string &kind, int status)
{
	if (kind == "qm-create")
	{
		return status == 201; // a ticket is made (an identical search repeated is the same ticket, again 201)
	}
	if (kind == "result")
	{
		return status == 200 || status == 201; // 201 a new report, 200 the same report again
	}
	return status == 200;
}
} // namespace

void PlatformApiClient::deliver(int id, int status, const std::string &body, const std::string &transportError)
{
	if (id == 0 || id != m_inFlight)
	{
		return; // an answer to nothing we asked
	}
	const Pending p = m_current;
	m_inFlight = 0;
	if (p.kind == "qm-poll")
	{
		m_pollQueued = false;
	}
	std::string problem;
	std::string code;
	JsonValue root;
	if (!transportError.empty())
	{
		code = "transport";
		problem = transportError;
	}
	else if (body.size() > kMaxBodyBytes)
	{
		code = "protocol";
		problem = "the answer is larger than " + std::to_string(kMaxBodyBytes) + " bytes";
	}
	else
	{
		std::string why;
		if (!JsonValue::parse(body, root, &why) || !root.isObject() || !root.get("v") || !root.get("v")->isNumber() || root.get("v")->number != 1)
		{
			code = "protocol";
			problem = "HTTP " + std::to_string(status) + " with something that is not platform API version 1";
		}
		else if (const JsonValue *err = root.get("error"))
		{
			// the service's envelope: {"v":1,"error":{"code":"...","field":"..."}}
			const JsonValue *c = err->isObject() ? err->get("code") : nullptr;
			const JsonValue *f = err->isObject() ? err->get("field") : nullptr;
			if (!c || !c->isString() || !idText(c->string, 1, 64) || (f && !(f->isString() && validText(f->string, 64, true))) || status < 400)
			{
				code = "protocol";
				problem = "HTTP " + std::to_string(status) + " with an error that is not {code, field}";
			}
			else
			{
				code = c->string;
				problem = describe(code, f ? f->string : std::string());
			}
		}
		else if (!successStatus(p.kind, status))
		{
			code = "protocol";
			problem = "HTTP " + std::to_string(status) + " is not the answer of a " + p.kind + " request";
		}
	}
	if (!problem.empty())
	{
		const int effective = code == "transport" ? 0 : status;
		if (p.kind == "health")
		{
			m_health = 0;
			Event e{ Event::Health };
			e.flag = false;
			e.text = problem;
			push(e);
			return;
		}
		if (p.kind == "result")
		{
			resultFailed(p, code + ": " + problem, retryable(code == "protocol" ? 500 : effective));
			return;
		}
		if (p.kind == "qm-create")
		{
			if (p.ref == std::to_string(m_qmGen) && m_qm == QmState::Starting)
			{
				setQm(QmState::Failed, "Quick Match is not available: " + problem);
			}
			return; // a refusal for a search that ended meanwhile is nobody's
		}
		if (p.kind == "qm-poll" && m_qm == QmState::Searching && p.ref == m_ticket)
		{
			if (code == "ticket_not_found")
			{
				setQm(QmState::Expired, "the search expired");
				return;
			}
			if (code == "transport" || retryable(effective))
			{
				if (++m_pollFailures >= m_options.pollFailuresAllowed)
				{
					setQm(QmState::Failed, "the online services stopped answering: " + problem);
				}
				return; // the next poll goes out at its time (m_nextPoll is unchanged)
			}
			setQm(QmState::Failed, "the search was refused: " + problem);
			return;
		}
		if (p.kind == "qm-poll" || p.kind == "qm-cancel")
		{
			return; // for a search that ended meanwhile (or a cancel: it was told to the player already)
		}
		if (p.kind == "tournaments")
		{
			m_pageSoFar.clear(); // the pages read so far are not a list
		}
		refuse(p, code, problem);
		return;
	}
	handle(p, root);
}

void PlatformApiClient::handle(const Pending &p, const JsonValue &root)
{
	std::string why;
	Reader r{ &why };
	if (p.kind == "health")
	{
		m_health = 1;
		Event e{ Event::Health };
		e.flag = true;
		push(e);
		return;
	}
	if (p.kind == "qm-create")
	{
		std::string ticket;
		std::int64_t pollAfter = 0;
		const bool ok = r.str(root, "ticket", ticket, "") && r.integer(root, "poll_after_s", 1, kMaxPollAfterS, pollAfter, "");
		if (ok && !validTicketId(ticket))
		{
			why = "ticket is not a ticket id (32 lowercase hex digits)";
		}
		const bool valid = ok && validTicketId(ticket);
		if (p.ref != std::to_string(m_qmGen) || m_qm != QmState::Starting)
		{
			// the search this ticket was made for ended (cancelled, even restarted) while the ticket was being made: the ticket is deleted, ahead of any
			// request of the search that may follow (an identical search would otherwise be handed this very ticket and then lose it)
			if (valid)
			{
				queue("qm-cancel", ticket, "DELETE", "/qm/tickets/" + ticket, "", true);
			}
			return;
		}
		if (!valid)
		{
			setQm(QmState::Failed, "the service's ticket was refused: " + why);
			return;
		}
		m_ticket = ticket;
		m_pollAfterMs = (std::uint64_t)pollAfter * 1000;
		m_nextPoll = m_now + m_pollAfterMs;
		setQm(QmState::Searching, "");
		return;
	}
	if (p.kind == "qm-poll")
	{
		if (m_qm != QmState::Searching || p.ref != m_ticket)
		{
			return; // an answer for a search that ended meanwhile
		}
		std::string state;
		Match match;
		if (!parseTicketState(root, state, match, &why))
		{
			setQm(QmState::Failed, "the service's answer was refused: " + why);
			return;
		}
		// the interval named with the ticket holds until a poll names another; a malformed one is refused, never replaced by a guess
		if (root.get("poll_after_s"))
		{
			std::int64_t pollAfter = 0;
			if (!r.integer(root, "poll_after_s", 1, kMaxPollAfterS, pollAfter, ""))
			{
				setQm(QmState::Failed, "the service's answer was refused: " + why);
				return;
			}
			m_pollAfterMs = (std::uint64_t)pollAfter * 1000;
		}
		m_pollFailures = 0;
		m_nextPoll = m_now + m_pollAfterMs;
		if (state == "searching")
		{
			return;
		}
		if (state == "expired")
		{
			setQm(QmState::Expired, "the search expired");
			return;
		}
		if (state == "cancelled")
		{
			setQm(QmState::Cancelled, "the search was cancelled by the online service");
			return;
		}
		// matched: the match is for this search's mode and size, and has this player in it
		const std::size_t expected = m_qmSize == "2v2" ? 4 : 2;
		if (match.mode != m_qmMode)
		{
			setQm(QmState::Failed, "the match's mode " + quoted(match.mode, 16) + " is not the search's " + quoted(m_qmMode, 16));
			return;
		}
		if (match.players.size() != expected)
		{
			setQm(QmState::Failed, "the match has " + std::to_string(match.players.size()) + " players, a " + m_qmSize + " match has " + std::to_string(expected));
			return;
		}
		// the matched list's first player is the host (the service: the oldest ticket); the others are known by their nickname (the list names no installs)
		match.host = match.hostInstall == m_options.install;
		int mine = match.host ? 0 : -1, same = 0;
		for (std::size_t i = 1; i < match.players.size() && !match.host; ++i)
		{
			if (match.players[i].nick == m_options.nick)
			{
				++same;
				mine = (int)i;
			}
		}
		if (same > 1)
		{
			setQm(QmState::Failed, "the match names more than one player " + quoted(m_options.nick) + ": choose a nickname no one else has");
			return;
		}
		if (mine < 0)
		{
			setQm(QmState::Failed, "the match does not name this player");
			return;
		}
		for (std::size_t i = 0; i < match.players.size(); ++i)
		{
			match.players[i].you = (int)i == mine;
		}
		m_match = match;
		setQm(QmState::Matched, "");
		return;
	}
	if (p.kind == "qm-cancel")
	{
		return;
	}
	if (p.kind == "tournaments")
	{
		std::vector<Tournament> page;
		std::string next;
		if (!parseTournaments(root, page, &next, &why))
		{
			m_pageSoFar.clear();
			refuse(p, "protocol", why);
			return;
		}
		for (const Tournament &t : page)
		{
			for (const Tournament &seen : m_pageSoFar)
			{
				if (seen.id == t.id)
				{
					m_pageSoFar.clear();
					refuse(p, "protocol", "tournament " + t.id + " appears on two pages of the list");
					return;
				}
			}
		}
		m_pageSoFar.insert(m_pageSoFar.end(), page.begin(), page.end());
		++m_pages;
		if (!next.empty() && m_pages < kMaxTournamentPages)
		{
			std::string escaped;
			for (unsigned char c : next)
			{
				char buf[4];
				if (idChar((char)c) || c == '.' || c == '~')
				{
					escaped.push_back((char)c);
				}
				else
				{
					std::snprintf(buf, sizeof(buf), "%%%02X", c);
					escaped += buf;
				}
			}
			queue("tournaments", next, "GET", "/tournaments?cursor=" + escaped, "");
			return; // the list is committed whole, when its last page is in
		}
		m_tournaments = std::move(m_pageSoFar);
		m_pageSoFar.clear();
		m_tournamentsTruncated = !next.empty();
		m_tournamentsKnown = true;
		push(Event{ Event::Tournaments });
		return;
	}
	if (p.kind == "tournament")
	{
		TournamentDetail d;
		if (!parseTournament(root, d, &why))
		{
			refuse(p, "protocol", why);
			return;
		}
		if (d.info.id != p.ref)
		{
			refuse(p, "protocol", "the answer is about tournament " + d.info.id + ", not " + p.ref);
			return;
		}
		m_tournament = std::move(d);
		m_tournamentKnown = true;
		Event e{ Event::TournamentInfo };
		e.id = p.ref;
		push(e);
		return;
	}
	if (p.kind == "register" || p.kind == "withdraw")
	{
		const JsonValue *registered = root.get("registered");
		const bool wanted = p.kind == "register";
		if (!registered || registered->type != JsonValue::BOOL || registered->boolean != wanted)
		{
			refuse(p, "protocol", std::string("the answer's registered is not ") + (wanted ? "true" : "false"));
			return;
		}
		if (wanted)
		{
			m_registered.insert(p.ref);
		}
		else
		{
			m_registered.erase(p.ref);
		}
		Event e{ Event::Registered };
		e.id = p.ref;
		e.flag = wanted;
		push(e);
		if (m_tournamentKnown && m_tournament.info.id == p.ref)
		{
			fetchTournament(p.ref); // the registration count and the bracket move with it
		}
		return;
	}
	if (p.kind == "checkin")
	{
		std::string state;
		if (!r.str(root, "state", state, "") || !oneOf(state, { "open", "ready" }))
		{
			refuse(p, "protocol", why.empty() ? "the check-in's state " + quoted(state, 16) + " is not open / ready" : why);
			return;
		}
		Event e{ Event::CheckedIn };
		e.id = p.ref;
		e.text = state;
		push(e);
		return;
	}
	if (p.kind == "global")
	{
		GlobalStats g;
		if (!parseGlobalStats(root, g, &why))
		{
			refuse(p, "protocol", why);
			return;
		}
		const std::size_t bar = p.ref.find('|');
		g.mode = p.ref.substr(0, bar);
		g.period = p.ref.substr(bar + 1);
		m_global = std::move(g);
		push(Event{ Event::GlobalStatsReady });
		return;
	}
	if (p.kind == "result")
	{
		Event e{ Event::ResultSent };
		e.id = p.ref;
		push(e);
		return;
	}
}

bool PlatformApiClient::parseTicketState(const JsonValue &root, std::string &state, Match &match, std::string *error)
{
	Reader r{ error };
	std::string s;
	if (!r.str(root, "state", s, ""))
	{
		return false;
	}
	if (!oneOf(s, { "searching", "matched", "expired", "cancelled" }))
	{
		return r.fail("state " + quoted(s, 16) + " is not searching / matched / expired / cancelled");
	}
	Match m;
	if (s == "matched")
	{
		const JsonValue *mo = root.get("match");
		if (!mo || !mo->isObject())
		{
			return r.fail("match is missing for a matched ticket");
		}
		if (!r.str(*mo, "id", m.id, "match.") || !r.str(*mo, "room_code", m.roomCode, "match.") || !r.str(*mo, "host_install", m.hostInstall, "match.")
			|| !r.str(*mo, "mode", m.mode, "match.") || !readMatchPlayers(r, *mo, m.players, "match."))
		{
			return false;
		}
		if (!validTicketId(m.id))
		{
			return r.fail("match.id is not a match id (32 lowercase hex digits)");
		}
		if (!validRoomCode(m.roomCode))
		{
			return r.fail("match.room_code is not a room code ([A-Za-z0-9_-], 6 .. 32)");
		}
		if (!validInstall(m.hostInstall))
		{
			return r.fail("match.host_install is not an install id (32 lowercase hex digits)");
		}
		if (!oneOf(m.mode, { "faithful", "uber" }))
		{
			return r.fail("match.mode " + quoted(m.mode, 16) + " is not faithful / uber");
		}
		if (m.players.size() < 2)
		{
			return r.fail("match.players has fewer than 2 players");
		}
	}
	state = s;
	match = m;
	return true;
}

bool PlatformApiClient::parseTournaments(const JsonValue &root, std::vector<Tournament> &out, std::string *nextCursor, std::string *error)
{
	Reader r{ error };
	const JsonValue *list = r.array(root, "tournaments", kMaxTournaments, "");
	if (!list)
	{
		return false;
	}
	std::vector<Tournament> all;
	for (std::size_t i = 0; i < list->array.size(); ++i)
	{
		Tournament t;
		if (!readTournament(r, list->array[i], t, "tournaments[" + std::to_string(i) + "]."))
		{
			return false;
		}
		for (const Tournament &other : all)
		{
			if (other.id == t.id)
			{
				return r.fail("tournaments[" + std::to_string(i) + "].id " + t.id + " appears twice");
			}
		}
		all.push_back(t);
	}
	std::string next;
	const JsonValue *c = root.get("next_cursor");
	if (c && c->type != JsonValue::NUL)
	{
		if (!c->isString() || !validText(c->string, 256))
		{
			return r.fail("next_cursor is not a cursor (1 .. 256 characters)");
		}
		next = c->string;
	}
	out = std::move(all);
	if (nextCursor)
	{
		*nextCursor = next;
	}
	return true;
}

bool PlatformApiClient::parseTournament(const JsonValue &root, TournamentDetail &out, std::string *error)
{
	Reader r{ error };
	TournamentDetail d;
	if (!readTournament(r, root, d.info, "") || !r.str(root, "rules", d.rules, "") || !r.optStr(root, "reason", d.reason, ""))
	{
		return false;
	}
	if (!validLongText(d.rules, kMaxRulesBytes))
	{
		return r.fail("rules is longer than 16384 bytes or has control characters");
	}
	if (!d.reason.empty() && !idText(d.reason, 1, 64))
	{
		return r.fail("reason is not a reason code");
	}
	const JsonValue *rounds = r.array(root, "rounds", kMaxRounds, "");
	const JsonValue *matches = rounds ? r.array(root, "matches", kMaxMatches, "") : nullptr;
	if (!matches)
	{
		return false;
	}
	const std::string prefix = d.info.id + ".";
	std::size_t listed = 0;
	for (std::size_t i = 0; i < rounds->array.size(); ++i)
	{
		const JsonValue &round = rounds->array[i];
		const std::string at = "rounds[" + std::to_string(i) + "]";
		if (!round.isArray() || round.array.size() > kMaxMatches)
		{
			return r.fail(at + " is not a list of at most " + std::to_string(kMaxMatches) + " match ids");
		}
		listed += round.array.size();
		if (listed > kMaxMatches)
		{
			return r.fail("rounds name more than " + std::to_string(kMaxMatches) + " matches");
		}
		std::vector<std::string> ids;
		for (std::size_t k = 0; k < round.array.size(); ++k)
		{
			const JsonValue &id = round.array[k];
			if (!id.isString() || !validMatchId(id.string) || id.string.compare(0, prefix.size(), prefix) != 0)
			{
				return r.fail(at + "[" + std::to_string(k) + "] is not a match id of this tournament");
			}
			ids.push_back(id.string);
		}
		d.rounds.push_back(ids);
	}
	if (listed != matches->array.size())
	{
		return r.fail("rounds name " + std::to_string(listed) + " matches, matches has " + std::to_string(matches->array.size()));
	}
	std::map<std::string, std::size_t> roundOf;
	for (std::size_t i = 0; i < d.rounds.size(); ++i)
	{
		for (const std::string &id : d.rounds[i])
		{
			if (!roundOf.emplace(id, i).second)
			{
				return r.fail("match " + id + " is listed twice in the rounds");
			}
		}
	}
	std::set<std::string> seen;
	for (std::size_t i = 0; i < matches->array.size(); ++i)
	{
		const JsonValue &m = matches->array[i];
		const std::string at = "matches[" + std::to_string(i) + "].";
		if (!m.isObject())
		{
			return r.fail(at + " is not an object");
		}
		BracketMatch bm;
		std::int64_t round = 0;
		if (!r.str(m, "id", bm.id, at) || !r.str(m, "state", bm.state, at) || !r.integer(m, "round", 0, (std::int64_t)kMaxRounds - 1, round, at)
			|| !r.optStr(m, "room_code", bm.roomCode, at) || !r.optStr(m, "winner", bm.winner, at))
		{
			return false;
		}
		bm.round = (int)round;
		if (!validMatchId(bm.id) || bm.id.compare(0, prefix.size(), prefix) != 0)
		{
			return r.fail(at + "id is not a match id of this tournament");
		}
		if (!seen.insert(bm.id).second)
		{
			return r.fail(at + "id " + bm.id + " appears twice in the bracket");
		}
		auto in = roundOf.find(bm.id);
		if (in == roundOf.end() || (int)in->second != bm.round)
		{
			return r.fail(at + "id is not listed in round " + std::to_string(bm.round));
		}
		if (!oneOf(bm.state, { "waiting", "open", "ready", "bye", "finished", "admin_resolved", "no_show", "disputed" }))
		{
			return r.fail(at + "state " + quoted(bm.state, 16) + " is not waiting / open / ready / bye / finished / admin_resolved / no_show / disputed");
		}
		if (!bm.roomCode.empty() && !validRoomCode(bm.roomCode))
		{
			return r.fail(at + "room_code is not a room code");
		}
		const JsonValue *players = m.get("players");
		if (!players || !players->isArray() || players->array.size() != 2)
		{
			return r.fail(at + "players is not a list of 2 nicknames (or null)");
		}
		for (std::size_t k = 0; k < 2; ++k)
		{
			const JsonValue &pl = players->array[k];
			if (pl.type == JsonValue::NUL)
			{
				continue;
			}
			if (!pl.isString() || !validText(pl.string, kMaxReceivedNickBytes))
			{
				return r.fail(at + "players[" + std::to_string(k) + "] is not a nickname");
			}
			bm.players[k] = pl.string;
		}
		if (!bm.winner.empty() && bm.winner != bm.players[0] && bm.winner != bm.players[1])
		{
			return r.fail(at + "winner is not one of the match's players");
		}
		const JsonValue *opened = m.get("opened_at");
		if (opened && opened->type != JsonValue::NUL && !r.integer(m, "opened_at", 0, (std::int64_t)1 << 40, bm.openedAt, at))
		{
			return false;
		}
		d.matches.push_back(bm);
	}
	out = std::move(d);
	return true;
}

bool PlatformApiClient::parseGlobalStats(const JsonValue &root, GlobalStats &out, std::string *error)
{
	Reader r{ error };
	GlobalStats g;
	if (!r.integer(root, "as_of", 0, (std::int64_t)1 << 40, g.asOf, "") || !readBucket(r, root, g.humans, ""))
	{
		return false;
	}
	const JsonValue *vsAi = root.get("vs_ai");
	if (vsAi && vsAi->type != JsonValue::NUL && !readBucket(r, *vsAi, g.vsAi, "vs_ai."))
	{
		return false;
	}
	g.known = true;
	out = std::move(g);
	return true;
}
