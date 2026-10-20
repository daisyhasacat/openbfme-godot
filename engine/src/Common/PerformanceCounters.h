// OpenBFME. GPL-3.0. Opt-in wall-clock diagnostics; never authoritative state.
#pragma once
#include <array>
#include <cstdint>
namespace perf
{
enum Category { Simulation, Lua, Pathing, Physics, Collision, Count };
struct Counter { std::uint64_t ns = 0, calls = 0; };
using Snapshot = std::array<Counter, Count>;
void setEnabled(bool enabled);
bool enabled();
Snapshot snapshot();
// Same-category recursion is counted only by the outermost scope on each thread.
class Scope
{
public:
 explicit Scope(Category category);
 ~Scope();
 Scope(const Scope &) = delete;
 Scope &operator=(const Scope &) = delete;
private:
 Category m_category;
 bool m_active = false, m_outer = false;
 std::uint64_t m_start = 0;
};
}
