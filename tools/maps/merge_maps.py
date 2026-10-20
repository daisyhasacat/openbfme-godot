#!/usr/bin/env python3
"""Tile compatible RotWK maps at their visible borders; never modify source maps.

Inputs must share the chunk dictionary and terrain palette (forks / copies of a
map do). Nonempty scripts, build lists, water and cinematics are refused rather
than copied with dangling references. See MERGE.md for the mounted-map workflow.
"""
import argparse
from array import array
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import sys


def pack(fmt, *values):
    return struct.pack('<' + fmt, *values)


class Reader:
    def __init__(self, data): self.data, self.pos = data, 0
    def raw(self, count):
        if count < 0 or self.pos + count > len(self.data): raise ValueError('Truncated map payload')
        result = self.data[self.pos:self.pos + count]; self.pos += count; return result
    def get(self, fmt): return struct.unpack('<' + fmt, self.raw(struct.calcsize('<' + fmt)))[0]
    def string(self, wide=False):
        return self.raw(self.get('H') * (2 if wide else 1)).decode('utf-16-le' if wide else 'latin-1')
    def done(self):
        if self.pos != len(self.data): raise ValueError('Unread map payload bytes')


def string(value, wide=False):
    raw = value.encode('utf-16-le' if wide else 'latin-1')
    return pack('H', len(raw) // (2 if wide else 1)) + raw


def read_dict(r, names):
    result = {}
    for _ in range(r.get('H')):
        key = r.get('I'); kind = key & 255; name = names[key >> 8]
        if kind in (0, 1, 2): value = r.get({0:'B', 1:'i', 2:'f'}[kind])
        elif kind in (3, 4): value = r.string(kind == 4)
        else: raise ValueError('Unknown dictionary value type')
        result[name] = (kind, value)
    return result


def write_dict(data, ids):
    result = bytearray(pack('H', len(data)))
    for name, (kind, value) in data.items():
        result += pack('I', ids[name] * 256 + kind)
        result += pack({0:'B', 1:'i', 2:'f'}[kind], value) if kind < 3 else string(value, kind == 4)
    return bytes(result)


@dataclass
class Chunk:
    name: str
    version: int
    body: bytes


def chunks(data, names):
    r = Reader(data); result = []
    while r.pos < len(data):
        name, version, size = r.get('I'), r.get('H'), r.get('i')
        result.append(Chunk(names[name], version, r.raw(size)))
    return result


def chunk_bytes(items, ids):
    return b''.join(pack('IHi', ids[c.name], c.version, len(c.body)) + c.body for c in items)


def decode(data):
    if data[:4] == b'CkMp': return data
    if data[:4] != b'EAR\0': raise ValueError('Expected CkMp or EAR map')
    # RefPack envelope, same documented commands as oracle/census.py; integer-only.
    size = struct.unpack_from('<I', data, 4)[0]; r = Reader(data[8:])
    flags, marker = r.get('B'), r.get('B')
    if marker != 251 or flags & 62 != 16: raise ValueError('Invalid RefPack header')
    width = 4 if flags & 128 else 3
    if flags & 1: r.raw(width)
    expected = int.from_bytes(r.raw(width), 'big'); out = bytearray()
    if expected != size or size > 512 * 1024 * 1024: raise ValueError('Invalid decompressed map size')
    while True:
        c = r.get('B'); distance = length = 0
        if c < 128:
            b = r.get('B'); literal, length, distance = c & 3, ((c & 28) >> 2) + 3, ((c & 96) << 3) + b + 1
        elif c < 192:
            b, d = r.get('B'), r.get('B'); literal, length, distance = b >> 6, (c & 63) + 4, ((b & 63) << 8) + d + 1
        elif c < 224:
            b, d, e = r.get('B'), r.get('B'), r.get('B'); literal, length, distance = c & 3, ((c & 12) << 6) + e + 5, ((c & 16) << 12) + (b << 8) + d + 1
        else: literal = ((c & 31) + 1) * 4 if c < 252 else c & 3
        out += r.raw(literal)
        if length:
            if distance > len(out): raise ValueError('Invalid RefPack back-reference')
            for _ in range(length): out.append(out[-distance])
        if len(out) > expected: raise ValueError('RefPack exceeds declared size')
        if c >= 252: break
    r.done()
    if len(out) != expected: raise ValueError('RefPack size mismatch')
    return bytes(out)


class Map:
    def __init__(self, path):
        self.path = Path(path); raw = self.path.read_bytes(); self.sha = hashlib.sha256(raw).hexdigest()
        r = Reader(decode(raw))
        if r.raw(4) != b'CkMp': raise ValueError('Missing chunk signature')
        self.names = {}
        for _ in range(r.get('i')):
            name = r.raw(r.get('B')).decode('latin-1'); self.names[r.get('I')] = name
        self.items = chunks(r.raw(len(r.data) - r.pos), self.names)
        self.by_name = {c.name:c for c in self.items}
        if len(self.by_name) != len(self.items): raise ValueError('Duplicate root chunks')
        hm = self.by_name['HeightMapData']
        if hm.version != 5: raise ValueError('HeightMapData v5 required')
        h = Reader(hm.body); self.width, self.height, self.border = h.get('i'), h.get('i'), h.get('i')
        bounds = [(h.get('i'), h.get('i')) for _ in range(h.get('i'))]
        if min(self.width, self.height) <= 2*self.border or h.get('i') != self.width*self.height: raise ValueError('Invalid height dimensions')
        self.visible = (self.width - 2*self.border, self.height - 2*self.border)
        if bounds != [self.visible]: raise ValueError('Only a single visible boundary is supported')
        self.heights = h.raw(self.width*self.height*2); h.done()
        self._blend()

    def _blend(self):
        c = self.by_name['BlendTileData']; v = c.version
        if v not in (8, 11, 14, 15, 16, 18): raise ValueError('Unsupported BlendTileData version')
        r = Reader(c.body); count = self.width*self.height
        if r.get('i') != count: raise ValueError('Blend/height dimensions differ')
        self.arrays = [(2, r.raw(count*2))]
        for _ in range(3): self.arrays.append((4 if v >= 14 else 2, r.raw(count*(4 if v >= 14 else 2))))
        self.planes = [r.raw(((self.width+7)//8)*self.height)]
        for since in (10, 11, 14, 15):
            if v >= since: self.planes.append(r.raw(((self.width+7)//8)*self.height))
        self.flammability = r.raw(count) if v >= 16 else None
        if v >= 17: self.planes.append(r.raw(((self.width+7)//8)*self.height))
        self.palette = r.raw(len(r.data)-r.pos)


def remap(value, tile, starts):
    return re.sub(r'Player_(\d+)', lambda m:'Player_'+str(int(m[1]) + tile*starts), value)


def object_data(c, names):
    r = Reader(c.body); xyz = [r.get('f') for _ in range(3)]; angle = r.get('f'); flags = r.get('i'); name = r.string()
    properties = read_dict(r, names); r.done()
    return xyz, angle, flags, name, properties


def clip_line(a, b, width, height):
    lo, hi = 0.0, 1.0
    for axis, maximum in ((0,width), (1,height)):
        delta = b[axis]-a[axis]
        if delta == 0:
            if not 0 <= a[axis] <= maximum: return None
        else:
            t0,t1 = -a[axis]/delta, (maximum-a[axis])/delta
            lo,hi = max(lo,min(t0,t1)), min(hi,max(t0,t1))
    if lo > hi: return None
    return [[a[i]+t*(b[i]-a[i]) for i in range(3)] for t in (lo,hi)]


def merge(paths, columns, rows, name, seam=16):
    if columns < 1 or rows < 1 or columns*rows > 16: raise ValueError('Grid must have 1–16 tiles')
    if len(paths) == 1: paths = paths * (columns*rows)
    if len(paths) != columns*rows: raise ValueError('Supply one map or one per grid position (top-left first)')
    sources = [Map(p) for p in paths]; first = sources[0]; ids = {v:k for k,v in first.names.items()}
    allowed = {'HeightMapData','BlendTileData','WorldInfo','MPPositionList','SidesList','LibraryMapLists','Teams','PlayerScriptsList','BuildLists','ObjectsList','TriggerAreas','StandingWaterAreas','RiverAreas','StandingWaveAreas','GlobalLighting','PostEffectsChunk','EnvironmentData','NamedCameras','CameraAnimationList','WaypointsList'}
    for m in sources:
        unknown = set(m.by_name) - allowed
        if unknown: raise ValueError('Unsupported root chunks: '+', '.join(sorted(unknown)))
        if (m.names, m.visible, m.border, m.palette, m.by_name['BlendTileData'].version) != (first.names, first.visible, first.border, first.palette, first.by_name['BlendTileData'].version):
            raise ValueError('Tiles must share their chunk dictionary, visible dimensions, border and terrain palette')
        for tag in ('TriggerAreas','StandingWaterAreas','RiverAreas','StandingWaveAreas','NamedCameras','CameraAnimationList','WaypointsList'):
            if tag in m.by_name and m.by_name[tag].body != pack('i',0): raise ValueError('Nonempty '+tag+' needs explicit reference/geometry remapping; merge refused')
        script_lists = chunks(m.by_name['PlayerScriptsList'].body,m.names)
        if any(c.name != 'ScriptList' or c.body for c in script_lists): raise ValueError('Scripted maps need script reference remapping; merge refused')
        for tag in ('SidesList','LibraryMapLists','Teams','BuildLists','GlobalLighting','PostEffectsChunk','EnvironmentData'):
            if m.by_name[tag].body != first.by_name[tag].body: raise ValueError('Incompatible global chunk '+tag)
    vw,vh = first.visible; border=first.border; width,height=columns*vw+2*border,rows*vh+2*border
    if width*height > 16_000_000: raise ValueError('Merged terrain exceeds 16 million samples')
    # Display order top-left, top-right, then the row below; world Y grows north.
    def owner(x,y):
        gx,gy=x-border,y-border
        col=min(columns-1,max(0,gx//vw)); bottom=min(rows-1,max(0,gy//vh)); tile=(rows-1-bottom)*columns+col
        m=sources[tile]; sx=min(m.width-1,max(0,gx-col*vw+m.border)); sy=min(m.height-1,max(0,gy-bottom*vh+m.border))
        return tile,sy*m.width+sx
    sample_owners=[owner(x,y) for y in range(height) for x in range(width)]
    def raster(buffers,size):
        result=bytearray(width*height*size)
        for dst,(tile,src) in enumerate(sample_owners): result[dst*size:(dst+1)*size]=buffers[tile][src*size:(src+1)*size]
        return result
    heights=array('H'); heights.frombytes(raster([m.heights for m in sources],2))
    if sys.byteorder!='little': heights.byteswap()
    seam=max(0,min(seam,min(vw,vh)//4))
    if seam:
        for vertical,count,span in ((True,columns,vw),(False,rows,vh)):
            for edge in range(1,count):
                line=border+edge*span
                for along in range(height if vertical else width):
                    def at(c): return along*width+c if vertical else c*width+along
                    a,b=heights[at(line-seam)],heights[at(line+seam)]
                    for d in range(-seam+1,seam): heights[at(line+d)]=round(a+(b-a)*(d+seam)/(2*seam))
    if sys.byteorder!='little': heights.byteswap()
    hm=pack('iiiiiii',width,height,border,1,columns*vw,rows*vh,width*height)+heights.tobytes()
    blend=bytearray(pack('i',width*height))
    for i,(size,_) in enumerate(first.arrays): blend+=raster([m.arrays[i][1] for m in sources],size)
    def plane(i):
        out=bytearray(((width+7)//8)*height)
        for dst,(tile,src) in enumerate(sample_owners):
            m=sources[tile]; sx,sy=src % m.width,src // m.width
            if m.planes[i][sy*((m.width+7)//8)+sx//8] & (1 << (sx%8)):
                x,y=dst%width,dst//width;out[y*((width+7)//8)+x//8] |= 1 << (x%8)
        return out
    v=first.by_name['BlendTileData'].version; plane_count=len(first.planes)-(1 if v>=17 else 0)
    for i in range(plane_count): blend+=plane(i)
    if first.flammability is not None: blend+=raster([m.flammability for m in sources],1)
    if v>=17: blend+=plane(plane_count)
    blend+=first.palette
    start_names=[]
    for c in chunks(first.by_name['ObjectsList'].body,first.names):
        d=object_data(c,first.names)[4]; match=re.fullmatch(r'Player_(\d+)_Start',d.get('waypointName',(3,''))[1])
        if match:start_names.append(int(match[1]))
    starts=max(start_names,default=0)
    if sorted(start_names)!=list(range(1,starts+1)) or not starts: raise ValueError('Player starts must be contiguous and unique')
    if starts*len(sources)>32:raise ValueError('Merged map exceeds 32 start positions')
    objects=[]; cropped=0; waypoint_max=0
    for m in sources:
        for c in chunks(m.by_name['ObjectsList'].body,m.names): waypoint_max=max(waypoint_max,int(object_data(c,m.names)[4].get('waypointID',(1,0))[1]))
    for tile,m in enumerate(sources):
        dx,dy=(tile%columns)*vw*10,(rows-1-tile//columns)*vh*10
        records=[object_data(c,m.names) for c in chunks(m.by_name['ObjectsList'].body,m.names)]
        index=0
        while index<len(records):
            recs=[records[index]]; index+=1
            if recs[0][2] & 2:
                if index==len(records) or not records[index][2]&4:raise ValueError('Unpaired road endpoint')
                recs.append(records[index]);index+=1
                clipped=clip_line(recs[0][0],recs[1][0],vw*10,vh*10)
                if clipped is None: cropped+=2;continue
                for rec,p in zip(recs,clipped):rec[0][:]=p
            elif recs[0][2]&4:raise ValueError('Unpaired road endpoint')
            elif not (0<=recs[0][0][0]<=vw*10 and 0<=recs[0][0][1]<=vh*10):cropped+=1;continue
            for xyz,angle,flags,template,props in recs:
                for key,(kind,value) in list(props.items()):
                    if isinstance(value,str):
                        value=remap(value,tile,starts)
                        if key in ('uniqueID','objectName','waypointName') and value and not re.fullmatch(r'Player_\d+_Start',value):value=f'tile{tile+1}_'+value
                        props[key]=(kind,value)
                if 'waypointID' in props:props['waypointID']=(1,int(props['waypointID'][1])+tile*(waypoint_max+1))
                body=pack('ffffi',xyz[0]+dx,xyz[1]+dy,xyz[2],angle,flags)+string(template)+write_dict(props,ids)
                objects.append(Chunk('Object',3,body))
    # Empty player script/build lists and generic side/team dictionaries can be
    # extended safely. Nonempty lists were explicitly refused above.
    r=Reader(first.by_name['SidesList'].body); lead=r.get('B'); sides=[]
    for _ in range(r.get('i')):
        side=read_dict(r,first.names)
        if r.get('i')!=0:raise ValueError('Nonempty side build list')
        sides.append(side)
    r.done()
    teams_r=Reader(first.by_name['Teams'].body);teams=[read_dict(teams_r,first.names) for _ in range(teams_r.get('i'))];teams_r.done()
    builds_r=Reader(first.by_name['BuildLists'].body);builds=[]
    for _ in range(builds_r.get('i')):
        key=builds_r.get('I')
        if builds_r.get('i')!=0:raise ValueError('Nonempty faction build list')
        builds.append(first.names[key>>8])
    builds_r.done()
    for tile in range(1,len(sources)):
        for original in list(sides[:len(script_lists)]):
            if re.fullmatch(r'Player_\d+',original.get('playerName',(3,''))[1]):sides.append({k:(t,remap(val,tile,starts) if isinstance(val,str) else val) for k,(t,val) in original.items()})
        for original in list(teams[:len(script_lists)]):
            if re.fullmatch(r'Player_\d+',original.get('teamOwner',(3,''))[1]):teams.append({k:(t,remap(val,tile,starts) if isinstance(val,str) else val) for k,(t,val) in original.items()})
        for original in list(builds[:len(script_lists)]):
            if re.fullmatch(r'Player_\d+',original):builds.append(remap(original,tile,starts))
    for key in builds:
        if key not in ids:ids[key]=max(ids.values())+1
    world=read_dict(Reader(first.by_name['WorldInfo'].body),first.names)
    world['mapName']=(4,name);world['mapDescription']=(4,f'{columns} x {rows} visible-border merge; {starts*len(sources)} starts, up to 16 active players.')
    replaced={'HeightMapData':hm,'BlendTileData':bytes(blend),'ObjectsList':chunk_bytes(objects,ids),'WorldInfo':write_dict(world,ids),
        'MPPositionList':first.by_name['MPPositionList'].body*len(sources),
        'SidesList':pack('Bi',lead,len(sides))+b''.join(write_dict(d,ids)+pack('i',0) for d in sides),
        'Teams':pack('i',len(teams))+b''.join(write_dict(d,ids) for d in teams),
        'PlayerScriptsList':chunk_bytes([Chunk('ScriptList',1,b'') for _ in sides],ids),
        'BuildLists':pack('i',len(builds))+b''.join(pack('Ii',ids[key]*256+3,0) for key in builds)}
    toc=b'CkMp'+pack('i',len(ids))+b''.join(pack('B',len(key.encode('latin-1')))+key.encode('latin-1')+pack('I',value) for key,value in ids.items())
    result=toc+chunk_bytes([Chunk(c.name,c.version,replaced.get(c.name,c.body)) for c in first.items],ids)
    manifest={'version':1,'name':name,'columns':columns,'rows':rows,'tile_order':'top-left to bottom-right','visible_cells':[columns*vw,rows*vh],'visible_world':[columns*vw*10,rows*vh*10],'border_cells':border,'seam_cells':seam,'starts':starts*len(sources),'objects':len(objects),'cropped_objects':cropped,'sources':[{'path':str(m.path),'sha256':m.sha} for m in sources]}
    return result,manifest


def worker(godot, mode, path, temporary, key=None):
    output=temporary/'result.json';output.unlink(missing_ok=True)
    request={'mode':mode,'path':str(path.resolve()),'output':str(output)}
    if key:request['key']=key
    request_path=temporary/'request.json';request_path.write_text(json.dumps(request))
    process=subprocess.run([str(godot),'--headless','--path',str(Path(__file__).resolve().parents[2]/'godot'),
        '--script','res://scripts/map_merge.gd','--',str(request_path)],capture_output=True,text=True,timeout=180)
    if not output.exists():raise ValueError('Map worker failed: '+(process.stdout+process.stderr)[-3000:])
    result=json.loads(output.read_text())
    if not result.get('ok'):raise ValueError(str(result))
    return result


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    group=p.add_mutually_exclusive_group(required=True)
    group.add_argument('--source',type=Path,action='append');group.add_argument('--map-key',help='Exact mounted archive key; requires --godot')
    p.add_argument('--godot',type=Path);p.add_argument('--install',action='store_true')
    p.add_argument('--columns',type=int,default=2);p.add_argument('--rows',type=int,default=2);p.add_argument('--name',required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--seam-cells',type=int,default=16)
    args=p.parse_args(argv)
    try:
        for source in args.source or []:
            if any((source.parent / companion).exists() for companion in ('map.ini','map.str')):raise ValueError('Source companions need explicit merging')
        if args.out.exists():raise ValueError('Output already exists; choose a new map folder/name')
        if (args.map_key or args.install) and not args.godot:raise ValueError('--map-key / --install require --godot')
        with tempfile.TemporaryDirectory(prefix='openbfme-map-merge-') as directory:
            temporary=Path(directory);sources=args.source
            if args.map_key:
                source=temporary/'source.map';origin=worker(args.godot,'extract',source,temporary,args.map_key);sources=[source]
            result,manifest=merge(sources,args.columns,args.rows,args.name,args.seam_cells)
            # An exact repeated mounted source shares its global companion definitions.
            # Mixed disk sources with companions remain refused above.
            if args.map_key:
                for entry in manifest['sources']:entry['path']=args.map_key;entry['archive']=origin['archive']
            args.out.parent.mkdir(parents=True,exist_ok=True);args.out.write_bytes(result)
            if args.map_key and origin.get('companions'):
                manifest['companions']={}
                for companion in origin['companions']:
                    data=(temporary/companion).read_bytes()
                    (args.out.parent/companion).write_bytes(data)
                    manifest['companions'][companion]=hashlib.sha256(data).hexdigest()
            if args.godot:manifest['validation']=worker(args.godot,'inspect',args.out,temporary)
            if args.install:manifest['installation']=worker(args.godot,'install',args.out,temporary)
            args.out.with_suffix('.merge.json').write_text(json.dumps(manifest,indent=2)+'\n')
            print(json.dumps(manifest,indent=2));return 0
    except (ValueError,KeyError,OSError,struct.error,subprocess.TimeoutExpired) as e:p.error(str(e))

if __name__=='__main__':raise SystemExit(main())
