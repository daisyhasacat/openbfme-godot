// OpenBFME. GPL-3.0.
// See Common/FloatFromChars.h. Not simulation arithmetic of ours: the third-party parser is checked against the C++ library by
// tools/sim/compare_float_chars.cpp (docs/MACOS.md) and through scanRealText by tests/test_win1_crt_parse.cpp.

#include "Common/FloatFromChars.h"

#include <fast_float/fast_float.h>

namespace
{
template <typename T>
std::from_chars_result parse(const char *first, const char *last, T &value) noexcept
{
	const auto result = fast_float::from_chars(first, last, value, fast_float::chars_format::general);
	return { result.ptr, result.ec };
}
} // namespace

std::from_chars_result FloatFromChars(const char *first, const char *last, float &value) noexcept
{
	return parse(first, last, value);
}

std::from_chars_result FloatFromChars(const char *first, const char *last, double &value) noexcept
{
	return parse(first, last, value);
}
