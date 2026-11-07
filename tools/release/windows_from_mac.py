#!/usr/bin/env python3
"""Build Windows x64 on macOS and create a private installer with local content."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
TEMPLATES = Path(__file__).with_name('windows_installer')
TOOLCHAIN = 'llvm-mingw-20261006-ucrt-macos-universal'
TOOLCHAIN_SHA256 = 'a7a19b11048e89372637d26279cdb7c430ed3cf8c1e224310421312bf986ebf6'
USERDATA = Path.home() / 'Library/Application Support/Godot/app_userdata/OpenBFME'


def run(*args, **kwargs):
    print('+', ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True, **kwargs)



def checked_editor(*args):
    """Godot can return zero after script/import errors; reject those exports too."""
    print('+', ' '.join(map(str, args)), flush=True)
    process = subprocess.Popen(list(map(str, args)), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
    problems = []
    for line in process.stdout:
        print(line, end='', flush=True)
        plain = re.sub(r'\x1b\[[0-9;]*m', '', line)
        if re.search(r'(^|\s)(SCRIPT ERROR:|ERROR:|PACK FILES FAIL:)', plain): problems.append(plain.strip())
    process.stdout.close()
    result = process.wait()
    if result: raise subprocess.CalledProcessError(result, args)
    if problems: raise ValueError('Godot import/export reported errors: ' + '; '.join(problems[:3]))


def digest(path, algorithm='sha256'):
    h = hashlib.new(algorithm)
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''): h.update(block)
    return h.hexdigest()


def config(path):
    result = {}; section = ''
    if not path.exists(): return result
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith(('#', ';')): continue
        if line.startswith('[') and line.endswith(']'): section = line[1:-1]; continue
        key, sep, value = line.partition('=')
        if not sep: raise ValueError(f'Unsupported configuration line in {path.name}')
        value = re.sub(r'^Array\[String\]\((.*)\)$', r'\1', value.strip())
        result[section + '/' + key.strip()] = json.loads(value)
    return result


def inventory(userdata, rotwk=None, bfme2=None, map_library=None, exclude_rotwk=False):
    remembered = {}
    paths = userdata / 'install-paths.cfg'
    if paths.exists():
        for line in paths.read_text().splitlines():
            key, sep, value = line.partition('=')
            if sep: remembered[key.strip()] = value.strip()
    roots = {}
    for name, explicit in [('rotwk', rotwk), ('bfme2', bfme2)]:
        if name == 'rotwk' and exclude_rotwk: continue
        value = explicit or os.environ.get(name.upper() + '_INSTALL') or remembered.get(name.upper() + '_INSTALL')
        if not value: raise ValueError(f'Missing {name} install; pass --{name}')
        path = Path(value).resolve()
        if not path.is_dir() or not any(p.suffix.lower() == '.big' for p in path.iterdir()):
            raise ValueError(f'{name} install must contain BIG archives: {path}')
        roots[name] = path
    cfg = config(userdata / 'mods.cfg')
    game = config(userdata / 'game-selection.cfg').get('game/selected', 'bfme2')
    if game not in ('bfme2', 'bfme2-106'):
        raise ValueError('This workflow packages BFME2/RotWK; select one of those games before building.')
    if exclude_rotwk: game = 'bfme2-106'
    mods = []; sources = [(path, 'content/' + name) for name, path in roots.items()]
    hd = cfg.get('mods/hd_textures', True)
    textures = cfg.get('mods/textures_only', [])
    for value in cfg.get('mods/enabled', []):
        texture = value in textures
        if texture and not hd: continue  # Match the actual runtime selection.
        path = Path(value.replace('user://', str(userdata) + '/', 1)).resolve()
        if not path.exists(): raise ValueError(f'Active mod does not exist: {path}')
        target = f'content/mods/{len(mods):03d}' + ('.big' if path.is_file() else '')
        sources.append((path, target)); mods.append({'path': target, 'name': path.name, 'textures_only': texture})
    for name in ('installed-maps', 'map-library', 'map_edits'):
        folder = userdata / name
        if folder.is_dir(): sources.append((folder, 'content/' + name))
    if map_library is not None and not map_library.is_dir():
        raise ValueError('Explicit map library does not exist or is not a directory')
    if not exclude_rotwk and map_library is None and userdata.resolve() == USERDATA.resolve():
        map_library = Path.home() / 'Library/Application Support/BFMELauncher/Maps/Library/rotwk'
    if not exclude_rotwk and map_library is not None and map_library.is_dir():
        sources.append((map_library, 'content/map-library/bundles/mac-snapshot/rotwk'))
    walls = userdata / 'wall_balance.cfg'
    if not walls.exists(): walls = ROOT / 'godot/dev/wall_balance.cfg'
    sources.append((walls, 'profile/wall_balance.cfg'))
    return {'version': 1, 'game': game, 'hd_textures': hd, 'mods': mods, 'installs': list(roots)}, sources


def walk_content(path, relative=Path(), ancestors=frozenset()):
    resolved = path.resolve(strict=True)
    if path.is_dir():
        if resolved in ancestors: raise ValueError(f'Cyclic content directory link: {path}')
        for child in sorted(path.iterdir()):
            yield from walk_content(child, relative / child.name, ancestors | {resolved})
    elif path.is_file():
        yield path, relative
    else:
        raise ValueError(f'Unsupported content file: {path}')


def files(sources, exclude_movies=False, exclude_rotwk=False):
    seen = set()
    for source, destination in sources:
        paths = walk_content(source) if source.is_dir() else [(source, Path())]
        for path, relative in paths:
            target = destination + '/' + relative.as_posix() if source.is_dir() else destination
            parts = target.lower().split('/')
            if exclude_movies and ('movies' in parts or path.suffix.lower() in ('.vp6', '.bik', '.mp4', '.avi', '.mov', '.webm')): continue
            if exclude_rotwk and 'rotwk' in parts: continue
            if target.casefold() in seen: raise ValueError(f'Windows filename collision: {target}')
            # Reject Windows-invalid filenames before copying gigabytes of data.
            for part in target.split('/'):
                if re.search(r'[<>:"\\|?*\x00-\x1f]', part) or part.endswith(('.', ' ')) or re.fullmatch(r'(?i)(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\..*)?', part):
                    raise ValueError(f'Windows-incompatible content path: {target}')
            seen.add(target.casefold())
            yield path, target


def fetch(url, destination, expected, algorithm='sha256'):
    if destination.exists() and digest(destination, algorithm) == expected: return
    temporary = destination.with_suffix(destination.suffix + '.part')
    run('curl', '-fL', '--retry', '3', '-o', temporary, url)
    if digest(temporary, algorithm) != expected: raise ValueError(f'Download checksum mismatch: {destination.name}')
    temporary.replace(destination)


def toolchain(work):
    archive = work / (TOOLCHAIN + '.tar.xz')
    fetch(f'https://github.com/mstorsjo/llvm-mingw/releases/download/20261006/{archive.name}', archive, TOOLCHAIN_SHA256)
    target = work / TOOLCHAIN
    if not (target / 'bin/x86_64-w64-mingw32-clang++').exists():
        run('tar', '-xf', archive, '-C', work)
    return target


def export_project(work, godot, compiler, jobs, exclude_rotwk=False, existing_install=False):
    build = work / 'build'; binary = work / 'bin'
    run('cmake', '-S', ROOT / 'engine', '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
        '-DOPENBFME_LAME=OFF', f'-DLLVM_MINGW_ROOT={compiler}', f'-DOPENBFME_GODOT_BIN={binary}',
        f'-DCMAKE_TOOLCHAIN_FILE={ROOT}/engine/cmake/toolchains/windows-x64-llvm-mingw.cmake')
    run('cmake', '--build', build, '--target', 'openbfme', '-j', jobs)
    dll = binary / 'openbfme.windows.template_debug.x86_64.dll'
    pdb = dll.with_suffix('.pdb')
    for command, args in [('scrub', [pdb]), ('scrub-dll', [dll]), ('check', [dll, pdb])]:
        run('python3', ROOT / 'tools/release/pdb_tools.py', command, *args)
    run('python3', ROOT / 'tools/release/audit_package.py', dll)
    project = work / 'project'
    if project.exists(): shutil.rmtree(project)
    shutil.copytree(ROOT / 'godot', project, ignore=shutil.ignore_patterns('.godot', 'bin', '*.disabled'))
    if exclude_rotwk:
        for name in ('rotwk_202_maps.big', 'wall_towers.big'):
            (project / 'mods' / name).unlink(missing_ok=True)
    (project / 'bin').mkdir()
    shutil.copy2(dll, project / 'bin' / dll.name)
    # Native host extension is needed to import scripts with registered engine types.
    host_libs = list((ROOT / 'godot/bin').glob('*.macos.*.dylib'))
    if not host_libs: raise ValueError('Build the native Mac extension first; Godot needs it for script imports.')
    for path in host_libs: shutil.copy2(path, project / 'bin' / path.name)
    if not existing_install:
        shutil.copy2(TEMPLATES / 'content_bootstrap.gd', project / 'content_bootstrap.gd')
        settings = (project / 'project.godot').read_text()
        settings = settings.replace('[application]', '[application]\nconfig/use_custom_user_dir=true\nconfig/custom_user_dir="OpenBFME Windows Bundle"', 1)
        settings = settings.replace('[autoload]', '[autoload]\nInstalledContent="*res://content_bootstrap.gd"', 1)
        (project / 'project.godot').write_text(settings)
    version = subprocess.check_output([str(godot), '--version'], text=True).strip().split('.stable')[0]
    if not re.fullmatch(r'4\.\d+(\.\d+)?', version): raise ValueError('Use a stable Godot 4 editor.')
    name = f'Godot_v{version}-stable_export_templates.tpz'
    base = f'https://github.com/godotengine/godot/releases/download/{version}-stable'
    sums = subprocess.check_output(['curl', '-fsSL', base + '/SHA512-SUMS.txt'], text=True)
    matching = [line.split()[0] for line in sums.splitlines() if line.split()[-1].lstrip('*') == name]
    if len(matching) != 1: raise ValueError('Official template checksum not found')
    archive = work / name
    fetch(base + '/' + name, archive, matching[0], 'sha512')
    template = work / 'windows_debug_x86_64.exe'
    with zipfile.ZipFile(archive) as z:
        with z.open('templates/windows_debug_x86_64.exe') as source, template.open('wb') as dest: shutil.copyfileobj(source, dest)
    presets = (project / 'export_presets.cfg').read_text()
    start = presets.index('[preset.1.options]'); end = presets.find('[preset.2]', start)
    options = presets[start:end].replace('custom_template/debug=""', f'custom_template/debug={json.dumps(str(template))}')
    (project / 'export_presets.cfg').write_text(presets[:start] + options + presets[end:])
    # A failed import is a failure; do not export a partially imported project.
    checked_editor(godot, '--headless', '--path', project, '--import')
    exported = work / 'export'; exported.mkdir(exist_ok=True)
    checked_editor(godot, '--headless', '--path', project, '--export-debug', 'Windows x64', exported / 'OpenBFME.exe')
    for name in ('OpenBFME.exe', 'OpenBFME.pck', dll.name):
        if not (exported / name).is_file(): raise ValueError(f'Export is missing {name}')
    return exported


def package(exported, output, profile, sources, exclude_movies=False, exclude_rotwk=False):
    if output.exists(): raise ValueError('Output already exists; choose a new directory')
    if exported.resolve() in output.resolve().parents: raise ValueError('Output must be outside the export')
    rows = list(files([(exported, ''), *sources], exclude_movies, exclude_rotwk))
    # Empty export prefix means files belong at the installation root.
    rows = [(path, target.lstrip('/')) for path, target in rows]
    for path, _ in rows:
        if path.suffix.lower() == '.pdb': raise ValueError('Private debug symbols cannot enter the installer')
    size = sum(path.stat().st_size for path, _ in rows)
    output.parent.mkdir(parents=True, exist_ok=True)
    if shutil.disk_usage(output.parent).free < size + 512 * 1024**2: raise ValueError('Insufficient free space for content archive')
    with tempfile.TemporaryDirectory(prefix='windows-installer-', dir=output.parent) as temporary:
        staged = Path(temporary)
        payload = staged / 'OpenBFME-content.tar'
        manifest = []
        with tarfile.open(payload, 'w', format=tarfile.PAX_FORMAT, dereference=True) as tar:
            for source, target in sources:
                if source.is_dir(): tar.add(source, arcname=target, recursive=False)
            for path, target in rows:
                before = path.stat()
                checksum = digest(path)
                tar.add(path, arcname=target, recursive=False)
                after = path.stat()
                if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
                    raise ValueError(f'Content changed during packaging: {path.name}; run again with the game closed')
                manifest.append({'path': target, 'bytes': before.st_size, 'sha256': checksum})
            profile_path = staged / 'content-profile.json'
            profile_path.write_text(json.dumps(profile, indent=2))
            tar.add(profile_path, arcname=profile_path.name)
            manifest_path = staged / 'content-manifest.json'
            manifest_path.write_text(json.dumps(manifest, indent=2))
            tar.add(manifest_path, arcname=manifest_path.name)
        checksum = digest(payload)
        installer = staged / 'OpenBFME-Setup.exe'
        run('makensis', '-V2', f'-DOUTPUT={installer}', f'-DPAYLOAD_SHA256={checksum}', f'-DINSTALL_KB={(size + 1023) // 1024}', TEMPLATES / 'setup.nsi', cwd=TEMPLATES)
        (staged / 'README.txt').write_text('Private local content bundle. Keep both files together.\nRun OpenBFME-Setup.exe on Windows 10/11 x64 and select an empty folder.\nThe executable resolves content relative to its installed location.\nSettings and saves: %APPDATA%/OpenBFME Windows Bundle\nContent archive SHA256: ' + checksum + '\n')
        profile_path.unlink(); manifest_path.unlink()
        shutil.move(str(staged), str(output))
    print('INSTALLER:', output / 'OpenBFME-Setup.exe')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exclude-movies', action='store_true', help='Omit loose movie files without changing game archives')
    parser.add_argument('--exclude-rotwk', action='store_true', help='Package BFME2 only, without RotWK installs or library')
    parser.add_argument('--upload', action='store_true', help='Upload completed installer to the maclauncher Space')
    parser.add_argument('--cdn-profile', help='AWS CLI profile for Spaces credentials')
    parser.add_argument('--cdn-release', help='Unique CDN release folder name')
    parser.add_argument('--cdn-public', action='store_true', help='Make the installer and included assets publicly downloadable')
    parser.add_argument('--userdata', type=Path, default=USERDATA)
    parser.add_argument('--map-library', type=Path, help='Additional RotWK map library to bundle')
    parser.add_argument('--rotwk', type=Path); parser.add_argument('--bfme2', type=Path)
    parser.add_argument('--work', type=Path, default=ROOT / 'build/windows-from-mac')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/windows-private-installer')
    parser.add_argument('--godot', type=Path, default=Path('/Applications/Godot.app/Contents/MacOS/Godot'))
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--check', action='store_true', help='Inspect selected content and prerequisites without building/copying')
    parser.add_argument('--exported', type=Path, help='Package an existing Windows export instead of compiling')
    args = parser.parse_args()
    profile, sources = inventory(args.userdata, args.rotwk, args.bfme2, args.map_library, args.exclude_rotwk)
    rows = list(files(sources, args.exclude_movies, args.exclude_rotwk))
    print(json.dumps(profile, indent=2))
    print(f'Content: {len(rows)} files, {sum(p.stat().st_size for p, _ in rows) / 1024**3:.2f} GiB')
    for command in ('cmake', 'ninja', 'curl', 'tar', 'makensis'):
        if not shutil.which(command): raise ValueError(f'Missing required tool: {command}')
    if not args.godot.is_file(): raise ValueError('Godot not found; pass --godot')
    if args.jobs < 1: raise ValueError('--jobs must be positive')
    if args.check: return
    work = args.work.resolve(); output = args.output.resolve()
    for source, _ in sources:
        if output == source or source in output.parents or work == source or source in work.parents:
            raise ValueError('Work and output must be outside content sources')
    if output.exists(): raise ValueError('Output already exists; choose a new directory')
    work.mkdir(parents=True, exist_ok=True)
    exported = args.exported.resolve() if args.exported else export_project(work, args.godot, toolchain(work), args.jobs, args.exclude_rotwk)
    for name in ('OpenBFME.exe', 'OpenBFME.pck', 'openbfme.windows.template_debug.x86_64.dll'):
        if not (exported / name).is_file(): raise ValueError(f'Windows export is missing {name}')
    package(exported, output, profile, sources, args.exclude_movies, args.exclude_rotwk)
    if args.upload:
        from publish_windows_installer import publish
        publish(output, args.cdn_release, args.cdn_profile, args.cdn_public)


if __name__ == '__main__':
    try: main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(f'Windows workflow failed: {error}')
