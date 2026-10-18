#!/usr/bin/env python3
"""OpenBFME GPL-3.0: tests of the parser-parity tooling (tools/sim/compare_float_chars.cpp, float_chars_corpus.py, gen_real_vectors.py).

The comparator is a gate: it must fail closed on a missing, empty, truncated or substituted corpus, and its digest must agree with the generator's."""
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import gen_real_vectors  # noqa: E402


@pytest.fixture(scope="module")
def comparator():
    cxx = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    if not cxx:
        pytest.skip("SKIP LOUDLY: no C++ compiler on this host: the comparator's fail-closed tests did not run")
    work = os.path.join(REPO, "workspace")  # git-ignored; also inside $HOME, which the Deck's flatpak compiler wrapper requires
    os.makedirs(work, exist_ok=True)
    build = tempfile.mkdtemp(prefix="cfc-", dir=work)
    exe = os.path.join(build, "compare_float_chars")
    cmd = [cxx, "-std=c++17", "-O1", "-I" + os.path.join(REPO, "engine", "src"), "-I" + os.path.join(REPO, "engine", "thirdparty", "fast_float", "include"),
           os.path.join(HERE, "compare_float_chars.cpp"), os.path.join(REPO, "engine", "src", "Common", "System", "FloatFromChars.cpp"), "-o", exe]
    r = subprocess.run(cmd, capture_output=True, text=True)
    try:
        assert r.returncode == 0, r.stderr
        yield exe
    finally:
        shutil.rmtree(build, ignore_errors=True)


def fnv(records):
    h = 14695981039346656037
    for data in records:
        for b in data + b"\xff":
            h = ((h ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % h


def write_corpus(path, records):
    with open(path, "wb") as f:
        for data in records:
            f.write(struct.pack("<I", len(data)) + data)


def run(exe, *args):
    r = subprocess.run([exe, *map(str, args)], capture_output=True, text=True, timeout=300)
    return r.returncode, r.stdout + r.stderr


RECORDS = [b"1", b"-0.5e3", b"2e78", b"inf", b"0x10", b"4.9e-324"]


def test_a_matching_corpus_passes_the_corpus_phase(comparator, tmp_path):
    p = tmp_path / "c.bin"
    write_corpus(p, RECORDS)
    rc, out = run(comparator, p, len(RECORDS), fnv(RECORDS), "--corpus-only")
    assert rc == 0, out
    assert "PASS project_literals_and_edges=%d" % len(RECORDS) in out


def test_a_missing_corpus_fails(comparator, tmp_path):
    rc, out = run(comparator, tmp_path / "absent.bin", 3, "0")
    assert rc == 2 and "PASS" not in out


@pytest.mark.parametrize("claimed", [0, 1, 6])
def test_an_empty_corpus_fails_whatever_count_is_claimed(comparator, tmp_path, claimed):
    p = tmp_path / "empty.bin"
    p.write_bytes(b"")
    rc, out = run(comparator, p, claimed, fnv([]), "--corpus-only")
    assert rc == 2 and "PASS" not in out


def test_a_truncated_record_fails(comparator, tmp_path):
    p = tmp_path / "t.bin"
    write_corpus(p, RECORDS)
    p.write_bytes(p.read_bytes()[:-2])
    rc, out = run(comparator, p, len(RECORDS), fnv(RECORDS), "--corpus-only")
    assert rc == 2 and "PASS" not in out


def test_a_missing_record_fails(comparator, tmp_path):
    p = tmp_path / "m.bin"
    write_corpus(p, RECORDS[:-1])
    rc, out = run(comparator, p, len(RECORDS), fnv(RECORDS), "--corpus-only")
    assert rc == 2 and "PASS" not in out


def test_a_substituted_record_fails_the_digest(comparator, tmp_path):
    p = tmp_path / "s.bin"
    write_corpus(p, RECORDS[:-1] + [b"4.9e-323"])
    rc, out = run(comparator, p, len(RECORDS), fnv(RECORDS), "--corpus-only")
    assert rc == 2 and "PASS" not in out


def test_bad_arguments_fail(comparator, tmp_path):
    p = tmp_path / "c.bin"
    write_corpus(p, RECORDS)
    assert run(comparator, p)[0] == 2
    assert run(comparator, p, "many", fnv(RECORDS), "--corpus-only")[0] == 2


def test_the_real_generated_corpus_matches_the_manifest_digest_and_a_flipped_byte_does_not(comparator, tmp_path):
    out = tmp_path / "real.bin"
    r = subprocess.run([sys.executable, os.path.join(HERE, "float_chars_corpus.py"), "--out", str(out)], capture_output=True, text=True, cwd=REPO)
    assert r.returncode == 0, r.stderr
    manifest = json.load(open(out.with_suffix(".json")))
    assert manifest["records"] > 1000 and manifest["literal_records"] > 100
    rc, text = run(comparator, out, manifest["records"], manifest["fnv1a64"], "--corpus-only")
    assert rc == 0, text
    data = bytearray(out.read_bytes())
    data[4] ^= 1
    out.write_bytes(bytes(data))
    rc, text = run(comparator, out, manifest["records"], manifest["fnv1a64"], "--corpus-only")
    assert rc != 0 and "PASS project" not in text


def test_the_vector_oracle_gets_known_values_right_by_hand():
    cases = {b"1": (1, 0x3F800000, 0x3FF0000000000000), b"-0": (2, 0x80000000, 0x8000000000000000), b"0.1": (3, 0x3DCCCCCD, 0x3FB999999999999A),
             b"1e39": (4, 0x7F800000, 0x48078287F49C4A1D), b"4.9e-324": (8, 0, 1), b"16777217": (8, 0x4B800000, 0x4170000010000000)}
    for text, (end, f, d) in cases.items():
        got = gen_real_vectors.expected(text)
        assert got[:3] == (end, f, d), text
    assert gen_real_vectors.expected(b"nan")[0] == -1 and gen_real_vectors.expected(b"0x10")[0] == 1
    assert gen_real_vectors.expected(b"1e39")[3] is True and gen_real_vectors.expected(b"1e39")[4] is False


def test_the_committed_vector_file_is_what_the_generator_makes(tmp_path):
    committed = os.path.join(REPO, "engine", "tests", "data", "win1_real_vectors.txt")
    assert open(committed, newline="").read() == gen_real_vectors.render(gen_real_vectors.build())
