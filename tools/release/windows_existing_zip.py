#!/usr/bin/env python3
"""Build an installer-free Windows ZIP that discovers existing game installations."""
import argparse
from pathlib import Path
import tempfile
import zipfile
from windows_from_mac import ROOT, export_project, toolchain, digest

NAMES = ('OpenBFME.exe', 'OpenBFME.pck', 'openbfme.windows.template_debug.x86_64.dll')


def package(exported, output):
    if output.exists(): raise ValueError('ZIP already exists; choose a new output')
    for name in NAMES:
        if not (exported / name).is_file(): raise ValueError('Missing Windows export: ' + name)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=output.parent, suffix='.zip', delete=False) as stream:
        temporary = Path(stream.name)
    try:
        with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for name in NAMES: archive.write(exported / name, 'OpenBFME/' + name)
            archive.writestr('OpenBFME/README.txt', 'Extract the entire OpenBFME folder, then run OpenBFME.exe.\nNo installer or bundled game files.\nFirst launch discovers existing BFME2 1.06 and RotWK 2.01 installs from Windows registry entries and standard locations. Confirm the discovered folders or choose them manually.\nSettings/saves use the normal OpenBFME user profile.\n')
        with zipfile.ZipFile(temporary) as archive:
            bad = archive.testzip()
            if bad: raise ValueError('ZIP integrity failure: ' + bad)
        temporary.rename(output)
    finally:
        temporary.unlink(missing_ok=True)
    print('PORTABLE ZIP:', output)
    print('SHA256:', digest(output))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, default=ROOT / 'build/windows-from-mac')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--godot', type=Path, default=Path('/Applications/Godot.app/Contents/MacOS/Godot'))
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if args.output.exists(): parser.error('Output already exists')
    work = args.work.resolve(); work.mkdir(parents=True, exist_ok=True)
    exported = export_project(work, args.godot, toolchain(work), args.jobs, exclude_rotwk=True, existing_install=True)
    project = (work / 'project/project.godot').read_text()
    if 'InstalledContent=' in project: raise ValueError('Bundled-content bootstrap must not enter existing-install ZIP')
    package(exported, args.output.resolve())


if __name__ == '__main__': main()
