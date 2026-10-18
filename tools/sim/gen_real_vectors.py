#!/usr/bin/env python3
"""OpenBFME GPL-3.0: freeze the independent expected values of engine/tests/test_win1_crt_parse.cpp.

The oracle here shares no code with the engine, with fast_float or with the C++ library: a decimal text becomes an exact Fraction and is rounded
to nearest-even into float32 / float64 bits by the arithmetic below. The grammar is the one documented at scanRealText (Common/INI/HostRealText.h):
white space, a sign, digits with one point, an exponent only when digits follow it, "inf" / "infinity" in any case; no hex, no nan.
A result beyond the type's range is +-infinity, one that rounds to zero is +-0 (the caller-visible values of scanRealText).

Line format of the output (after the header lines), tab separated:
  category, hex(input bytes), end, float_bits, double_bits, float_range, double_range
  end          offset after the number, or -1 when there is no number (the output is then untouched)
  *_bits       the stored bits in hex; "-" when there is no number
  *_range      "R" when the text is a non-zero number whose result is out of range (C++ from_chars reports result_out_of_range), else "-"
Header: `count N`, `fnv1a64 H` (over the data lines, each with its newline) and `category NAME COUNT` per category: the test refuses a file whose
counts or digest disagree (a truncated or emptied file fails).
Regenerate:  python3 tools/sim/gen_real_vectors.py engine/tests/data/win1_real_vectors.txt
"""
import random
import sys
from fractions import Fraction

FORMATS = {"f": (8, 23), "d": (11, 52)}  # exponent bits, fraction bits
DIGITS = b"0123456789"
SPACE = b" \t\n\v\f\r"


def round_nearest_even(q: Fraction) -> int:
    fl, rem = divmod(q.numerator, q.denominator)
    twice = 2 * rem
    if twice > q.denominator or (twice == q.denominator and fl & 1):
        fl += 1
    return fl


def encode(value: Fraction, negative: bool, kind: str):
    """exact value >= 0 -> (bits, out_of_range): round to nearest even, overflow to infinity, a non-zero value rounding to zero is out of range"""
    ebits, fbits = FORMATS[kind]
    bias = (1 << (ebits - 1)) - 1
    sign = (1 << (ebits + fbits)) if negative else 0
    if value == 0:
        return sign, False
    e = value.numerator.bit_length() - value.denominator.bit_length()
    if Fraction(2) ** e > value:
        e -= 1
    e = max(e, 1 - bias)
    m = round_nearest_even(value / Fraction(2) ** (e - fbits))
    if m >= (1 << (fbits + 1)):  # rounded up into the next binade
        m >>= 1
        e += 1
    if m < (1 << fbits):  # subnormal or zero
        return sign | m, m == 0
    if e > bias:
        return sign | (((1 << ebits) - 1) << fbits), True
    return sign | ((e + bias) << fbits) | (m - (1 << fbits)), False


def parse(text: bytes):
    """-> None (no number) or (end, negative, digits, exp10, is_inf)"""
    p = 0
    while p < len(text) and text[p] in SPACE:
        p += 1
    negative = text[p:p + 1] == b"-"
    if text[p:p + 1] in (b"+", b"-"):
        p += 1
    low = text[p:p + 8].lower()
    if low[:3] == b"inf":
        return (p + 8 if low == b"infinity" else p + 3), negative, None, 0, True
    digits = b""
    point = None
    q = p
    while q < len(text):
        c = text[q:q + 1]
        if c in (b"0", b"1", b"2", b"3", b"4", b"5", b"6", b"7", b"8", b"9"):
            digits += c
        elif c == b"." and point is None:
            point = len(digits)
        else:
            break
        q += 1
    if not digits:
        return None
    exp = 0
    if text[q:q + 1] in (b"e", b"E"):
        r = q + 1
        eneg = text[r:r + 1] == b"-"
        if text[r:r + 1] in (b"+", b"-"):
            r += 1
        s = r
        while text[r:r + 1] in (b"0", b"1", b"2", b"3", b"4", b"5", b"6", b"7", b"8", b"9"):
            r += 1
        if r > s:
            exp = int(text[s:r])
            exp = -exp if eneg else exp
            q = r
    frac_digits = len(digits) - (point if point is not None else len(digits))
    return q, negative, digits.decode(), exp - frac_digits, False


def expected(text: bytes):
    got = parse(text)
    if got is None:
        return -1, None, None, False, False
    end, negative, digits, exp10, is_inf = got
    out = {}
    for kind in "fd":
        ebits, fbits = FORMATS[kind]
        if is_inf:
            out[kind] = ((1 << (ebits + fbits) if negative else 0) | (((1 << ebits) - 1) << fbits), False)
        elif int(digits) == 0:
            out[kind] = encode(Fraction(0), negative, kind)
        elif exp10 > 100000:  # beyond both ranges whatever the digits (the inputs here stay under 4000 digits)
            out[kind] = encode(Fraction(2) ** 5000, negative, kind)
        elif exp10 < -100000:
            out[kind] = encode(Fraction(1, 2 ** 5000), negative, kind)
        else:
            out[kind] = encode(Fraction(int(digits)) * Fraction(10) ** exp10, negative, kind)
    return end, out["f"][0], out["d"][0], out["f"][1], out["d"][1]


def exact_value(bits: int, kind: str) -> Fraction:
    ebits, fbits = FORMATS[kind]
    bias = (1 << (ebits - 1)) - 1
    exp, frac = divmod(bits, 1 << fbits)
    if exp == 0:
        return Fraction(frac) * Fraction(2) ** (1 - bias - fbits)
    return Fraction(frac + (1 << fbits)) * Fraction(2) ** (exp - bias - fbits)


def decimal_text(value: Fraction, offset: int) -> str:
    """the exact decimal expansion of a dyadic rational, `offset` units of the last digit added"""
    k = value.denominator.bit_length() - 1
    assert value.denominator == 1 << k
    scale = k + 2
    digits = str(value.numerator * 5 ** k * 100 + offset).rjust(scale + 1, "0")
    return digits[:-scale] + "." + digits[-scale:]


def build():
    rng = random.Random(0x57494E31)
    cases = []

    def add(category, text):
        cases.append((category, text.encode("latin-1") if isinstance(text, str) else text))

    for t in ["0", "-0", "+0", "0.0", "-0.0e5", "1", "-1", "0.1", "0.2", "0.3", "1.5", "-2.25", "100", "0.005", "16777217", "16777216.5",
              "33554431", "9007199254740993", "9007199254740992.5", "0.30000000000000004", "3.4028234e38", "3.4028235e38", "1.17549435e-38",
              "2.2250738585072014e-308", "1.7976931348623157e308", "1.7976931348623158e308", "1e300", "1e-300", "00012.50", "1.e5", ".5",
              "-.25e-3", "5.", "1e5", "1E5", "1e+5", "1e-5", "12.75abc", "7e", "7e+", "7e-", "7ex", "7.e", "1.2.3", "0x10", "0X1p3", "1_000", "1,5"]:
        add("plain", t)
    for t in ["  12.75", "\t\n\v\f\r 3", "+4.5", "+-4", "-+4", "- 4", "+", "-", ".", "-.", "+.e5", "e5", "", "   ", "abc", ".e5", "-.e"]:
        add("syntax", t)
    for sign in ["", "-", "+"]:
        for w in ["inf", "INF", "Inf", "infinity", "INFINITY", "InFiNiTy", "infin", "infinit", "infinityx", "infx", "in", "i", "nan", "NAN",
                  "nan(1)"]:
            add("special", sign + w)
    for t in ["1e38", "3.40282347e38", "3.4028235677973362e38", "3.4028235677973366e38", "1e39", "1e309", "1.797693134862315708e308",
              "1.797693134862315807e308", "1e400", "-1e400", "1e5000", "-1e5000", "1e99999", "1e999999999", "0e99999", "0.000e-99999", "1e-99999",
              "-1e-99999", "1e-400", "-1e-400", "1e-46", "1e-45", "1.4e-45", "7.00649232162408535e-46", "7.0064923216240853e-46", "1e-324",
              "4.9e-324", "2.4703282292062327e-324", "2.4703282292062328e-324", "2.5e-324", "1e-310", "1.17549421e-38", "1.1754943e-38"]:
        add("range", t)
    add("range", "9" * 400 + "e-400")
    add("range", "0." + "0" * 400 + "1e400")
    add("long", "1" + "0" * 300)
    add("long", "0." + "0" * 300 + "1")
    add("long", "1." + "0" * 800 + "1")
    add("long", "9" * 1500)
    add("long", "0." + "0" * 1500 + "1" + "0" * 1500 + "1e1500")
    # extreme bit patterns of both types: exact midpoints (ties-to-even decide), one decimal unit either side, and the exact values
    for kind in "fd":
        ebits, fbits = FORMATS[kind]
        maxexp = (1 << ebits) - 2
        patterns = {0, 1, 2, (1 << fbits) - 1, 1 << fbits, (1 << fbits) + 1, (maxexp << fbits) - 1, maxexp << fbits,
                    (maxexp << fbits) | ((1 << fbits) - 2), (maxexp << fbits) | ((1 << fbits) - 2) - 1}
        patterns.update(rng.randrange(maxexp << fbits) for _ in range(12))
        for bits in sorted(patterns):
            lo = exact_value(bits, kind)
            mid = (lo + exact_value(bits + 1, kind)) / 2
            for off in (-1, 0, 1):
                add("midpoint_" + kind, decimal_text(mid, off))
            add("exact_" + kind, decimal_text(lo, 0))
    # the largest finite value's midpoint to the next power of two: the overflow threshold of each type
    for kind in "fd":
        ebits, fbits = FORMATS[kind]
        top = ((1 << ebits) - 2) << fbits | ((1 << fbits) - 1)
        mid = (exact_value(top, kind) + exact_value(top, kind) + Fraction(2) ** ((1 << (ebits - 1)) - 1 - fbits)) / 2
        for off in (-1, 0, 1):
            add("overflow_" + kind, decimal_text(mid, off))
    for _ in range(1200):
        s = "-" if rng.random() < 0.25 else ""
        s += "".join(rng.choice("0123456789") for _ in range(rng.randrange(0, 12))) or "0"
        if rng.random() < 0.8:
            s += "." + "".join(rng.choice("0123456789") for _ in range(rng.randrange(1, 24)))
        if rng.random() < 0.5:
            s += "e" + str(rng.randrange(-60, 60))
        add("random", s)
    for _ in range(150):
        add("wide", "".join(rng.choice("0123456789") for _ in range(rng.randrange(1, 40))) + "e" + str(rng.randrange(-340, 320)))
    for _ in range(250):
        bits = rng.randrange(0x00800000, 0x7F000000)
        add("midpoint_f", decimal_text((exact_value(bits, "f") + exact_value(bits + 1, "f")) / 2, rng.choice((-1, 0, 1))))
    return cases


def render(cases):
    body, counts = [], {}
    for category, text in cases:
        end, fb, db, fr, dr = expected(text)
        if end < 0:
            row = "\t".join([category, text.hex(), "-1", "-", "-", "-", "-"])
        else:
            row = "\t".join([category, text.hex(), str(end), "%08x" % fb, "%016x" % db, "R" if fr else "-", "R" if dr else "-"])
        body.append(row)
        counts[category] = counts.get(category, 0) + 1
    digest = 0xCBF29CE484222325
    for row in body:
        for c in (row + "\n").encode():
            digest = ((digest ^ c) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    head = ["# win1 real vectors: independent oracle, tools/sim/gen_real_vectors.py (its header gives the format)", "count %d" % len(body),
            "fnv1a64 %016x" % digest] + ["category %s %d" % (n, counts[n]) for n in sorted(counts)]
    return "\n".join(head + body) + "\n"


if __name__ == "__main__":
    with open(sys.argv[1], "w", newline="\n") as f:
        f.write(render(build()))
