// OpenBFME. GPL-3.0.
//
// SnapshotProbe (lane SAVE-4e review r1, Sol: "test each closed set with an out-of-set value"): a test hook in every closed-set check of a snapshot load
// (SnapshotXfer::checkRange / checkMask / maskWithin / maskWords / enumIn, AIXfer::inRange / enumIn / issued, checkLoadedUpgradeMask). Off unless a test
// arms it on its own thread. Armed with an index n, the n-th check of the load (counted from 0 in call order) treats the value it checks as one outside its
// closed set, as a hostile save holding such a value at that place would make it: the check must then refuse the load, named. With a log set, every check
// records its name in call order (the inventory of the checks a real load runs through). Not simulation code: it runs only inside a snapshot load.

#pragma once

#include <string>
#include <vector>

namespace SnapshotProbe
{
struct State
{
	long long armed = -1;                ///< the check to fail (-1: none)
	long long calls = 0;                 ///< checks counted since the test reset it
	std::vector<std::string> *log = nullptr;
	int suspended = 0;                   ///< > 0: the checks run outside the probed load (the load's own save and put-back): not counted
};
// this thread's state (a test arms and reads it; the load's checks consult it)
State &state();
// called by every closed-set check with its name: true when this is the armed check (the check then behaves as for an out-of-set value)
inline bool hit(const char *what)
{
	State &s = state();
	if ((s.armed < 0 && !s.log) || s.suspended > 0)
	{
		return false;
	}
	if (s.log)
	{
		s.log->push_back(what);
	}
	return s.calls++ == s.armed;
}
inline bool hit(const std::string &what) { return hit(what.c_str()); }
// the checks inside this scope are not the probed load's (LiveGame::loadSnapshot's keeping save, its put-back load and its byte check)
struct Suspend
{
	Suspend() { ++state().suspended; }
	~Suspend() { --state().suspended; }
	Suspend(const Suspend &) = delete;
	Suspend &operator=(const Suspend &) = delete;
};
} // namespace SnapshotProbe
