#!/usr/bin/env python3
"""Lane RELEASE-1: the package archives, written byte-for-byte reproducibly.

    python3 tools/release/make_archive.py <folder> <out.tar.gz | out.zip> --mtime <unix time>
    python3 tools/release/make_archive.py --diff <a> <b>     (which layer / member of two archives differs; exit 1 when they differ)
    python3 tools/release/make_archive.py --extract <archive> <file below its top folder> <out file>
                                                             (lane ONEZIP-1: the launcher's executable for the combined package)
    python3 tools/release/make_archive.py --extract-all <archive> <out folder>
                                                             (a fresh, complete package unpack)
    python3 tools/release/make_archive.py --has <archive> <file below its top folder>     (exit 0 when it is there, 1 when not)

The same folder content and --mtime (package.sh passes the commit's time, SOURCE_DATE_EPOCH) always give the same bytes:
  * members in sorted order, the top folder first; nothing but the folder and regular files (no links, no extra entries)
  * tar: GNU format, uid / gid 0, empty user / group names, every mtime = --mtime, mode 0755 for executables (the file's x bit) and the
    folder, 0644 otherwise; gzip: compression level 9, no file name, header mtime = --mtime
  * zip: nested files only for recognized macOS app packages; no directory entries, comments or extra fields; deflate level 9, every date =
    --mtime (UTC), Unix permissions as above
The bytes then depend only on Python's zlib, which package.sh records next to the archives (see docs/RELEASE.md, "Reproducibility").
"""
from __future__ import annotations

import gzip
import io
import os
import re
import stat
import sys
import tarfile
import time
import zipfile
from pathlib import Path

MACOS_ARM64_TOP = re.compile(r"^openbfme-[A-Za-z0-9][A-Za-z0-9.+_-]*-macos-arm64$")


def members(folder: Path) -> list[Path]:
    out = []
    for p in sorted(folder.rglob("*")):
        if p.is_symlink() or not (p.is_file() or p.is_dir()):
            raise SystemExit(f"make_archive: {p} is neither a regular file nor a folder")
        out.append(p)
    return out


def mode_of(p: Path) -> int:
    return 0o755 if p.is_dir() or (p.stat().st_mode & stat.S_IXUSR) else 0o644


def tar_gz(folder: Path, out: Path, mtime: int) -> None:
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.GNU_FORMAT) as t:
        for p in [folder] + members(folder):
            arc = folder.name if p == folder else f"{folder.name}/{p.relative_to(folder).as_posix()}"
            info = tarfile.TarInfo(arc)
            info.mtime = mtime
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            info.mode = mode_of(p)
            if p.is_dir():
                info.type = tarfile.DIRTYPE
                t.addfile(info)
            else:
                data = p.read_bytes()
                info.size = len(data)
                t.addfile(info, io.BytesIO(data))
    with open(out, "wb") as f, gzip.GzipFile(filename="", mode="wb", fileobj=f, compresslevel=9, mtime=mtime) as g:
        g.write(raw.getvalue())


def zip_file(folder: Path, out: Path, mtime: int) -> None:
    date = time.gmtime(max(mtime, 315532800))[:6]  # ZIP dates start in 1980
    paths = members(folder)
    nested = any(p.is_dir() for p in paths)
    if nested and not folder.name.endswith("-macos-universal") and folder.name != "openbfme-v0.4.6-macos-arm64-unstable-preview" and not MACOS_ARM64_TOP.fullmatch(folder.name):
        raise SystemExit(f"make_archive: {folder.name}: nested package files are supported only for macOS universal apps or recognized Mac arm64 packages")
    with zipfile.ZipFile(out, "w") as z:
        for p in paths:
            if p.is_dir():
                continue
            info = zipfile.ZipInfo(f"{folder.name}/{p.relative_to(folder).as_posix()}", date_time=date)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | mode_of(p)) << 16
            z.writestr(info, p.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


def archive_members(path: Path) -> list[tuple[str, bytes]]:
    """(name + metadata, content) of every member, for --diff."""
    data = path.read_bytes()
    out = []
    if path.name.endswith(".zip"):
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            for i in z.infolist():
                out.append((f"{i.filename} {i.date_time} {i.external_attr:o} extra={i.extra!r} comment={i.comment!r}", z.read(i)))
    else:
        with tarfile.open(fileobj=io.BytesIO(gzip.decompress(data)), mode="r:") as t:
            for m in t.getmembers():
                content = t.extractfile(m).read() if m.isfile() else b""
                out.append((f"{m.name} {m.type!r} {m.mode:o} {m.mtime} {m.uid}/{m.gid} {m.uname}/{m.gname} link={m.linkname!r}", content))
    return out


def diff(a: Path, b: Path) -> int:
    da, db = a.read_bytes(), b.read_bytes()
    if da == db:
        print(f"identical: {a.name}")
        return 0
    print(f"DIFFERENT: {a} and {b} ({len(da)} / {len(db)} bytes)")
    try:
        ma, mb = archive_members(a), archive_members(b)
    except Exception as e:  # a damaged archive: the byte difference is the finding
        print(f"  cannot list the members: {e}")
        return 1
    for (na, ca), (nb, cb) in zip(ma, mb):
        if na != nb:
            print(f"  member metadata: {na!r} vs {nb!r}")
        elif ca != cb:
            print(f"  member content: {na.split(' ')[0]} ({len(ca)} / {len(cb)} bytes)")
    if len(ma) != len(mb):
        print(f"  {len(ma)} / {len(mb)} members")
    if ma == mb:
        print("  same members: the difference is in the container (headers, padding, compression or bytes outside the members)")
    return 1


def has(archive: Path, rel: str) -> bool:
    """whether a package archive holds the file `rel` below its top folder (package.sh --verify: which run made a folder)"""
    want = archive.name.removesuffix(".tar.gz").removesuffix(".zip") + "/" + rel
    if archive.name.endswith(".zip"):
        with zipfile.ZipFile(archive) as z:
            return any(n == want or n.startswith(want.rstrip("/") + "/") for n in z.namelist())
    with tarfile.open(archive, "r:gz") as t:
        return any(n == want or n.startswith(want.rstrip("/") + "/") for n in t.getnames())


def extract(archive: Path, rel: str, out: Path) -> int:
    """Extract one file to `out` or a regular-file-only folder subtree below the archive's top folder."""
    top = archive.name.removesuffix(".tar.gz").removesuffix(".zip")
    want = f"{top}/{rel}"
    out = out.resolve()
    extracted = 0
    if archive.name.endswith(".zip"):
        with zipfile.ZipFile(archive) as z:
            matches = [info for info in z.infolist() if info.filename == want or info.filename.startswith(want.rstrip("/") + "/")]
            if not matches:
                print(f"make_archive: {archive.name} has no {want}")
                return 1
            single_file = len(matches) == 1 and matches[0].filename == want and not matches[0].is_dir()
            for info in matches:
                if info.is_dir() or info.filename.endswith("/"):
                    continue
                parts = Path(info.filename).parts
                mode = (info.external_attr >> 16) & 0o170000
                if Path(info.filename).is_absolute() or ".." in parts or mode not in (0, stat.S_IFREG):
                    raise SystemExit(f"make_archive: {info.filename} is not a safe regular file")
                target = out if single_file else out.joinpath(*parts[len(Path(top).parts):])
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(z.read(info))
                os.chmod(target, 0o755 if (info.external_attr >> 16) & 0o111 else 0o644)
                extracted += 1
    else:
        with tarfile.open(archive, "r:gz") as t:
            matches = [member for member in t.getmembers() if member.name == want or member.name.startswith(want.rstrip("/") + "/")]
            if not matches:
                print(f"make_archive: {archive.name} has no {want}")
                return 1
            single_file = len(matches) == 1 and matches[0].name == want and matches[0].isfile()
            for member in matches:
                if member.isdir():
                    continue
                parts = Path(member.name).parts
                if not member.isfile() or Path(member.name).is_absolute() or ".." in parts:
                    raise SystemExit(f"make_archive: {member.name} is not a safe regular file")
                target = out if single_file else out.joinpath(*parts[len(Path(top).parts):])
                target.parent.mkdir(parents=True, exist_ok=True)
                source = t.extractfile(member)
                if source is None:
                    raise SystemExit(f"make_archive: cannot read {member.name}")
                target.write_bytes(source.read())
                os.chmod(target, (member.mode & 0o111) | 0o644)
                extracted += 1
    if extracted == 0:
        print(f"make_archive: {want} contains no files")
        return 1
    return 0


def extract_all(archive: Path, out: Path) -> int:
    """Extract every regular file below the archive's top folder into an empty directory."""
    top = archive.name.removesuffix(".tar.gz").removesuffix(".zip")
    out.mkdir(parents=True, exist_ok=True)
    out = out.resolve()
    if any(out.iterdir()):
        raise SystemExit(f"make_archive: {out} is not empty")
    extracted = 0

    def target_for(name: str, is_dir: bool) -> Path | None:
        if name.endswith("/"):
            name = name[:-1]
        parts = name.split("/")
        if parts == [top] and is_dir:
            return None
        if "\\" in name or any(part in ("", ".", "..") for part in parts) or parts[0] != top:
            raise SystemExit(f"make_archive: {name} is not a safe package path")
        target = out.joinpath(*parts[1:])
        if not target.resolve().is_relative_to(out):
            raise SystemExit(f"make_archive: {name} escapes the output folder")
        return target

    if archive.name.endswith(".zip"):
        with zipfile.ZipFile(archive) as z:
            for info in z.infolist():
                mode = (info.external_attr >> 16) & 0o170000
                if info.is_dir() or info.filename.endswith("/"):
                    if mode not in (0, stat.S_IFDIR):
                        raise SystemExit(f"make_archive: {info.filename} is not a regular directory")
                    target_for(info.filename, True)
                    continue
                if mode not in (0, stat.S_IFREG):
                    raise SystemExit(f"make_archive: {info.filename} is not a safe regular file")
                target = target_for(info.filename, False)
                if target is None or target.exists():
                    raise SystemExit(f"make_archive: {info.filename} is duplicated")
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(z.read(info))
                os.chmod(target, 0o755 if (info.external_attr >> 16) & 0o111 else 0o644)
                extracted += 1
    else:
        with tarfile.open(archive, "r:gz") as t:
            for member in t.getmembers():
                if member.isdir():
                    target_for(member.name, True)
                    continue
                if not member.isfile():
                    raise SystemExit(f"make_archive: {member.name} is not a safe regular file")
                target = target_for(member.name, False)
                if target is None or target.exists():
                    raise SystemExit(f"make_archive: {member.name} is duplicated")
                target.parent.mkdir(parents=True, exist_ok=True)
                source = t.extractfile(member)
                if source is None:
                    raise SystemExit(f"make_archive: cannot read {member.name}")
                target.write_bytes(source.read())
                os.chmod(target, (member.mode & 0o111) | 0o644)
                extracted += 1
    if extracted == 0:
        print(f"make_archive: {archive.name} contains no files")
        return 1
    return 0


def main(argv: list[str]) -> int:
    if len(argv) == 3 and argv[0] == "--diff":
        return diff(Path(argv[1]), Path(argv[2]))
    if len(argv) == 3 and argv[0] == "--has":
        return 0 if has(Path(argv[1]), argv[2]) else 1
    if len(argv) == 3 and argv[0] == "--extract-all":
        return extract_all(Path(argv[1]), Path(argv[2]))
    if len(argv) == 4 and argv[0] == "--extract":
        return extract(Path(argv[1]), argv[2], Path(argv[3]))
    if len(argv) != 4 or argv[2] != "--mtime":
        print(__doc__)
        return 2
    folder, out, mtime = Path(argv[0]), Path(argv[1]), int(argv[3])
    if out.name.endswith(".tar.gz"):
        tar_gz(folder, out, mtime)
    elif out.name.endswith(".zip"):
        zip_file(folder, out, mtime)
    else:
        print(f"make_archive: {out.name} is neither .tar.gz nor .zip")
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
