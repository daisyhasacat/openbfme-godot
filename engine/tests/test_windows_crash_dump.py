"""Native subprocess checks. Usage: python engine/tests/test_windows_crash_dump.py <probe.exe> <evidence-dir>"""
import pathlib
import re
import struct
import subprocess
import sys

probe = pathlib.Path(sys.argv[1]).resolve()
root = pathlib.Path(sys.argv[2]).resolve()
root.mkdir(parents=True, exist_ok=True)
for mode in ("handled", "main", "worker", "existing-worker", "reassert", "job", "pool", "terminate", "fatal"):
    directory = root / mode
    directory.mkdir(exist_ok=True)
    before = set(directory.glob("*.dmp"))
    result = subprocess.run([str(probe), mode, str(directory)], capture_output=True, timeout=30)
    output = (result.stdout + result.stderr).decode("utf-8", errors="replace")
    (root / (mode + ".log")).write_text(output, encoding="utf-8")
    dumps = set(directory.glob("*.dmp")) - before
    if mode == "handled":
        assert result.returncode == 0, output
        assert not dumps, "handled exceptions must not create a dump"
    else:
        assert result.returncode != 0, output
        assert len(dumps) == 1, (mode, output, dumps)
        data = dumps.pop().read_bytes()
        assert data[:4] == b"MDMP"
        count, rva = struct.unpack_from("<II", data, 8)
        exception = None
        for i in range(count):
            stream, size, offset = struct.unpack_from("<III", data, rva + i * 12)
            if stream == 6:  # MINIDUMP_EXCEPTION_STREAM
                exception = struct.unpack_from("<IIII", data, offset)
        assert exception is not None, "missing exception context"
        thread, _, code, flags = exception
        expected = 0x40000015 if mode == "terminate" else 0xC0000409 if mode == "fatal" else 0xC0000005
        assert code == expected, (mode, hex(code))
        main = int(re.search(r"MAIN_THREAD=(\d+)", output)[1])
        assert (thread == main) == (mode in ("main", "fatal")), (mode, thread, main)
        if mode == "reassert":
            assert "was replaced" in output
    print(f"PASS {mode}: exit={result.returncode}, dumps={0 if mode == 'handled' else 1}")
