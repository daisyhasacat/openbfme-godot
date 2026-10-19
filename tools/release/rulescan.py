#!/usr/bin/env python3
"""Exact fast paths for the privacy rules' scans (PRIVSPEED-1: the v0.4.9 publish's privacy gate ran 1-2+ hours, ~8 s per MB of binary:
every rule's regex over every one of ~28 decodings of every member).

finditer(rx, text) yields exactly the matches of rx.finditer(text), the same match objects in the same order, but runs the regex only
where the regex's own structure proves a match can be (a proof, never a heuristic; anything the analysis does not understand is
scanned whole, as before):

* every match of the rule contains one of a set of literal strings (the cover: required literals read from the parsed pattern, every
  alternative of a branch has its own), found case-insensitively in fold(text), a copy of the text with every character that IGNORECASE
  matches to an ASCII letter turned into that letter (computed from the running Python's re module, never from a table);
* 'cand': where each literal sits at a bounded offset from the match's start (`192.168.` at 0, a word rule's words at 0, a user rule's
  at 0 or 1): the regex is tried (rx.match, the whole text, no end position) only at those starts, and finditer's leftmost,
  non-overlapping choice is replayed over them;
* 'seg': otherwise, when some characters can never be consumed by the pattern (not by any of its atoms, lookarounds included) and, if it
  uses \\b / \\B, are no word characters, and it has no end anchor ($, \\Z): the text splits at those 'break' characters, no match
  crosses one, and the regex runs (rx.finditer(text, start, end), lookbehinds and \\b see the real text before start) only on the pieces
  that hold a literal of the cover; a break character at the end position behaves as the end of the text does;
* 'presence': otherwise the regex runs over the whole text, but only if a literal of the cover is in it;
* 'full': no cover, an empty-matching pattern, inline flags, back references or an unknown node: the whole text, as before.

tools/release/test_rulescan.py proves it against rx.finditer on random and planted corpora (every rule, every decoding)."""
from __future__ import annotations

import re
from collections.abc import Iterator

try:   # Python 3.11+
    from re import _compiler as _K, _constants as _C, _parser as _P
except ImportError:  # pragma: no cover
    import sre_compile as _K, sre_constants as _C, sre_parse as _P  # noqa: E401

INF = int(_C.MAXREPEAT)
CAND_WINDOW = 64        # 'cand' only when a literal's offset range in a match is at most this wide
_UNIVERSE: str | None = None
_SPECIALS: list[tuple[str, str]] | None = None
_SCANS: dict[tuple[str, int], "Scan"] = {}


def _universe() -> str:
    """every code point a str can hold (no surrogates: no decoding yields one)"""
    global _UNIVERSE
    if _UNIVERSE is None:
        _UNIVERSE = "".join(map(chr, range(0xD800))) + "".join(map(chr, range(0xE000, 0x110000)))
    return _UNIVERSE


def _specials() -> list[tuple[str, str]]:
    """(character, replacement) that fold() applies before str.lower(): every non-ASCII character that IGNORECASE (Unicode) matches to
    an ASCII letter but that does not lower() to it (U+0130, U+0131, U+017F in CPython 3.12 / 3.13), and every character whose lower()
    is not one character (U+0130), which would shift positions (replaced by U+FFFD, which matches no ASCII letter)"""
    global _SPECIALS
    if _SPECIALS is None:
        out: dict[str, str] = {}
        u = _universe()
        # every character some ASCII character's IGNORECASE literal matches (one pass: an alternation matches where any of them does)
        alt = re.compile("|".join(re.escape(chr(c)) for c in range(128)), re.I)
        for m in sorted({m.group(0) for m in alt.finditer(u)} - set(map(chr, range(128)))):
            for c in map(chr, range(128)):
                if re.fullmatch(re.escape(c), m, re.I) and m.lower() != c.lower():
                    out[m] = c.lower()
        for a in range(0, len(u), 4096):   # lower() never shortens a character: a piece keeps its length iff each of them does
            piece = u[a:a + 4096]
            if len(piece.lower()) != len(piece):
                for c in piece:
                    if len(c.lower()) != 1 and c not in out:
                        out[c] = "\ufffd"
        _SPECIALS = sorted(out.items())
    return _SPECIALS


def fold(text: str) -> str | None:
    """text with the same length, each character lower-cased, where every character that a case-insensitive pattern's ASCII letter
    matches is that letter in lower case (and ASCII non-letters match only themselves). None if that cannot be made (never in CPython:
    then the caller scans whole)."""
    if text.isascii():
        return text.lower()
    for c, r in _specials():
        if c in text:
            text = text.replace(c, r)
    out = text.lower()
    return out if len(out) == len(text) else None


class _Unsupported(Exception):
    pass


def _width(state, item) -> tuple[int, int]:
    lo, hi = _P.SubPattern(state, [item]).getwidth()
    return lo, (INF if hi >= INF else hi)


def _add(a: int, b: int) -> int:
    return INF if a >= INF or b >= INF else a + b


_ATOMS = (_C.LITERAL, _C.NOT_LITERAL, _C.ANY, _C.IN)
_REPEATS = (_C.MAX_REPEAT, _C.MIN_REPEAT, _C.POSSESSIVE_REPEAT)
_ZERO = (_C.AT, _C.ASSERT, _C.ASSERT_NOT)


def _edge_atom(item):
    """the one-character atom that necessarily consumes the first (and the last) character `item` consumes, or None: the item itself,
    or the body of a repeat of one atom with a minimum of 1"""
    op, av = item
    if op in _ATOMS:
        return item
    if op in _REPEATS and av[0] >= 1 and len(av[2].data) == 1 and av[2].data[0][0] in _ATOMS:
        return av[2].data[0]
    return None


def _neighbour(items, k: int, step: int):
    """the atom that consumes the character next to position k of the sequence (step -1: the one before items[k], +1: from items[k]
    on), zero-width items skipped; None if not one atom"""
    while 0 <= k < len(items) and items[k][0] in _ZERO:
        k += step
    return _edge_atom(items[k]) if 0 <= k < len(items) else None


def _cover(state, items) -> list[tuple[str, int, int, object, object]] | None:
    """[(literal, lo, hi, before, after)]: every match of the sequence `items` contains one of the (lower-case ASCII) literals starting
    lo..hi characters after the match's start (hi INF: unbounded), with the character just before it consumed by the atom `before` and
    the one just after it by `after` (None: not known); None if no such set is known. The most selective candidate (longest shortest
    literal, then neighbour atoms known, then bounded offsets) is kept."""
    items = list(getattr(items, "data", items))
    best, best_score = None, None
    pre_lo = pre_hi = 0
    run, run_lo, run_hi, run_at = "", 0, 0, 0

    def consider(cov):
        nonlocal best, best_score
        if not cov:
            return
        score = (min(6, min(len(c[0]) for c in cov)), all(c[3] is not None or c[4] is not None for c in cov),
                 all(c[2] < INF for c in cov), -len(cov))
        if best_score is None or score > best_score:
            best, best_score = cov, score

    def flush(k):
        consider([(run, run_lo, run_hi, _neighbour(items, run_at - 1, -1), _neighbour(items, k, 1))])

    for k, item in enumerate(items):
        op, av = item
        if op is _C.LITERAL and av < 128:
            if not run:
                run_lo, run_hi, run_at = pre_lo, pre_hi, k
            run += chr(av).lower()
        else:
            if run:
                flush(k)
                run = ""
            sub = None
            if op is _C.SUBPATTERN:
                if av[1] or av[2]:
                    raise _Unsupported("inline flags")
                sub = _cover(state, av[3])
            elif op is _C.ATOMIC_GROUP:
                sub = _cover(state, av)
            elif op is _C.BRANCH:
                parts = [_cover(state, b) for b in av[1]]
                if all(parts):
                    sub = [x for p in parts for x in p]
            elif op in _REPEATS and av[0] >= 1:
                sub = _cover(state, av[2])
            if sub:
                consider([(l, _add(pre_lo, a), _add(pre_hi, b), bef, aft) for l, a, b, bef, aft in sub])
        lo, hi = _width(state, item)
        pre_lo, pre_hi = _add(pre_lo, lo), _add(pre_hi, hi)
    if run:
        flush(len(items))
    return best


_END_ATS = {_C.AT_END, _C.AT_END_LINE, _C.AT_END_STRING}
_BOUNDARY_ATS = {_C.AT_BOUNDARY, _C.AT_NON_BOUNDARY, _C.AT_UNI_BOUNDARY, _C.AT_UNI_NON_BOUNDARY, _C.AT_LOC_BOUNDARY,
                 _C.AT_LOC_NON_BOUNDARY}
_START_ATS = {_C.AT_BEGINNING, _C.AT_BEGINNING_LINE, _C.AT_BEGINNING_STRING}


def _walk(items, atoms: list, flags: dict) -> None:
    """every consuming atom (anywhere, lookarounds included) into atoms; flags['end'] / flags['boundary'] when the pattern has an end
    anchor / a word boundary; _Unsupported for back references, conditionals, inline flags or a node this does not know"""
    for op, av in getattr(items, "data", items):
        if op in (_C.LITERAL, _C.NOT_LITERAL, _C.ANY, _C.IN):
            atoms.append((op, av))
        elif op is _C.AT:
            if av in _END_ATS:
                flags["end"] = True
            elif av in _BOUNDARY_ATS:
                flags["boundary"] = True
            elif av not in _START_ATS:
                raise _Unsupported(str(av))
        elif op is _C.SUBPATTERN:
            if av[1] or av[2]:
                raise _Unsupported("inline flags")
            _walk(av[3], atoms, flags)
        elif op is _C.ATOMIC_GROUP:
            _walk(av, atoms, flags)
        elif op is _C.BRANCH:
            for b in av[1]:
                _walk(b, atoms, flags)
        elif op in (_C.MAX_REPEAT, _C.MIN_REPEAT, _C.POSSESSIVE_REPEAT):
            _walk(av[2], atoms, flags)
        elif op in (_C.ASSERT, _C.ASSERT_NOT):
            _walk(av[1], atoms, flags)
        else:
            raise _Unsupported(str(op))


def class_re(chars: str, astral: bool) -> re.Pattern:
    """a one-character class of the Basic Multilingual Plane characters of `chars` (as ranges) and, with astral, of every character
    beyond it, without astral of none (re reads a class with ranges beyond U+FFFF range by range: 40x slower on a 100 MB text; a
    class here is either a superset or a subset of the set it stands for, as its use allows)"""
    cps = sorted(c for c in map(ord, chars) if c < 0x10000)
    parts, i = [], 0
    while i < len(cps):
        j = i
        while j + 1 < len(cps) and cps[j + 1] == cps[j] + 1:
            j += 1
        a, b = chr(cps[i]), chr(cps[j])
        parts.append(re.escape(a) if i == j else f"{re.escape(a)}-{re.escape(b)}")
        i = j + 1
    if astral:
        parts.append("\U00010000-\U0010ffff")
    return re.compile("[" + "".join(parts) + "]") if parts else re.compile("(?!)")


class Scan:
    """one rule's scan strategy (module docstring); build with scan_of()"""

    def __init__(self, rx: re.Pattern) -> None:
        self.rx = rx
        self.mode = "full"
        self.cover: list[tuple[str, int, int, re.Pattern | None, re.Pattern | None]] = []
        self._breaks: re.Pattern | None = None
        self._lazy = None   # makes _breaks when it is first needed (a 'cand' rule rarely needs it)
        try:
            self._analyse()
        except (_Unsupported, re.error, RecursionError, TypeError, ValueError):
            self.mode, self._breaks, self._lazy = "full", None, None

    @property
    def breaks(self) -> re.Pattern | None:
        """a class of characters no atom of the pattern can consume (and, with a word boundary, no word characters), or None"""
        if self._lazy is not None:
            make, self._lazy = self._lazy, None
            self._breaks = make()
        return self._breaks

    def _analyse(self) -> None:
        if self.rx.flags & (re.ASCII | re.LOCALE) or not isinstance(self.rx.pattern, str):
            return
        p = _P.parse(self.rx.pattern, self.rx.flags)
        if p.getwidth()[0] == 0:
            return   # a match can be empty: the whole text
        atoms: list = []
        flags: dict = {}
        _walk(p.data, atoms, flags)
        cover = _cover(p.state, p.data)
        if not cover:
            return
        state = p.state
        atom_rx: dict[str, re.Pattern] = {}

        def compiled(atom):   # a neighbour atom as a one-character pattern (the pattern's own flags)
            if atom is None:
                return None
            if repr(atom) not in atom_rx:
                atom_rx[repr(atom)] = _K.compile(_P.SubPattern(state, [atom]), state.flags)
            return atom_rx[repr(atom)]
        self.cover = [(lit, lo, hi, compiled(bef), compiled(aft)) for lit, lo, hi, bef, aft in cover]
        if all(h < INF and h - l <= CAND_WINDOW for _, l, h, _, _ in cover):
            self.mode = "cand"
        if not flags.get("end") and atoms:
            self._lazy = lambda: self._make_breaks(state, atoms, flags)
        if self.mode != "cand":
            self.mode = "seg" if self.breaks is not None else "presence"

    @staticmethod
    def _make_breaks(state, atoms: list, flags: dict) -> re.Pattern | None:
        """the break class (None: none, or the pattern could not be read for it: then no piece is cut)"""
        try:
            # every atom of the pattern on its own (deduplicated): the characters some atom consumes, exactly as it does in the pattern
            singles = list({repr(a): a for a in atoms}.values())
            alt = _P.SubPattern(state, [(_C.BRANCH, (None, [_P.SubPattern(state, [a]) for a in singles]))])
            consumable = _K.compile(alt, state.flags | re.DOTALL)
            rest = consumable.sub("", _universe()[:0xF800])   # the BMP's break characters (_universe() holds U+0000..U+FFFF
            # without the surrogates first; the breaks are BMP ones only: class_re)
            if flags.get("boundary"):
                rest = re.sub(r"\w", "", rest)
            # a subset of the break characters (the BMP's): still none can be consumed
            return class_re(rest, astral=False) if rest else None
        except (_Unsupported, re.error, RecursionError, TypeError, ValueError):
            return None

    def occurrences(self, text: str, folded: str, index: dict[str, list[int]] | None = None) -> list[tuple[int, int, int, int]]:
        """(position, literal length, lo, hi) of every (overlapping) occurrence of every cover literal whose neighbours match the
        literal's neighbour atoms (a match through it consumes them); index: literal_index(folded, ...) if the caller has one"""
        out = []
        for lit, lo, hi, bef, aft in self.cover:
            n = len(lit)
            for i in index[lit] if index is not None and lit in index else _positions(folded, lit):
                if (bef is None or (i > 0 and bef.match(text, i - 1))) and (aft is None or aft.match(text, i + n)):
                    out.append((i, n, lo, hi))
        return out

    def finditer(self, text: str, folded: str | None = None, index: dict[str, list[int]] | None = None) -> Iterator[re.Match]:
        """exactly self.rx.finditer(text); folded: fold(text), index: literal_index(text, ...) (with every literal of this rule), if the
        caller has them"""
        rx = self.rx
        if self.mode == "full":
            yield from rx.finditer(text)
            return
        if index is not None and any(c[0] not in index for c in self.cover):
            index = None
        if index is None and folded is None:
            folded = fold(text)
            if folded is None:
                yield from rx.finditer(text)
                return
        occ = self.occurrences(text, folded, index)
        if not occ:
            return
        if self.mode == "cand":
            starts = sorted({q for p, _, lo, hi in occ for q in range(max(0, p - hi), p - lo + 1)})
            if len(starts) <= max(4096, len(text) // 8):
                cur = 0
                for q in starts:
                    if q < cur:
                        continue
                    m = rx.match(text, q)
                    if m:
                        if m.end() == m.start():   # cannot happen (minimum width > 0), but never loop or skip on it
                            raise AssertionError("an empty match of a non-empty pattern")
                        yield m
                        cur = m.end()
                return
            if self.breaks is None:
                yield from rx.finditer(text)
                return
        if self.breaks is None:   # presence
            yield from rx.finditer(text)
            return
        brk, end = self.breaks, -1
        for p, n, _, _ in sorted(occ):
            if p < end:
                continue   # inside the piece already scanned
            a = self._last_break(text, p, max(end + 1, 0))
            m = brk.search(text, p + n)
            b = m.start() if m else len(text)
            if a < b:
                yield from rx.finditer(text, a, b)
            end = b

    def _last_break(self, text: str, p: int, floor: int) -> int:
        """the position after the last break character before p (floor: a position after a break, or 0, that the piece may start at)"""
        k = 256
        while True:
            lo = max(floor, p - k)
            last = None
            for last in self.breaks.finditer(text, lo, p):
                pass
            if last is not None:
                return last.end()
            if lo == floor:
                return floor
            k *= 8


def scan_of(rx: re.Pattern) -> Scan:
    key = (rx.pattern, rx.flags)
    s = _SCANS.get(key)
    if s is None:
        s = _SCANS[key] = Scan(rx)
    return s


def finditer(rx: re.Pattern, text: str, folded: str | None = None, index: dict[str, list[int]] | None = None) -> Iterator[re.Match]:
    """exactly rx.finditer(text) (module docstring)"""
    return scan_of(rx).finditer(text, folded, index)


def _positions(folded: str, lit: str) -> list[int]:
    out, i = [], folded.find(lit)
    while i >= 0:
        out.append(i)
        i = folded.find(lit, i + 1)
    return out


def _trie(words: list[str]) -> str:
    """a pattern that matches where one of the (non-empty) words starts: a word that is a prefix of another ends its branch"""
    by: dict[str, list[str]] = {}
    for w in words:
        by.setdefault(w[0], []).append(w[1:])
    alts = []
    for c, rests in sorted(by.items()):
        alts.append(re.escape(c) if "" in rests else re.escape(c) + "(?:" + _trie(sorted(set(rests))) + ")")
    return "|".join(alts)


_TRIES: dict[tuple[str, ...], tuple[re.Pattern, dict[str, list[str]]]] = {}


INDEX_CHUNK = 1 << 22   # literal_index folds this many characters at a time (a folded copy of a 100 MB form took 100s of MB per worker)


def literal_index(text: str, rxs) -> dict[str, list[int]] | None:
    """{literal: every position it starts at in fold(text)} for the cover literals of all the rules rxs, in one pass (a trie of them finds
    where any starts; each found position is checked literal by literal), folding INDEX_CHUNK characters at a time (with the longest
    literal's length - 1 more, so a literal across the cut is found once, in the piece it starts in). fold() is per character for
    every character that can fold to ASCII (str.lower()'s one context rule, the Greek final sigma, gives no ASCII either way). None if
    fold() cannot be made (never in CPython: finditer then scans whole)."""
    lits = tuple(sorted({c[0] for rx in rxs for c in scan_of(rx).cover}))
    out: dict[str, list[int]] = {lit: [] for lit in lits}
    if not lits:
        return out
    if lits not in _TRIES:
        by: dict[str, list[str]] = {}
        for lit in lits:
            by.setdefault(lit[0], []).append(lit)
        _TRIES[lits] = (re.compile(_trie(list(lits)), re.S), by)
    trie, by = _TRIES[lits]
    over = max(map(len, lits)) - 1
    for a in range(0, max(len(text), 1), INDEX_CHUNK):
        piece = text[a:a + INDEX_CHUNK + over]
        folded = fold(piece)
        if folded is None:
            return None
        end = min(INDEX_CHUNK, len(piece))
        m = trie.search(folded, 0, end + over)
        while m and m.start() < end:
            p = m.start()
            for lit in by[folded[p]]:
                if folded.startswith(lit, p):
                    out[lit].append(a + p)
            m = trie.search(folded, p + 1, end + over)
    return out
