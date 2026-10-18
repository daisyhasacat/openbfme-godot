#!/usr/bin/env python3
"""Merge local BFME2 1.09 Mordor walls into the optional, untracked wall pack.

Only wall objects, their commands/upgrades, and referenced art are imported.
Retail-format payloads remain local; never stage the generated BIG.
"""
import argparse
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / 'audio'))
from bigread import Mount, read_big


def archive(path):
    with path.open('rb') as stream:
        result = {}
        for name, offset, size in read_big(path):
            stream.seek(offset)
            result[name] = stream.read(size)
        return result


def clean(data):
    return '\n'.join(re.split(r';|//', line, maxsplit=1)[0].rstrip()
                     for line in data.decode('latin1').replace('\r', '').splitlines())


def blocks(text, kind):
    return {m[1].lower(): m[0] for m in re.finditer(
        r'(?ms)^' + kind + r'\s+(\w+)[^\n]*\n.*?^End\b[^\n]*', text)}


def write_big(path, files):
    size = 16 + sum(9 + len(name.encode('latin1')) for name in files)
    index, content = bytearray(), bytearray()
    for name, data in sorted(files.items()):
        index += struct.pack('>II', size + len(content), len(data)) + name.encode('latin1') + b'\0'
        content += data
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b'BIGF' + struct.pack('>III', size + len(content), len(files), size) + index + content)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='1.09 v3 BIG containing Mordor walls')
    parser.add_argument('--asset-source', type=Path, action='append', default=[], help='Earlier 1.09 archive supplying wall art')
    parser.add_argument('--rotwk', required=True)
    parser.add_argument('--bfme2', required=True)
    parser.add_argument('--base-pack', type=Path, required=True, help='Existing optional wall tower pack')
    parser.add_argument('--output', type=Path, default=Path('godot/mods/wall_towers.big'))
    args = parser.parse_args()
    source = {}
    for path in args.asset_source:
        source.update(archive(path))
    source.update(archive(args.source))
    files = archive(args.base_pack)
    retail = Mount(args.rotwk, args.bfme2)
    definitions = clean(source['data\\ini\\gamedata.ini'])
    macros = dict(re.findall(r'(?m)^\s*#define\s+(\S+)\s+([^\n]+)', definitions))

    def expand(text):
        for _ in range(20):
            updated = re.sub(r'(?<![\w.])(?:' + '|'.join(re.escape(key) for key in sorted(macros, key=len, reverse=True)) + r')(?![\w.])', lambda m: macros[m[0]], text)
            if updated == text:
                return text.replace('CONTROLBAR:Command_ConstructMordorCastleWallHub09v2', 'CONTROLBAR:Command_ConstructIsengardCastleWallHub')
            text = updated
        raise ValueError('Recursive wall macro')

    selected = {}
    for name in ('mordorcastlewalls.ini', 'mordorcastlewallhubexpansion.ini', 'mordorwallcatapultexpansion.ini'):
        key = 'data\\ini\\object\\evilfaction\\structures\\mordor\\' + name
        selected[key] = expand(clean(source[key]))
    catapults = blocks(clean(source['data\\ini\\object\\evilfaction\\units\\mordor\\catapult.ini']), 'ChildObject')
    selected['data\\ini\\object\\evilfaction\\units\\mordor\\openbfme_wallcatapult.ini'] = expand(catapults['mordorwallcatapult'])
    wall_text = '\n'.join(selected.values())
    for filename, kind in [('commandset.ini', 'CommandSet'), ('commandbutton.ini', 'CommandButton'), ('upgrade.ini', 'Upgrade')]:
        key = 'data\\ini\\' + filename
        candidates = blocks(clean(source[key]), kind)
        chosen = {name: text for name, text in candidates.items() if 'mordor' in name and 'wall' in name}
        base = clean(files.get(key, retail.get(key)))
        if kind == 'Upgrade':
            existing = blocks(base, kind)
            for name in re.findall(r'\bUpgrade_\w+', wall_text):
                if name.lower() not in existing and name.lower() in candidates:
                    chosen[name.lower()] = candidates[name.lower()]
        for name, text in chosen.items():
            pattern = r'(?ms)^' + kind + r'\s+' + re.escape(name) + r'\b[^\n]*\n.*?^End\b[^\n]*'
            base, count = re.subn(pattern, lambda _: expand(text), base, flags=re.I)
            if not count:
                base += '\n' + expand(text) + '\n'
        if filename == 'commandset.ini':
            porter = blocks(base, kind)['mordorportercommandset']
            command = 'Command_PorterConstructMordorWallHubOuter'
            if command.lower() not in porter.lower():
                occupied = {int(n) for n in re.findall(r'(?m)^\s*(\d+)\s*=', porter)}
                slot = next(n for n in range(1, 19) if n not in occupied)
                replacement = porter[:porter.rfind('End')] + f' {slot} = {command}\nEnd'
                base = base.replace(porter, replacement)
        files[key] = base.encode('latin1')
        wall_text += '\n' + '\n'.join(chosen.values())
    # Import portrait definitions only when referenced by the wall objects/buttons.
    tokens = set(re.findall(r'\b[\w.]+\b', wall_text.lower()))
    for key, data in source.items():
        if not key.endswith('.ini') or 'mappedimages' not in key:
            continue
        chosen = [text for name, text in blocks(clean(data), 'MappedImage').items() if name in tokens]
        if chosen:
            text = '\n'.join(chosen)
            selected['data\\ini\\mappedimages\\openbfme_mordor_walls.ini'] = selected.get('data\\ini\\mappedimages\\openbfme_mordor_walls.ini', '') + '\n' + text
            tokens.update(re.findall(r'\b[\w.]+\b', text.lower()))
    tokens.update(token.rsplit('.', 1)[0] for token in list(tokens))
    assets = {}
    for name in source:
        if name.startswith('art\\'):
            assets.setdefault(Path(name.replace('\\', '/')).stem, []).append(name)
    pending = [name for stem, names in assets.items() if stem in tokens or any(stem.startswith(prefix) for prefix in ('mbwall', 'mbwl', 'dolgul', 'dolgol')) for name in names]
    seen = set()
    while pending:
        key = pending.pop()
        if key in seen:
            continue
        seen.add(key)
        data = source[key]
        files[key] = data
        # W3D strings include texture files and extensionless hierarchy/animation names.
        for raw in re.findall(rb'[A-Za-z0-9_.-]{3,}', data):
            token = raw.decode('ascii').lower()
            for stem in (token, token.rsplit('.', 1)[0]):
                for dependency in assets.get(stem, []):
                    if dependency not in seen:
                        pending.append(dependency)
    files.update({key: text.encode('latin1') for key, text in selected.items()})
    write_big(args.output, files)
    print(f'{args.output}: imported {len(selected)} wall INIs and {len(seen)} art assets from {args.source.name}')


if __name__ == '__main__':
    main()
