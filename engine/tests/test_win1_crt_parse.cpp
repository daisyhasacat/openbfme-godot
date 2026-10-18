// OpenBFME unit tests (lane WIN-1). GPL-3.0.
//
// Cross-OS lockstep: the simulation read its INI numbers through the HOST C runtime's text-to-number conversions (INI::scanReal's sscanf("%f"), the
// RwGrammar float probe and strtof / strtod in the module parsers). A Windows build reaches the UCRT, a Linux build glibc, and they differ (Wine's UCRT
// strtof / sscanf are not correctly rounded on hard inputs). Lane WIN-1 replaced every one of those calls: INI reals are read the way RETAIL reads
// them, with MSVCR71's own converter ported (Common/INI/Msvcr71Real.h, tests/test_win1_msvcr71_real.cpp), and the correctly rounded reading
// scanRealText / strtodPortable (Common/INI/HostRealText.h, the pinned FloatFromChars parser) is what the port uses for a double retail does not read
// through the CRT. This file checks the scanRealText integration on every build; tools/sim/compare_float_chars.cpp independently checks the parser
// against the standard library. The host CRT is only compared for information.
//
// The vectors test at the end compares every platform against tests/data/win1_real_vectors.txt: expected bits computed by an independent exact
// oracle (tools/sim/gen_real_vectors.py, Python Fractions, no engine / fast_float / C++ library code), so nothing is derived from the code under test.
// Run it natively (Windows MSVC and macOS, where the coordinator's Devin boxes report):
//   build the openbfme_tests target (build.bat / cmake as docs/WINDOWS.md and docs/MACOS.md say), then from the repository root
//   engine/build/openbfme_tests -tc="win1 crt parse*"            (Windows: engine\build\openbfme_tests.exe -tc="win1 crt parse*")
// and send back the doctest totals. OPENBFME_TEST_DATA_DIR is compiled in; no retail files are needed.

#include "doctest.h"

#include "Common/INI/HostRealText.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <system_error>
#include <vector>

namespace
{
std::uint32_t bitsOf(float f)
{
	std::uint32_t u;
	std::memcpy(&u, &f, sizeof u);
	return u;
}

std::uint64_t bitsOf(double d)
{
	std::uint64_t u;
	std::memcpy(&u, &d, sizeof u);
	return u;
}

} // namespace

#if !defined(__APPLE__) // macOS before 26 has no floating-point std::from_chars: there the frozen vectors (below) are the oracle
namespace
{
// the decimal texts an INI file can hold, the hard ones included: short and long mantissas, signs, leading / trailing zeros, exponents across the
// whole range (subnormals and overflow to the boundary excluded: the INI values are not there, and an implementation may flag ERANGE differently),
// exact halfway points between adjacent floats and doubles written out in full
std::vector<std::string> corpus()
{
	std::vector<std::string> out = { "0", "-0", "1", "0.1", "0.2", "0.3", "1.5", "-2.25", "100", "0.005", "3.4028234e38", "1.17549435e-38", "16777217",
		"16777216.5", "33554431", "0.000001", "123456789", "9007199254740993", "2.2250738585072014e-308", "1e300", "0.30000000000000004",
		"1.00000005960464477539062500000000000000000001", "1.000000059604644775390625", "1.00000017881393432617187499", "7.038531e-26",
		"8.589973e9", "3.1415927", "2.7182817", "57.29578", "0.017453292", "30.0", "+4.5", "  12.75", "0012.50", "1.e5", ".5", "-.25e-3" };
	std::mt19937_64 rng(0x57494E31u); // "WIN1"
	for (int i = 0; i < 60000; ++i)
	{
		std::string s;
		if (rng() % 4 == 0)
		{
			s += '-';
		}
		const int intDigits = (int)(rng() % 10), fracDigits = (int)(rng() % 22);
		for (int d = 0; d < intDigits; ++d)
		{
			s += (char)('0' + rng() % 10);
		}
		if (intDigits == 0)
		{
			s += '0';
		}
		if (fracDigits > 0)
		{
			s += '.';
			for (int d = 0; d < fracDigits; ++d)
			{
				s += (char)('0' + rng() % 10);
			}
		}
		if (rng() % 3 == 0)
		{
			s += 'e' + std::to_string((int)(rng() % 70) - 35);
		}
		out.push_back(s);
	}
	// exact midpoints between neighbouring floats (they decide ties-to-even) and their one-ulp-of-text neighbours
	for (int i = 0; i < 4000; ++i)
	{
		const std::uint32_t u = (std::uint32_t)(rng() % 0x7E000000u) + 0x00800000u; // normal floats
		float lo;
		std::memcpy(&lo, &u, sizeof lo);
		const double mid = ((double)lo + (double)std::nextafter(lo, std::numeric_limits<float>::infinity())) / 2.0; // exact in binary64
		char buf[64];
		std::snprintf(buf, sizeof buf, "%.40g", mid);
		out.push_back(buf);
	}
	return out;
}

// the text after the skipped white space and a leading '+', as from_chars takes it (it accepts neither); every corpus text is otherwise in its grammar
const char *forFromChars(const std::string &s)
{
	const char *p = s.c_str();
	while (*p == ' ')
	{
		++p;
	}
	return *p == '+' ? p + 1 : p;
}
} // namespace

TEST_CASE("win1 crt parse: scanRealText (the simulation's sscanf(\"%f\") / strtof / strtod) is correctly rounded on every OS; the host CRT is compared for information")
{
	const std::vector<std::string> texts = corpus();
	size_t bad = 0, hostScanf = 0, hostStrtof = 0, hostStrtod = 0;
	for (const std::string &s : texts)
	{
		const char *ref = forFromChars(s);
		const char *end = ref + std::strlen(ref);
		float wantF = 0.0f;
		double wantD = 0.0;
		const std::from_chars_result rf = std::from_chars(ref, end, wantF);
		const std::from_chars_result rd = std::from_chars(ref, end, wantD);
		if (rf.ec == std::errc::result_out_of_range || rd.ec == std::errc::result_out_of_range)
		{
			continue; // outside the range of the type: not an INI value
		}
		REQUIRE_MESSAGE((rf.ec == std::errc() && rf.ptr == end), s);
		REQUIRE_MESSAGE((rd.ec == std::errc() && rd.ptr == end), s);
		float gotF = -1.0f;
		double gotD = -1.0;
		const char *endF = scanRealText(s.c_str(), gotF);
		const char *endD = scanRealText(s.c_str(), gotD);
		const bool ok = endF == s.c_str() + s.size() && endD == endF && bitsOf(gotF) == bitsOf(wantF) && bitsOf(gotD) == bitsOf(wantD);
		if (!ok && ++bad <= 5)
		{
			MESSAGE("scanRealText of '" << s << "' is not the correctly rounded value (or not the whole text)");
		}
		// the host C runtime, which the simulation no longer calls: glibc agrees everywhere, Wine's UCRT strtof / sscanf miss hard cases
		float viaScanf = 0.0f;
		hostScanf += std::sscanf(s.c_str(), "%f", &viaScanf) != 1 || bitsOf(viaScanf) != bitsOf(wantF) ? 1 : 0;
		hostStrtof += bitsOf(std::strtof(s.c_str(), nullptr)) != bitsOf(wantF) ? 1 : 0;
		hostStrtod += bitsOf(std::strtod(s.c_str(), nullptr)) != bitsOf(wantD) ? 1 : 0;
	}
	MESSAGE(texts.size() << " texts; the host CRT misses the correctly rounded value in sscanf %f " << hostScanf << ", strtof " << hostStrtof << ", strtod "
						  << hostStrtod << " of them (information: the simulation does not call it)");
	CHECK(bad == 0);
}
#endif

TEST_CASE("win1 crt parse: scanRealText reads the prefix sscanf / strtod read (it stops at the first character outside the number)")
{
	// the strtod prefix: "0x10" is the decimal 0 (no hex in MSVCR71's grammar, nor here); strtofPortable is retail's sscanf "%f" (Msvcr71Real), which
	// fails where no digit starts the number too
	struct Case
	{
		const char *text;
		float value;
		int used; // characters consumed; -1: no number
	};
	const Case cases[] = { { "1.0,", 1.0f, 3 }, { "2.5f", 2.5f, 3 }, { "-0.5x", -0.5f, 4 }, { "7.", 7.0f, 2 }, { "8.25%", 8.25f, 4 }, { "1e5e5", 1e5f, 3 },
		{ "1.5.5", 1.5f, 3 }, { "  +4.5", 4.5f, 6 }, { "3e2x", 300.0f, 3 }, { "6E-1,", 0.6f, 4 }, { "3e", 3.0f, 1 }, { "4e+", 4.0f, 1 }, { ".5", 0.5f, 2 },
		{ "inf", HUGE_VALF, 3 }, { "-INFINITY", -HUGE_VALF, 9 }, { "infin", HUGE_VALF, 3 }, { "1e50", HUGE_VALF, 4 }, { "-1e-60", -0.0f, 6 },
		{ "0x10", 0.0f, 1 }, { "nan", 0.0f, -1 }, { "-", 0.0f, -1 }, { ".", 0.0f, -1 }, { "e5", 0.0f, -1 }, { "", 0.0f, -1 }, { "+-1", 0.0f, -1 },
		{ "1e-45", 1e-45f, 5 }, { "3.4028235e38", 3.4028235e38f, 12 } };
	for (const Case &c : cases)
	{
		INFO(c.text);
		float v = 12345.0f;
		const char *e = scanRealText(c.text, v);
		if (c.used < 0)
		{
			CHECK(e == nullptr);
			CHECK(v == 12345.0f);
			char *se = nullptr;
			CHECK(strtofPortable(c.text, &se) == 0.0f);
			CHECK(se == c.text);
		}
		else
		{
			REQUIRE(e != nullptr);
			CHECK(e - c.text == c.used);
			CHECK(bitsOf(v) == bitsOf(c.value));
		}
	}
}

TEST_CASE("win1 crt parse (Sol r1): scanRealText rounds a float subnormal once, straight from the text, and tells underflow from overflow")
{
	// 7.0064923216240854e-46 lies just above 2^-150, half the smallest float subnormal: correctly rounded it is 0x00000001. Read as a double first it
	// became exactly 2^-150 (the nearest double), whose float conversion is a tie that rounds to even, zero: the double rounding lane WIN-1 r1 had.
	float f = -1.0f;
	REQUIRE(scanRealText("7.0064923216240854e-46", f) != nullptr);
	CHECK(bitsOf(f) == 0x00000001u);
	REQUIRE(scanRealText("-7.0064923216240854e-46", f) != nullptr);
	CHECK(bitsOf(f) == 0x80000001u);
	REQUIRE(scanRealText("7.006492321624085e-46", f) != nullptr); // just below 2^-150: zero
	CHECK(bitsOf(f) == 0x00000000u);
	REQUIRE(scanRealText("1.1754942e-38", f) != nullptr); // the largest subnormal
	CHECK(bitsOf(f) == 0x007FFFFFu);
	// out of range: the sign of the decimal exponent decides, for a text far beyond either end and one just beyond
	REQUIRE(scanRealText("1e-60", f) != nullptr);
	CHECK(bitsOf(f) == 0x00000000u);
	REQUIRE(scanRealText("-0.000001e-50", f) != nullptr);
	CHECK(bitsOf(f) == 0x80000000u);
	REQUIRE(scanRealText("3.5e38", f) != nullptr);
	CHECK(bitsOf(f) == 0x7F800000u);
	REQUIRE(scanRealText("-1000000e35", f) != nullptr);
	CHECK(bitsOf(f) == 0xFF800000u);
	double d = -1.0;
	REQUIRE(scanRealText("4.9406564584124654e-324", d) != nullptr); // the smallest double subnormal
	CHECK(bitsOf(d) == 0x0000000000000001ull);
	REQUIRE(scanRealText("1e-400", d) != nullptr);
	CHECK(bitsOf(d) == 0x0000000000000000ull);
	REQUIRE(scanRealText("1e400", d) != nullptr);
	CHECK(bitsOf(d) == 0x7FF0000000000000ull);
}

namespace
{
struct RealVector
{
	std::string category, text;
	int end = -1;
	std::uint32_t fbits = 0;
	std::uint64_t dbits = 0;
	bool fRange = false, dRange = false;
};

std::string hexDecode(const std::string &hex)
{
	REQUIRE(hex.size() % 2 == 0);
	std::string out;
	for (size_t i = 0; i < hex.size(); i += 2)
	{
		out += (char)std::stoi(hex.substr(i, 2), nullptr, 16);
	}
	return out;
}

std::vector<std::string> splitTabs(const std::string &line)
{
	std::vector<std::string> out;
	size_t from = 0;
	for (;;)
	{
		const size_t t = line.find('\t', from);
		out.push_back(line.substr(from, t == std::string::npos ? std::string::npos : t - from));
		if (t == std::string::npos)
		{
			return out;
		}
		from = t + 1;
	}
}
} // namespace

TEST_CASE("win1 crt parse: scanRealText and FloatFromChars match the frozen independent vectors (every platform; no expectation comes from the code under test)")
{
	const std::string path = std::string(OPENBFME_TEST_DATA_DIR) + "/win1_real_vectors.txt";
	std::ifstream in(path, std::ios::binary);
	REQUIRE_MESSAGE(in.good(), "missing vector file " << path);
	std::string line;
	size_t count = 0;
	std::uint64_t digest = 0xCBF29CE484222325ull, wantDigest = 0;
	std::map<std::string, size_t> wantCategories, gotCategories;
	std::vector<RealVector> vectors;
	bool haveCount = false, haveDigest = false;
	while (std::getline(in, line))
	{
		if (!line.empty() && line.back() == '\r')
		{
			FAIL("the vector file has CR line ends: " << path);
		}
		if (line.empty() || line[0] == '#')
		{
			continue;
		}
		if (line.rfind("count ", 0) == 0)
		{
			count = (size_t)std::stoull(line.substr(6));
			haveCount = true;
		}
		else if (line.rfind("fnv1a64 ", 0) == 0)
		{
			wantDigest = std::stoull(line.substr(8), nullptr, 16);
			haveDigest = true;
		}
		else if (line.rfind("category ", 0) == 0)
		{
			const size_t sp = line.rfind(' ');
			wantCategories[line.substr(9, sp - 9)] = (size_t)std::stoull(line.substr(sp + 1));
		}
		else
		{
			for (const char c : line + "\n")
			{
				digest = (digest ^ (unsigned char)c) * 0x100000001B3ull;
			}
			const std::vector<std::string> f = splitTabs(line);
			REQUIRE_MESSAGE(f.size() == 7, line.substr(0, 80));
			RealVector v;
			v.category = f[0];
			v.text = hexDecode(f[1]);
			v.end = std::stoi(f[2]);
			if (v.end >= 0)
			{
				v.fbits = (std::uint32_t)std::stoul(f[3], nullptr, 16);
				v.dbits = std::stoull(f[4], nullptr, 16);
				v.fRange = f[5] == "R";
				v.dRange = f[6] == "R";
			}
			++gotCategories[v.category];
			vectors.push_back(std::move(v));
		}
	}
	// the file must be the whole file: header counts, digest and every named category present (an emptied or truncated file fails here)
	REQUIRE(haveCount);
	REQUIRE(haveDigest);
	REQUIRE(count == 1932);
	REQUIRE(vectors.size() == count);
	REQUIRE(digest == wantDigest);
	const std::map<std::string, size_t> inventory = { { "exact_d", 22 }, { "exact_f", 22 }, { "long", 5 }, { "midpoint_d", 66 }, { "midpoint_f", 316 }, { "overflow_d", 3 }, { "overflow_f", 3 }, { "plain", 48 }, { "random", 1200 }, { "range", 35 }, { "special", 45 }, { "syntax", 17 }, { "wide", 150 } };
	REQUIRE(gotCategories == inventory); // every category by name with its exact count
	REQUIRE(wantCategories == inventory);
	size_t noNumber = 0, rangeF = 0, rangeD = 0, direct = 0;
	for (const RealVector &v : vectors)
	{
		INFO(v.category << " text(" << v.text.size() << " bytes) " << v.text.substr(0, 60));
		float f = 12345.0f; // an untouched output keeps these bits
		double d = 12345.0;
		const char *ef = scanRealText(v.text.c_str(), f);
		const char *ed = scanRealText(v.text.c_str(), d);
		if (v.end < 0)
		{
			++noNumber;
			CHECK(ef == nullptr);
			CHECK(ed == nullptr);
			CHECK(bitsOf(f) == bitsOf(12345.0f));
			CHECK(bitsOf(d) == bitsOf(12345.0));
			continue;
		}
		REQUIRE(ef != nullptr);
		REQUIRE(ed != nullptr);
		CHECK(ef - v.text.c_str() == v.end);
		CHECK(ed - v.text.c_str() == v.end);
		CHECK(bitsOf(f) == v.fbits);
		CHECK(bitsOf(d) == v.dbits);
		rangeF += v.fRange;
		rangeD += v.dRange;
		// the caller-visible result of FloatFromChars itself (the Lua, Apt and INI callers): the span of the number without white space and '+'
		const char *b = v.text.c_str();
		while (std::isspace((unsigned char)*b))
		{
			++b;
		}
		b += *b == '+';
		const char *digitsAt = b + (*b == '-');
		if (*digitsAt == 'i' || *digitsAt == 'I')
		{
			continue; // inf is read by scanRealText's own code, not by the parser
		}
		const char *e = v.text.c_str() + v.end;
		float pf = 12345.0f;
		double pd = 12345.0;
		const std::from_chars_result rf = FloatFromChars(b, e, pf);
		const std::from_chars_result rd = FloatFromChars(b, e, pd);
		++direct;
		CHECK(rf.ptr == e);
		CHECK(rd.ptr == e);
		CHECK((rf.ec == std::errc::result_out_of_range) == v.fRange);
		CHECK((rd.ec == std::errc::result_out_of_range) == v.dRange);
		if (!v.fRange)
		{
			// on a range error the standard requires the destination to stay unmodified, but implementations differ (libstdc++ keeps it, libc++ and
			// fast_float store 0 or infinity): the exclusion accommodates that, and the caller-visible results (ec, end offset) are checked above
			CHECK(bitsOf(pf) == v.fbits);
		}
		if (!v.dRange)
		{
			CHECK(bitsOf(pd) == v.dbits);
		}
	}
	// the exact inventory of the checks that carried weight: a vector set that stopped exercising them would fail here
	CHECK(noNumber == 29);
	CHECK(rangeF == 430);
	CHECK(rangeD == 26);
	CHECK(direct == 1873);
}
