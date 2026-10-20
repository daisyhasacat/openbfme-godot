import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).parent))
import merge_maps as m


def fixture(path, scripted=False):
    labels=['HeightMapData','BlendTileData','WorldInfo','MPPositionList','MPPositionInfo','SidesList','Teams','PlayerScriptsList','ScriptList','BuildLists','ObjectsList','Object','TriggerAreas','StandingWaterAreas','RiverAreas','StandingWaveAreas','NamedCameras','CameraAnimationList','WaypointsList','LibraryMapLists','GlobalLighting','PostEffectsChunk','EnvironmentData','mapName','mapDescription','playerName','playerIsHuman','playerDisplayName','playerFaction','playerAllies','playerEnemies','teamOwner','teamName','uniqueID','waypointID','waypointName','originalOwner','Player_1','Player_2']
    ids={name:i+1 for i,name in enumerate(labels)}
    def d(v):return m.write_dict(v,ids)
    def c(name,body,version=1):return m.Chunk(name,version,body)
    heights=struct.pack('<36H',*(10*y+x for y in range(6) for x in range(6)))
    palette=m.pack('iiiiiiii',1,1,1,1,0,1,1,0)+m.string('TestTerrain')+m.pack('ii',0,0)
    plane=bytearray(6);plane[1]=2
    blend=m.pack('i',36)+bytes(36*14)+bytes(plane)*5+bytes(36)+bytes(plane)+palette
    sides=[{'playerName':(3,'' )}]+[{'playerName':(3,'Player_'+str(i))} for i in (1,2)]
    objects=[]
    for i in (1,2):
        props={'uniqueID':(3,'Player_%d_Start'%i),'waypointName':(3,'Player_%d_Start'%i),'waypointID':(1,i)}
        objects.append(c('Object',m.pack('ffffi',i*10,i*10,0,0,0)+m.string('*Waypoints/Waypoint')+d(props),3))
    for x,flags,key in [(5,0,'Tree'),(-5,0,'Outside'),(-5,2,'RoadA'),(45,4,'RoadB')]:
        objects.append(c('Object',m.pack('ffffi',x,20,0,0,flags)+m.string('Road' if flags else 'Tree')+d({'uniqueID':(3,key)}),3))
    items=[c('HeightMapData',m.pack('iiiiiii',6,6,1,1,4,4,36)+heights,5),c('BlendTileData',blend,18),
       c('WorldInfo',d({'mapName':(4,'Source'),'mapDescription':(4,'')})),c('MPPositionList',m.chunk_bytes([c('MPPositionInfo',bytes(3)+m.pack('ii',-1,0))]*2,ids),0),
       c('SidesList',m.pack('Bi',1,3)+b''.join(d(s)+m.pack('i',0) for s in sides),6),c('Teams',m.pack('i',0)),
       c('PlayerScriptsList',m.chunk_bytes([c('ScriptList',b'x' if scripted else b'') for _ in sides],ids)),c('BuildLists',m.pack('i',2)+m.pack('IiIi',ids['Player_1']*256+3,0,ids['Player_2']*256+3,0)),c('ObjectsList',m.chunk_bytes(objects,ids),3)]
    for tag in ['TriggerAreas','StandingWaterAreas','RiverAreas','StandingWaveAreas','NamedCameras','CameraAnimationList','WaypointsList']:items.append(c(tag,m.pack('i',0)))
    for tag in ['LibraryMapLists','GlobalLighting','PostEffectsChunk','EnvironmentData']:items.append(c(tag,b''))
    toc=b'CkMp'+m.pack('i',len(ids))+b''.join(m.pack('B',len(k))+k.encode()+m.pack('I',v) for k,v in ids.items())
    path.write_bytes(toc+m.chunk_bytes(items,ids))


class MergeTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name);self.source=self.root/'source.map';fixture(self.source)
    def tearDown(self):self.tmp.cleanup()
    def merged(self,seam=0):
        data,report=m.merge([self.source],2,2,'Combined',seam);out=self.root/'merged.map';out.write_bytes(data);return m.Map(out),report
    def test_visible_crop_height_and_bitplanes(self):
        out,report=self.merged();self.assertEqual((out.width,out.height,out.visible),(10,10,(8,8)))
        heights=struct.unpack('<100H',out.heights)
        for x,y in [(1,1),(5,1),(1,5),(5,5)]:
            self.assertEqual(heights[y*10+x],11)
            self.assertTrue(out.planes[0][y*2+x//8] & (1<<(x%8)))
        self.assertEqual(report['visible_world'],[80,80]);self.assertEqual(report['starts'],8)
    def test_unique_starts_objects_and_clipped_roads(self):
        out,report=self.merged();records=[m.object_data(c,out.names) for c in m.chunks(out.by_name['ObjectsList'].body,out.names)]
        starts={p['waypointName'][1]:xyz[:2] for xyz,_,_,_,p in records if 'waypointName' in p}
        self.assertEqual(starts['Player_1_Start'],[10,50]);self.assertEqual(starts['Player_3_Start'],[50,50]);self.assertEqual(starts['Player_5_Start'],[10,10]);self.assertEqual(starts['Player_7_Start'],[50,10])
        names=[p['uniqueID'][1] for *_,p in records];self.assertEqual(len(names),len(set(names)))
        roads=[xyz[:2] for xyz,_,flags,_,_ in records if flags]
        self.assertEqual(roads,[[0,60],[40,60],[40,60],[80,60],[0,20],[40,20],[40,20],[80,20]])
        self.assertEqual(report['cropped_objects'],4)
    def test_seams_only_change_join_band(self):
        out,_=self.merged(1);h=struct.unpack('<100H',out.heights)
        self.assertEqual(h[5*10+5],33);self.assertEqual(h[1*10+1],11)
    def test_script_refused_and_source_unchanged(self):
        original=self.source.read_bytes();fixture(self.root/'scripted.map',True)
        with self.assertRaisesRegex(ValueError,'Scripted maps'):m.merge([self.root/'scripted.map'],2,2,'x')
        self.assertEqual(self.source.read_bytes(),original)
    def test_existing_output_refused(self):
        out=self.root/'existing.map';out.write_bytes(b'keep')
        with self.assertRaises(SystemExit):m.main(['--source',str(self.source),'--out',str(out),'--name','x'])
        self.assertEqual(out.read_bytes(),b'keep')

if __name__=='__main__':unittest.main()
