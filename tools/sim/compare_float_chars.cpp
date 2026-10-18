// OpenBFME. GPL-3.0. Independent parser compatibility gate, built separately from engine tests.
#include "Common/FloatFromChars.h"
#include <charconv>
#include <type_traits>
#include <cfenv>
#include <clocale>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

template <class T> std::uint64_t bits(T value)
{
    using U = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
    U out;
    std::memcpy(&out, &value, sizeof(out));
    return out;
}

template <class T> bool compare(const std::string &s)
{
    T reference = 0, candidate = 0;
    const auto a = std::from_chars(s.data(), s.data() + s.size(), reference, std::chars_format::general);
    const auto b = FloatFromChars(s.data(), s.data() + s.size(), candidate);
    // the standard leaves the value unspecified on result_out_of_range (libstdc++ keeps it, libc++ and fast_float store 0 / inf): there the
    // error code and the end must agree; the callers decide the out-of-range value themselves
    const bool outOfRange = a.ec == std::errc::result_out_of_range;
    if ((outOfRange || bits(reference) == bits(candidate)) && a.ec == b.ec && a.ptr == b.ptr)
        return true;
    std::printf("MISMATCH type=%s input=\"%s\" length=%zu initial=+0\n", sizeof(T) == 4 ? "float" : "double", s.c_str(), s.size());
    std::printf("stdlib     bits=0x%016llx value=%a end=%td ec=%d\n", static_cast<unsigned long long>(bits(reference)), static_cast<double>(reference), a.ptr - s.data(), static_cast<int>(a.ec));
    std::printf("fast_float bits=0x%016llx value=%a end=%td ec=%d\n", static_cast<unsigned long long>(bits(candidate)), static_cast<double>(candidate), b.ptr - s.data(), static_cast<int>(b.ec));
    return false;
}

std::uint64_t state = 0x4d414335b17d1ffULL;
std::uint64_t nextBits()
{
    std::uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

std::uint64_t count = 0, digest = 14695981039346656037ULL;
bool check(const std::string &s, const char *category)
{
    ++count;
    for (unsigned char c : s) digest = (digest ^ c) * 1099511628211ULL;
    digest = (digest ^ 0xff) * 1099511628211ULL;
    if (compare<double>(s) && compare<float>(s)) return true;
    std::printf("STOP index=%llu category=%s corpus_digest=%016llx\n", (unsigned long long)count, category, (unsigned long long)digest);
    std::ofstream out("float-chars-mismatch.input", std::ios::binary);
    out.write(s.data(), static_cast<std::streamsize>(s.size()));
    if (!out) std::fputs("ERROR: could not save exact mismatch input\n", stderr);
    return false;
}

int main(int argc, char **argv)
{
    // usage: compare_float_chars <corpus.bin> <records> <fnv1a64 hex> [--corpus-only]   (the last two come from the corpus manifest, float_chars_corpus.py)
    if ((argc != 4 && !(argc == 5 && std::strcmp(argv[4], "--corpus-only") == 0)) || !std::setlocale(LC_ALL, "C") || std::fegetround() != FE_TONEAREST)
    {
        std::fputs("usage: compare_float_chars <corpus.bin> <records> <fnv1a64 hex> [--corpus-only]\n", stderr);
        return 2;
    }
    char *tail = nullptr;
    const unsigned long long wantRecords = std::strtoull(argv[2], &tail, 10);
    const unsigned long long wantDigest = std::strtoull(argv[3], nullptr, 16);
#ifdef _LIBCPP_VERSION
    std::printf("libcxx_version=%d\n", _LIBCPP_VERSION);
#endif
    std::printf("rounding=%d seed=%016llx\n", std::fegetround(), (unsigned long long)state);
    std::ifstream in(argv[1], std::ios::binary);
    if (!in || *tail != '\0' || wantRecords == 0)
    {
        std::fputs("ERROR: the corpus is missing or the record count is not a positive number\n", stderr);
        return 2;
    }
    while (in.peek() != EOF)
    {
        unsigned char len[4];
        if (!in.read(reinterpret_cast<char *>(len), 4)) return 2;
        const std::uint32_t size = len[0] | (std::uint32_t(len[1]) << 8) | (std::uint32_t(len[2]) << 16) | (std::uint32_t(len[3]) << 24);
        std::string s(size, '\0');
        if (!in.read(s.data(), size)) return 2;
        if (!check(s, "project_literals_and_edges")) return 1;
    }
    // the corpus must be the frozen one: a truncated, emptied or substituted file is an error, not a smaller test
    if (count != wantRecords || digest != wantDigest)
    {
        std::printf("ERROR: corpus has %llu records, digest %016llx; the manifest says %llu, %016llx\n", (unsigned long long)count, (unsigned long long)digest,
                    wantRecords, (unsigned long long)wantDigest);
        return 2;
    }
    std::printf("PASS project_literals_and_edges=%llu\n", (unsigned long long)count);
    if (argc == 5) return 0;
    const auto edgeCount = count;
    for (std::uint64_t i = 0; i < 2500000; ++i)
    {
        std::uint64_t dBits;
        do { dBits = nextBits(); } while ((dBits & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL);
        std::uint32_t fBits;
        do { fBits = static_cast<std::uint32_t>(nextBits()); } while ((fBits & 0x7f800000U) == 0x7f800000U);
        double d;
        float f;
        std::memcpy(&d, &dBits, sizeof d);
        std::memcpy(&f, &fBits, sizeof f);
        char text[1024];
        int n = std::snprintf(text, sizeof text, "%.17g", d);
        if (n < 0 || std::size_t(n) >= sizeof text) return 2;
        if (!check(std::string(text, n), "random_double_17g")) return 1;
        n = std::snprintf(text, sizeof text, "%.9g", static_cast<double>(f));
        if (n < 0 || std::size_t(n) >= sizeof text) return 2;
        if (!check(std::string(text, n), "random_float_9g")) return 1;
        auto r = std::to_chars(text, text+sizeof text, d);
        if (r.ec != std::errc()) return 2;
        if (!check(std::string(text, r.ptr), "random_double_shortest")) return 1;
        r = std::to_chars(text, text+sizeof text, f);
        if (r.ec != std::errc()) return 2;
        if (!check(std::string(text, r.ptr), "random_float_shortest")) return 1;
    }
    std::printf("PASS strings=%llu random_strings=%llu comparisons=%llu digest=%016llx\n", (unsigned long long)count, (unsigned long long)(count-edgeCount), (unsigned long long)(count*2), (unsigned long long)digest);
}
