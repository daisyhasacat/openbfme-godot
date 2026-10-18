// OpenBFME. GPL-3.0.
//
// Small transfer helpers of lane SAVE-4c's blocks (they follow docs/SAVES.md section 8's rules for xfer authors) (obfme:AIWorld, obfme:SkirmishAI, obfme:Shroud, obfme:Partition): the composite values and containers
// the AI state is made of, each in one fixed layout. Containers travel with their length first (Xfer::xferCount: a damaged count is a named error) and in
// their own order (a std::map / std::set in key order, never by pointer or hash order). Not simulation maths: bytes only.

#pragma once

#include "Common/SnapshotProbe.h"
#include "Common/INIDataTypes.h"
#include "Common/Xfer.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace AIXfer
{
inline void coord(Xfer &x, Coord3D &c)
{
	x.xferReal(&c.x);
	x.xferReal(&c.y);
	x.xferReal(&c.z);
}

inline void coord2(Xfer &x, Coord2D &c)
{
	x.xferReal(&c.x);
	x.xferReal(&c.y);
}

inline void icoord2(Xfer &x, ICoord2D &c)
{
	x.xferInt(&c.x);
	x.xferInt(&c.y);
}

inline void u64(Xfer &x, unsigned long long &v)
{
	std::uint64_t w = v;
	x.xferUnsignedInt64(&w);
	v = w;
}

inline void uns(Xfer &x, unsigned &v)
{
	std::uint32_t w = v;
	x.xferUnsignedInt(&w);
	v = w;
}

// a loaded value outside its closed set (an enum, a flag word, an index the block knows the size of) is refused by name (docs/SAVES.md section 8, rule 5)
inline void inRange(long long v, long long lo, long long hi, const char *what)
{
	if (SnapshotProbe::hit(what)) // lane SAVE-4e: a test's out-of-set value here
	{
		v = hi < 0x7FFFFFFFFFFFFFFFLL ? hi + 1 : lo - 1;
	}
	if (v < lo || v > hi)
	{
		throw XferException(std::string(what) + " " + std::to_string(v) + " is outside " + std::to_string(lo) + " .. " + std::to_string(hi));
	}
}

// an enum-like value of the closed set lo .. hi: transferred as an int, checked on load BEFORE it becomes the enum (a value outside an enum's range is not
// a value of the type: UBSan)
template <typename E>
void enumIn(Xfer &x, E &e, long long lo, long long hi, const char *what)
{
	std::int32_t v = x.isLoading() ? 0 : (std::int32_t)e;
	x.xferInt(&v);
	if (x.isLoading())
	{
		inRange(v, lo, hi, what);
	}
	e = (E)v;
}

// the containers: every size through Xfer::xferVectorSize / xferCount with the fewest bytes an entry encodes to (rule 3); a load fills a fresh container
inline void ids(Xfer &x, std::vector<std::uint32_t> &v)
{
	x.xferVectorSize(v, 4);
	for (std::uint32_t &id : v)
	{
		x.xferUnsignedInt(&id);
	}
}

inline void ints(Xfer &x, std::vector<int> &v)
{
	x.xferVectorSize(v, 4);
	for (int &i : v)
	{
		std::int32_t w = x.isLoading() ? 0 : i;
		x.xferInt(&w);
		i = w;
	}
}

inline void strings(Xfer &x, std::vector<std::string> &v)
{
	x.xferVectorSize(v, 1); // an empty string is its length byte
	for (std::string &s : v)
	{
		x.xferAsciiString(&s);
	}
}

inline void coords(Xfer &x, std::vector<Coord3D> &v)
{
	x.xferVectorSize(v, 12);
	for (Coord3D &c : v)
	{
		coord(x, c);
	}
}

// a map in key order; `kv(key, value)` transfers one entry of at least `minEntryBytes` (on load into default-made locals, inserted in the order read; a
// key read twice is refused)
template <typename K, typename V, typename C, typename F>
void map(Xfer &x, std::map<K, V, C> &m, size_t minEntryBytes, F kv)
{
	std::uint32_t n = (std::uint32_t)m.size();
	x.xferCount(&n, minEntryBytes, sizeof(std::pair<const K, V>) + Xfer::kNodeBytes);
	if (!x.isLoading())
	{
		for (auto &e : m)
		{
			K k = e.first;
			kv(k, e.second);
		}
		return;
	}
	std::map<K, V, C> loaded;
	for (std::uint32_t i = 0; i < n; ++i)
	{
		K k{};
		V v{};
		kv(k, v);
		if (!loaded.emplace(k, std::move(v)).second)
		{
			throw XferException("a map holds a key twice");
		}
	}
	m.swap(loaded);
}

template <typename K, typename V, typename F>
void multimap(Xfer &x, std::multimap<K, V> &m, size_t minEntryBytes, F kv)
{
	std::uint32_t n = (std::uint32_t)m.size();
	x.xferCount(&n, minEntryBytes, sizeof(std::pair<const K, V>) + Xfer::kNodeBytes);
	if (!x.isLoading())
	{
		for (auto &e : m)
		{
			K k = e.first;
			kv(k, e.second);
		}
		return;
	}
	std::multimap<K, V> loaded;
	for (std::uint32_t i = 0; i < n; ++i)
	{
		K k{};
		V v{};
		kv(k, v);
		loaded.emplace_hint(loaded.end(), k, std::move(v)); // equal keys keep the order they were saved in (insertion order)
	}
	m.swap(loaded);
}

// The strings of one block's diagnostics (the stop lines every unit's mover, locomotors and AI keep, each a long sentence and the same for many units):
// while a StringTableScope lives on this thread, reportString / reportStrings write a string once (its new index, then the text) and later by index alone.
// Without a scope they are plain strings. Only diagnostics use it (the report's stop lines); no simulation state goes through it.
struct StringTable
{
	std::vector<std::string> list;
	std::map<std::string, std::uint32_t> index; ///< looked up only, never iterated
};
inline thread_local StringTable *t_stringTable = nullptr;
struct StringTableScope
{
	StringTable table;
	StringTable *previous;
	StringTableScope() : previous(t_stringTable) { t_stringTable = &table; }
	~StringTableScope() { t_stringTable = previous; }
	StringTableScope(const StringTableScope &) = delete;
	StringTableScope &operator=(const StringTableScope &) = delete;
};

inline void reportString(Xfer &x, std::string &s)
{
	StringTable *t = t_stringTable;
	if (!t)
	{
		x.xferAsciiString(&s);
		return;
	}
	if (!x.isLoading())
	{
		auto it = t->index.find(s);
		std::uint32_t i = it == t->index.end() ? (std::uint32_t)t->list.size() : it->second;
		x.xferUnsignedInt(&i);
		if (it == t->index.end())
		{
			x.xferAsciiString(&s);
			t->index.emplace(s, i);
			t->list.push_back(s);
		}
		return;
	}
	std::uint32_t i = 0;
	x.xferUnsignedInt(&i);
	if (i == (std::uint32_t)t->list.size())
	{
		x.xferAsciiString(&s);
		t->list.push_back(s);
	}
	else if (i < (std::uint32_t)t->list.size())
	{
		s = t->list[i];
	}
	else
	{
		throw XferException("a report string reference " + std::to_string(i) + " beyond the " + std::to_string(t->list.size()) + " strings read");
	}
}

inline void reportStrings(Xfer &x, std::vector<std::string> &v)
{
	x.xferVectorSize(v, t_stringTable ? 4 : 1); // an index, or an empty string's length byte
	for (std::string &s : v)
	{
		reportString(x, s);
	}
}

inline void idSet(Xfer &x, std::set<std::uint32_t> &s)
{
	std::uint32_t n = (std::uint32_t)s.size();
	x.xferCount(&n, 4, sizeof(std::uint32_t) + Xfer::kNodeBytes);
	if (!x.isLoading())
	{
		for (std::uint32_t id : s)
		{
			x.xferUnsignedInt(&id);
		}
		return;
	}
	std::set<std::uint32_t> loaded;
	for (std::uint32_t i = 0; i < n; ++i)
	{
		std::uint32_t id = 0;
		x.xferUnsignedInt(&id);
		if (!loaded.insert(id).second)
		{
			throw XferException("a set holds the id " + std::to_string(id) + " twice");
		}
	}
	s.swap(loaded);
}
// Object ids an AI keeps and looks up when it uses them (a victim, a requester, a producer, a goal object): a unit that died since stays named until the
// state that holds it moves on, so a live object cannot be required; the closed set is the ids the game has issued. While an IssuedIdScope lives on this
// thread (the AI world's and an AI module's load), objectId refuses an id at or beyond the game's next object id; INVALID_ID (0) always passes
inline thread_local std::uint32_t t_issuedIdLimit = 0;
struct IssuedIdScope
{
	std::uint32_t previous;
	explicit IssuedIdScope(std::uint32_t nextObjectId) : previous(t_issuedIdLimit) { t_issuedIdLimit = nextObjectId; }
	~IssuedIdScope() { t_issuedIdLimit = previous; }
	IssuedIdScope(const IssuedIdScope &) = delete;
	IssuedIdScope &operator=(const IssuedIdScope &) = delete;
};

inline void issued(std::uint32_t id, const char *what)
{
	if (SnapshotProbe::hit(what) && t_issuedIdLimit != 0) // lane SAVE-4e: a test's id the game has not issued
	{
		id = t_issuedIdLimit;
	}
	if (id != 0 && t_issuedIdLimit != 0 && id >= t_issuedIdLimit)
	{
		throw XferException(std::string(what) + " " + std::to_string(id) + " is an object id the game has not issued (the next is " +
			std::to_string(t_issuedIdLimit) + ")");
	}
}

inline void objectId(Xfer &x, std::uint32_t &id, const char *what)
{
	x.xferUnsignedInt(&id);
	if (x.isLoading())
	{
		issued(id, what);
	}
}

// A block that commits inside its xfer checks its framing first: the block must be read to its end (review r3: GameState's endBlock check runs after the
// xfer returned, which is after the commit)
inline void blockReadToEnd(Xfer &x, const char *what)
{
	if (x.isLoading() && x.loadBytesLeftInBlock() != 0)
	{
		throw XferException(std::string(what) + ": " + std::to_string(x.loadBytesLeftInBlock()) + " bytes of the block are left after its state");
	}
}

// A load as one transaction (common.md "Hostile input" 2: on refusal the state is unchanged): before `body` loads anything, the current state is saved
// by `body` itself; when the load throws, that capture is loaded back (a save / load round trip gives the same state, T2) and the load's error goes on.
// Saving and checksumming run `body` once. The capture has its own report string table
template <typename F>
void transaction(Xfer &x, F body)
{
	if (!x.isLoading())
	{
		body(x);
		return;
	}
	std::vector<std::uint8_t> backup;
	{
		StringTableScope scope;
		XferSave save;
		body(save);
		backup = save.data();
	}
	try
	{
		body(x);
	}
	catch (const std::exception &e)
	{
		try
		{
			StringTableScope scope;
			XferLoad restore(backup);
			body(restore);
		}
		catch (const std::exception &r)
		{
			throw XferException(std::string(e.what()) + "; and the state before the load could not be restored: " + r.what());
		}
		throw;
	}
}
} // namespace AIXfer
