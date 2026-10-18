// OpenBFME. GPL-3.0.
#pragma once

#include <charconv>

// One correctly rounded, locale-independent decimal parser on every platform (fast_float, engine/thirdparty/fast_float). The vendored headers are
// included by Common/System/FloatFromChars.cpp alone: its SSE2 header includes made libclang 18 report parse errors in every simulation file that
// reached them, and the AST audit (tools/sim/sim_audit.py) then misread those files.
// As std::from_chars: on result_out_of_range the value is stored as 0 or +-infinity (the callers decide the out-of-range value themselves).
std::from_chars_result FloatFromChars(const char *first, const char *last, float &value) noexcept;
std::from_chars_result FloatFromChars(const char *first, const char *last, double &value) noexcept;
