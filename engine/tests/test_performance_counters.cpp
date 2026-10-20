// OpenBFME. GPL-3.0. Profiling changes diagnostics only, including on the logic worker.
#include "Common/PerformanceCounters.h"
#include "doctest.h"
#include "MoveTestUtil.h"
#include <thread>

TEST_CASE("performance: disabled scopes, recursion, threads and enable transitions")
{
 perf::setEnabled(false);
 const auto before = perf::snapshot();
 { perf::Scope scope(perf::Pathing); }
 CHECK(perf::snapshot()[perf::Pathing].calls == before[perf::Pathing].calls);
 CHECK(perf::snapshot()[perf::Pathing].ns == before[perf::Pathing].ns);
 perf::setEnabled(true);
 {
  perf::Scope outer(perf::Pathing);
  { perf::Scope nested(perf::Pathing); }
  std::thread worker([] { perf::Scope otherThread(perf::Pathing); });
  worker.join();
 }
 CHECK(perf::snapshot()[perf::Pathing].calls == before[perf::Pathing].calls + 2);
 {
  perf::Scope scope(perf::Physics);
  perf::setEnabled(false); // completing an in-flight scope remains well-defined
 }
 CHECK(perf::snapshot()[perf::Physics].calls == before[perf::Physics].calls + 1);
}

TEST_CASE("performance: enabling diagnostics preserves every movement frame hash")
{
 auto run = [](bool enabled) {
  perf::setEnabled(enabled);
  movetest::MoveWorld mw;
  mw.buildMap();
  const int alice = mw.playerIndex("Alice");
  std::vector<Object *> units;
  for (int i=0; i<20; ++i) units.push_back(mw.spawn("Walker", 105.0f+float(i%5)*30.0f, 105.0f+float(i/5)*30.0f));
  mw.frames(2);
  mw.select(alice, units);
  mw.moveTo(alice, 605.0f, 605.0f);
  std::vector<std::uint32_t> hashes;
  for (int i=0; i<80; ++i) { mw.frames(1); hashes.push_back(mw.logic->computeStateHash()); }
  perf::setEnabled(false);
  return hashes;
 };
 const auto before = perf::snapshot();
 const auto disabled = run(false);
 const auto enabled = run(true);
 CHECK(disabled == enabled);
 const auto after = perf::snapshot();
 // tolerance: callbacks depend on movement, but every instrumented subsystem must execute in this workload.
 CHECK(after[perf::Pathing].calls > before[perf::Pathing].calls);
 // tolerance: the scheduler runs multiple phases per frame; this checks instrumentation coverage, not timing.
 CHECK(after[perf::Simulation].calls > before[perf::Simulation].calls);
 // tolerance: collision calls depend on object configuration; at least one pass must be measured.
 CHECK(after[perf::Collision].calls > before[perf::Collision].calls);
}
