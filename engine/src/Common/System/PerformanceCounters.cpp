// OpenBFME. GPL-3.0. Diagnostic clocks and counters are never consumed by the simulation.
#include "Common/PerformanceCounters.h"
#include <atomic>
#include <chrono>
namespace perf
{
namespace {
std::atomic<bool> capturing{false};
struct AtomicCounter { std::atomic<std::uint64_t> ns{0}, calls{0}; };
std::array<AtomicCounter, Count> counters;
thread_local std::array<unsigned, Count> depths{};
std::uint64_t now() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}
void setEnabled(bool value) { capturing.store(value, std::memory_order_relaxed); }
bool enabled() { return capturing.load(std::memory_order_relaxed); }
Snapshot snapshot() {
 Snapshot result;
 for (int i=0; i<Count; ++i) {
  result[i].ns = counters[i].ns.load(std::memory_order_relaxed);
  result[i].calls = counters[i].calls.load(std::memory_order_relaxed);
 }
 return result;
}
Scope::Scope(Category category) : m_category(category), m_active(enabled()) {
 if (m_active) { m_outer = depths[category]++ == 0; if (m_outer) m_start = now(); }
}
Scope::~Scope() {
 if (!m_active) return;
 --depths[m_category];
 if (m_outer) {
  counters[m_category].ns.fetch_add(now()-m_start, std::memory_order_relaxed);
  counters[m_category].calls.fetch_add(1, std::memory_order_relaxed);
 }
}
}
