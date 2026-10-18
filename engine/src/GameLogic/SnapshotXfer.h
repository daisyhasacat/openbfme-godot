// OpenBFME. GPL-3.0.
//
// SnapshotXfer (lane SAVE-4b, docs/SAVES.md): the transfers the object and module xfers share. Retail has the same helpers as free functions taking the
// Xfer (XferObjectID RW 0x707887, xferSTLObjectIDList, the Coord3D composite of Xfer.cpp, SAVES-RETAIL.md section 6 "call:" entries); these are OpenBFME's
// for its own types. Pointers to objects travel as object ids and are resolved at once: the Objects block creates every object of the snapshot before any
// object's state loads (GameLogic/GameLogicObjectSnapshot.cpp), so an id that names no object is a damaged snapshot, a named error. Templates travel by
// name and are looked up in this game's data; a name the data lacks is a named error too.
//
// Not simulation maths: bytes are copied, floats by their bit patterns.

#pragma once

#include "Common/SnapshotProbe.h"
#include "GameLogic/ObjectTypes.h"
#include "Common/INIDataTypes.h"
#include "Common/Xfer.h"

#include <array>
#include <cstdint>
#include <deque>
#include <list>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

class GameLogic;
class Object;
class ThingTemplate;
class WeaponTemplate;
class UpgradeTemplate;
class SpecialPowerTemplate;
struct DamageInfo;
struct DamageInfoInput;
struct ObjectCreationList;

namespace SnapshotXfer
{
// a counter of the build's `unsigned long long` (64 bits on every target), as a u64
inline void u64(Xfer &x, unsigned long long &v)
{
	std::uint64_t w = x.isLoading() ? 0 : (std::uint64_t)v; // a load never reads its destination (SAVES.md, rules for xfer authors)
	x.xferUnsignedInt64(&w);
	v = (unsigned long long)w;
}
void coord3D(Xfer &x, Coord3D &c);
void coord2D(Xfer &x, Coord2D &c);
void icoord2D(Xfer &x, ICoord2D &c);
// the encoded size of damageInfo (a version byte, the input's fixed fields, the output's): the fewest bytes of a container entry holding one
constexpr size_t kDamageInfoBytes = 105;
void damageInput(Xfer &x, DamageInfoInput &in);
void damageInfo(Xfer &x, DamageInfo &info);

// fixed-size words (bit masks): the size is the build's, written once so a different size is a named error rather than a shifted read
template <size_t N>
void words(Xfer &x, std::array<std::uint32_t, N> &w)
{
	std::uint32_t n = (std::uint32_t)N;
	x.xferUnsignedInt(&n);
	if (n != N)
	{
		throw XferException("a mask of " + std::to_string(n) + " words where this build has " + std::to_string(N));
	}
	for (std::uint32_t &v : w)
	{
		x.xferUnsignedInt(&v);
	}
}
template <size_t N>
void words(Xfer &x, std::uint32_t (&w)[N])
{
	std::uint32_t n = (std::uint32_t)N;
	x.xferUnsignedInt(&n);
	if (n != N)
	{
		throw XferException("a mask of " + std::to_string(n) + " words where this build has " + std::to_string(N));
	}
	for (std::uint32_t &v : w)
	{
		x.xferUnsignedInt(&v);
	}
}

// lane SAVE-4e review r1 (Sol: closed sets): a bit mask loaded into a temporary and refused when it holds a bit outside its closed set (`legal`; `set` names
// it: "the 22 conditions"), then stored: "<what> 0x... hold bits outside <set> (0x...)"
std::string hex32(std::uint64_t v);
inline std::uint64_t outsideOf(std::uint64_t legal) { return ~legal & (legal + 1); } // the lowest bit outside a mask of low bits (0: none)
inline void checkMask(std::uint64_t v, std::uint64_t legal, const std::string &what, const std::string &set)
{
	if (SnapshotProbe::hit(what)) // lane SAVE-4e: a test's out-of-set value here
	{
		v |= outsideOf(legal) ? outsideOf(legal) : 0x80000000ull;
	}
	if ((v & ~legal) != 0)
	{
		throw XferException(what + " " + hex32(v) + " hold bits outside " + set + " (" + hex32(legal) + ")");
	}
}
template <typename T>
void maskWithin(Xfer &x, T &dst, std::uint64_t legal, const std::string &what, const std::string &set)
{
	std::uint32_t v = x.isLoading() ? 0u : (std::uint32_t)dst;
	x.xferUnsignedInt(&v);
	if (x.isLoading())
	{
		checkMask(v, legal, what, set);
	}
	dst = (T)v;
}
// the sizes of the binary's name lists the masks are over (lane SAVE-4e): model conditions (591), object status (106), weapon set conditions (104), KindOf (222)
int modelConditionCount();
int objectStatusCount();
int weaponConditionCount();
int kindOfCount();
// the bits [0, bits) of a set of `bits` names, the others refused
inline std::uint64_t lowBits(int bits) { return bits >= 64 ? ~0ull : ((1ull << bits) - 1ull); }
// a word array mask (SnapshotXfer::words' layout) loaded into a temporary: any bit at or beyond `bits` (the set's size) is refused by its number
template <size_t N>
void maskWords(Xfer &x, std::array<std::uint32_t, N> &w, int bits, const std::string &what, const std::string &set)
{
	std::array<std::uint32_t, N> v = x.isLoading() ? std::array<std::uint32_t, N>{} : w;
	words(x, v);
	if (x.isLoading() && SnapshotProbe::hit(what) && bits >= 0 && (size_t)bits < N * 32) // lane SAVE-4e: a test's bit beyond the set
	{
		v[(size_t)bits >> 5] |= 1u << (bits & 31);
	}
	for (size_t k = 0; k < N * 32 && x.isLoading(); ++k)
	{
		if ((int)k >= bits && ((v[k >> 5] >> (k & 31)) & 1u))
		{
			throw XferException(what + " hold bit " + std::to_string(k) + ", beyond " + set + " (" + std::to_string(bits) + ")");
		}
	}
	w = v;
}

// ZH Xfer::xferSTLObjectIDList (RotWK RW 0x76ABAF's callers, e.g. TunnelTracker::xfer RW 0x8FA569; inference: the same code): version (1, 1), a u16 count,
// the ids. The u16 count keeps retail's width; a load checks and charges it like any count (Xfer::checkLoadedCount) and reads the ids into a temporary
void stlObjectIDList(Xfer &x, std::list<ObjectID> &v);
// containers in their order (std::set / std::map iterate in key order: a defined order). Every count goes through Xfer::xferCount with the entry's encoded
// size (SAVES.md section 8, "rules for xfer authors"); a set loaded with a repeated entry is refused (a save never writes one)
void ids(Xfer &x, std::vector<ObjectID> &v);
void ids(Xfer &x, std::list<ObjectID> &v);
void ids(Xfer &x, std::deque<ObjectID> &v);
void ids(Xfer &x, std::set<ObjectID> &v);
void ints(Xfer &x, std::vector<int> &v);
void uints(Xfer &x, std::vector<std::uint32_t> &v);
void reals(Xfer &x, std::vector<float> &v);
void strings(Xfer &x, std::vector<std::string> &v);
void strings(Xfer &x, std::set<std::string> &v);
void bools(Xfer &x, std::vector<bool> &v);
void coords(Xfer &x, std::vector<Coord3D> &v);

// the memory one element of a container costs (an allocation budget estimate: a node container's node beyond its value)
template <typename C>
constexpr size_t entryMemory()
{
	return sizeof(typename C::value_type) + (std::is_same<C, std::vector<typename C::value_type>>::value ? 0 : Xfer::kNodeBytes);
}

// any sequence container (vector, list, deque) of T with a per-element function; `minEntryBytes` is the fewest bytes one element encodes to
template <typename C, typename F>
void each(Xfer &x, C &c, size_t minEntryBytes, F fn)
{
	std::uint32_t n = x.isLoading() ? 0 : (std::uint32_t)c.size();
	x.xferCount(&n, minEntryBytes, entryMemory<C>());
	if (x.isLoading())
	{
		c.clear();
		c.resize(n);
	}
	for (auto &e : c)
	{
		fn(e);
	}
}

// a std::map in key order: `key(K &)` and `value(V &)` transfer one entry. A load fills a temporary, refuses a key listed twice (named by `what`), then
// replaces the map
template <typename K, typename V, typename FK, typename FV>
void map(Xfer &x, std::map<K, V> &m, size_t minEntryBytes, const char *what, FK key, FV value)
{
	std::uint32_t n = x.isLoading() ? 0 : (std::uint32_t)m.size();
	x.xferCount(&n, minEntryBytes, sizeof(std::pair<const K, V>) + Xfer::kNodeBytes);
	if (!x.isLoading())
	{
		for (auto &kv : m)
		{
			K k = kv.first;
			key(k);
			value(kv.second);
		}
		return;
	}
	std::map<K, V> loaded;
	for (std::uint32_t i = 0; i < n; ++i)
	{
		K k{};
		key(k);
		if (loaded.count(k))
		{
			throw XferException(std::string(what) + ": the key " + std::to_string(k) + " is listed twice");
		}
		value(loaded[k]);
	}
	m.swap(loaded);
}

// a loaded value checked against its closed range [lo, hi] (an index, a player, a count bounded by its container): `what` V "is not one of" lo .. hi
void checkRange(std::int64_t v, std::int64_t lo, std::int64_t hi, const std::string &what);
// the entries of one of the binary's NULL-terminated name lists (the closed set of the enum it names)
int nameCount(const char *const *names);

// an enum-like int loaded only from its closed set [lo, hi] (SAVES.md, rules for xfer authors: no clamping, no default); `what` names it
template <typename E>
void enumIn(Xfer &x, E &e, std::int32_t lo, std::int32_t hi, const char *what)
{
	std::int32_t v = x.isLoading() ? 0 : (std::int32_t)e;
	x.xferInt(&v);
	if (x.isLoading() && SnapshotProbe::hit(what)) // lane SAVE-4e: a test's out-of-set value here
	{
		v = hi < 0x7FFFFFFF ? hi + 1 : lo - 1;
	}
	if (v < lo || v > hi)
	{
		throw XferException(std::string(what) + " " + std::to_string(v) + " is not one of " + std::to_string(lo) + " .. " + std::to_string(hi));
	}
	e = (E)v;
}

// an object of this game by id: null for INVALID_ID; an id no object has is an XferException naming `what`. While an Objects load runs, only the ids of the
// snapshot's object table are objects (an object a creation made on its own, deleted after the load, is not)
Object *findObject(GameLogic &logic, ObjectID id, const char *what);
// a contain's list of riders (its riders, a horde's members, a siege crew) of the object `owner`, as ids. A load validates every id (an object of the snapshot,
// not INVALID_ID, listed once); during an Objects load the list is staged (GameLogic::SnapshotLink::RIDERS) and installed only after both sides of every
// link are checked, otherwise it is replaced at once
void objectPointers(Xfer &x, GameLogic &logic, std::list<Object *> &list, const char *what, ObjectID owner);
// the object `owner`'s container, staged like the rider lists (GameLogic::SnapshotLink::CONTAINED_BY)
void containerPointer(Xfer &x, GameLogic &logic, Object *&container, ObjectID owner);

// templates by name (null: the empty name); a name this game's data lacks is an XferException
void thingTemplate(Xfer &x, GameLogic &logic, const ThingTemplate *&t);
void weaponTemplate(Xfer &x, const WeaponTemplate *&t);
void upgradeTemplate(Xfer &x, const UpgradeTemplate *&t);
void specialPowerTemplate(Xfer &x, const SpecialPowerTemplate *&t);

// the version byte of a class's own layout, with the class's name in the error (the window check is Xfer::xferVersion's)
void version(Xfer &x, std::uint8_t earliest, std::uint8_t current);
} // namespace SnapshotXfer
