// OpenBFME. GPL-3.0.
#pragma once

#include <cstdint>
#include <cstring>
#include <limits>

namespace ArithmeticNaN
{
// Only a newly generated invalid-arithmetic result gets x86's indefinite bits.
// Operand NaNs retain the operation's existing sign/payload behaviour; x86 is a no-op.
template <typename Float, typename Bits>
inline Float generated(Float a, Float b, Float result, Bits infinity, Bits indefinite)
{
	static_assert(sizeof(Float) == sizeof(Bits), "IEEE storage width");
#if !defined(__i386__) && !defined(__x86_64__) && !defined(_M_IX86) && !defined(_M_X64)
	Bits r;
	std::memcpy(&r, &result, sizeof r);
	const Bits magnitude = std::numeric_limits<Bits>::max() >> 1;
	if ((r & magnitude) > infinity)
	{
		Bits x, y;
		std::memcpy(&x, &a, sizeof x);
		std::memcpy(&y, &b, sizeof y);
		if ((x & magnitude) <= infinity && (y & magnitude) <= infinity)
			std::memcpy(&result, &indefinite, sizeof result);
	}
#else
	(void)a;
	(void)b;
	(void)infinity;
	(void)indefinite;
#endif
	return result;
}

inline float generated(float a, float b, float result)
{
	return generated(a, b, result, UINT32_C(0x7F800000), UINT32_C(0xFFC00000));
}

inline double generated(double a, double b, double result)
{
	return generated(a, b, result, UINT64_C(0x7FF0000000000000), UINT64_C(0xFFF8000000000000));
}
}
