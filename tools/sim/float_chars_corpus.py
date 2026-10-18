#!/usr/bin/env python3
"""OpenBFME GPL-3.0: freeze project literals and exact decimal rounding boundaries for compare_float_chars.cpp."""
import argparse
import ast
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import random
import re
import shlex
import struct
import subprocess
import sys
import warnings

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', required=True, type=Path, help='binary corpus path; a JSON source/checksum manifest is written alongside it')
parser.add_argument('--run', help='the compiled compare_float_chars: run it on the corpus with its record count and digest and exit with its status')
args = parser.parse_args()
out, run = args.out, args.run
records = []
sources = []
seen = set()
def add(text):
    data = text.encode('utf-8', errors='surrogatepass')
    if data not in seen:
        records.append(data)
        seen.add(data)

# A superset of textual numeric literals, including C/C++ suffixes and hex forms.
number = re.compile(r'[-+]?(?:0[xX][0-9a-fA-F]+(?:\.[0-9a-fA-F]*)?(?:[pP][-+]?\d+)?|(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?)[uUlLfF]*|[-+]?(?:inf(?:inity)?|nan(?:\([^)]*\))?)', re.I)
quoted = re.compile(r'"(?:\\.|[^"\\])*"', re.S)
raw = re.compile(r'R"([^ ()\\\t\r\n]{0,16})\((.*?)\)\1"', re.S)
paths = subprocess.check_output(['git', 'ls-files', '-z', 'engine/src', 'engine/tests', 'engine/data', 'tests/data'], cwd=root).decode().split('\0')
for name in sorted(filter(None, paths)):
    p = root / name
    if not p.is_file():
        raise RuntimeError('missing tracked corpus input: '+name)
    data = p.read_bytes()
    if b'\0' in data:
        sources.append(dict(path=name, sha256=hashlib.sha256(data).hexdigest(), binary=True))
        continue
    text = data.decode('utf-8')
    sources.append(dict(path=name, sha256=hashlib.sha256(data).hexdigest(), binary=False))
    for m in number.finditer(text):
        add(m.group())
    for m in raw.finditer(text):
        add(m.group(2))
    previous_end = None
    combined = ''
    for m in quoted.finditer(text):
        try:
            with warnings.catch_warnings():
                warnings.simplefilter('ignore')
                value = ast.literal_eval(m.group())
        except (ValueError, SyntaxError):
            # The numeric-token scan above covers tokens in non-Python C++ forms.
            continue
        add(value)
        if previous_end is not None and not re.sub(r'/\*.*?\*/|//[^\n]*', '', text[previous_end:m.start()], flags=re.S).strip():
            combined += value
            add(combined)
        else:
            combined = value
        previous_end = m.end()
literal_count = len(records)

for s in ['', ' ', '\t1', '\n-1', '+1', ' 1', '0x', '0x1p0', '-0x1p0', '0X1.ffffp1023', '1e', '1e+', '1e-', '.', '-.', '1.2.3', '1\0tail', '1,5']:
    add(s)
for spelling in ['inf', 'infinity', 'infi', 'infinitytail', 'nan', 'nan()', 'nan(123)', 'nan(0x123)', 'nan(payload)', 'nan(a-b)', 'nan(', 'nan(abc)tail']:
    for sign in ['', '-', '+', ' ']:
        for form in [spelling, spelling.upper(), spelling.title()]:
            add(sign+form)
for n in [64, 768, 1024, 4096, 100000]:
    for s in ['9'*n, '0.'+'0'*n+'1', '1.'+'0'*n+'1', '1e'+'9'*n, '1e-'+'9'*n, '9'*n+'e-'+str(n)]:
        add(s)
        add('-'+s)

def exact_value(bits, frac, bias):
    exponent, mantissa = divmod(bits, 1 << frac)
    significand = mantissa if exponent == 0 else mantissa + (1 << frac)
    power = (1-bias if exponent == 0 else exponent-bias) - frac
    return Fraction(significand << power) if power >= 0 else Fraction(significand, 1 << -power)

rng = random.Random(0x4d414335)
for frac, expbits, bias in [(23, 8, 127), (52, 11, 1023)]:
    maxbits = (((1 << expbits)-1) << frac)-1
    selected = {0, 1, maxbits}
    for exp in range((1 << expbits)-1):
        for mantissa in [0, 1, (1 << frac)-2, (1 << frac)-1]:
            selected.add((exp << frac) | mantissa)
    selected.update(rng.randrange(maxbits+1) for _ in range(512))
    for bits in sorted(selected):
        midpoint = (exact_value(bits, frac, bias) + exact_value(bits+1, frac, bias))/2
        k = midpoint.denominator.bit_length()-1
        assert midpoint.denominator == 1 << k
        # Exact midpoint, then one decimal unit on either side at an extra digit.
        coefficient = midpoint.numerator * 5**k * 10
        scale = k+1
        for offset in [-1, 0, 1]:
            digits = str(coefficient+offset).rjust(scale+1, '0')
            s = digits[:-scale]+'.'+digits[-scale:]
            add(s)
            add('-'+s)

if not records or not literal_count:
    raise RuntimeError('the corpus is empty: no project literals were found')
fnv = 14695981039346656037
for data in records:  # the comparator's digest of the corpus phase: FNV-1a over each record's bytes, then 0xff
    for b in data + b'\xff':
        fnv = ((fnv ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
with out.open('wb') as f:
    for data in records:
        f.write(struct.pack('<I', len(data)))
        f.write(data)
manifest = dict(records=len(records), literal_records=literal_count, fnv1a64='%016x' % fnv, corpus_sha256=hashlib.sha256(out.read_bytes()).hexdigest(), sources=sources)
out.with_suffix('.json').write_text(json.dumps(manifest, indent=2)+'\n')
print(json.dumps({k:v for k,v in manifest.items() if k!='sources'}), 'source_files',len(sources))
if run:
    # comparator <corpus> <records> <digest>: the compiled tools/sim/compare_float_chars.cpp; its exit code is ours
    sys.exit(subprocess.call([*shlex.split(run), str(out), str(len(records)), '%016x' % fnv]))
