"""Run the soak harness with controlled peers and BSD-style padded wc counts."""
from pathlib import Path
import os
import shutil
import subprocess
import sys

import pytest

REPO = Path(__file__).resolve().parents[2]


def executable(path: Path, source: str) -> None:
    path.write_text(source)
    path.chmod(0o755)


@pytest.mark.parametrize('failure', ['', 'frames', 'hash', 'desync', 'replay'])
def test_soak_verdict_with_padded_counts(tmp_path, failure):
    build = tmp_path / 'build'
    (build / 'relay').mkdir(parents=True)
    executable(build / 'openbfme_peer', f'#!{sys.executable}\n' + r'''
import json, os, sys
from pathlib import Path
args = sys.argv[1:]
def value(key): return args[args.index(key) + 1]
fail = os.environ.get('SOAK_TEST_FAILURE', '')
if '--print-profile' in args:
    print('0' * 32)
    print('faction 0 FactionMen\nfaction 1 FactionMordor')
elif '--online-start' in args and '--relay-log' not in args:
    slot = json.loads(Path(value('--online-start')).read_text())['slot']
    frames = int(value('--frames')) - (1 if fail == 'frames' and slot == 1 else 0)
    hashed = slot if fail == 'hash' else 0
    desyncs = 1 if fail == 'desync' and slot == 1 else 0
    Path(value('--report')).write_text(f'frames {frames}\nfinal_hash {hashed}\ndesyncs {desyncs}\nrelay packets 0 bound 1 \n')
elif '--relay-log' in args:
    Path(value('--record')).write_text('generated test replay')
elif '--replay' in args:
    mismatches = 1 if fail == 'replay' else 0
    Path(value('--report')).write_text(f'hashes_compared 3\nmismatches {mismatches}\n')
''')
    executable(build / 'relay/openbfme_relay', f'#!{sys.executable}\n' + r'''
import signal, sys, time
from pathlib import Path
args = sys.argv[1:]
Path(args[args.index('--log-dir') + 1], 'test.obfmerlog').write_text('generated relay log')
signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
while True: time.sleep(0.05)
''')
    bin_dir = tmp_path / 'bin'
    bin_dir.mkdir()
    # Force the macOS wc output on every platform so Linux CI catches the regression too.
    executable(bin_dir / 'wc', f'#!{sys.executable}\n' +
               f'import subprocess, sys\nr = subprocess.run([{shutil.which("wc")!r}, *sys.argv[1:]], input=sys.stdin.buffer.read(), capture_output=True, check=True)\nprint(f"{{int(r.stdout):8d}}")\n')
    (bin_dir / 'python3').symlink_to(sys.executable)
    env = dict(os.environ, PATH=str(bin_dir) + os.pathsep + os.environ['PATH'], SOAK_GAMES='1', SOAK_TEST_FAILURE=failure)
    result = subprocess.run(['bash', str(REPO / 'tools/net/online2_soak.sh'), str(build), str(tmp_path / 'out'), '300'],
                            env=env, capture_output=True, text=True, timeout=30)
    expected = 1 if failure else 0
    assert result.returncode == expected, result.stdout + result.stderr
    assert f'RESULT online2 soak: games 1 frames 300 exit {expected}' in result.stdout
