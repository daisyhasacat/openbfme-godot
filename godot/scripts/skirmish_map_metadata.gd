extends RefCounted
## Persistent library metadata, independent of the slower object/feature scan.
const VERSION = 2
var directory := 'user://map-library-metadata'
var filesystem: RefCounted
var cache_hits := 0

func setup(fs: RefCounted, cache_root := 'user://map-library-metadata'):
 filesystem=fs;directory=cache_root

func _path(path: String) -> String:
 return directory.path_join(path.sha256_text()+'.cfg')

func _signature(path: String) -> String:
 if not FileAccess.file_exists(path):return ''
 var records: Array[String] = []
 if not _companions(path.get_base_dir(),'',records):return ''
 records.sort()
 # Hash the actual metadata sources: even edits preserving size and timestamp
 # invalidate names/descriptions. Other companions affect the preview alias.
 var identity=str(VERSION)+'\n'+str(filesystem.content_signature())+'\n'+FileAccess.get_sha256(path)+'\n'+'\n'.join(records)
 return identity.sha256_text()

func _companions(root: String, relative: String, records: Array[String]) -> bool:
 var dir=DirAccess.open(root.path_join(relative))
 if dir==null:return false
 for file in dir.get_files():
  var rel=relative.path_join(file)
  var disk=root.path_join(rel)
  var stream=FileAccess.open(disk,FileAccess.READ)
  if stream==null:return false
  records.append(rel+':'+str(stream.get_length())+':'+str(FileAccess.get_modified_time(disk)))
  if file.to_lower()=='map.str':records.append(rel+':'+FileAccess.get_sha256(disk))
 for child in dir.get_directories():
  if child.begins_with('.') or dir.is_link(child):continue
  if not _companions(root,relative.path_join(child),records):return false
 return true

func cached(path: String) -> Dictionary:
 var config=ConfigFile.new()
 if config.load(_path(path))!=OK:return {}
 var signature=_signature(path)
 if signature.is_empty() or config.get_value('metadata','signature','')!=signature:return {}
 var data=config.get_value('metadata','data',{})
 if not data is Dictionary:return {}
 for key in ['name','description','key','preview_name','content_id','path']:
  if not data.get(key) is String:return {}
 if data.path!=path or not data.get('starts') is Array or not data.get('ok',false):return {}
 cache_hits+=1
 return data

func save(entry: Dictionary) -> String:
 if not entry.get('ok',false):return ''
 var path=str(entry.path)
 var signature=_signature(path)
 if signature.is_empty():return 'Cannot fingerprint map metadata: '+path
 var status=DirAccess.make_dir_recursive_absolute(directory)
 if status!=OK:return 'Cannot create map metadata cache: '+error_string(status)
 var config=ConfigFile.new()
 config.set_value('metadata','signature',signature)
 # Keep map-derived metadata; UI state, favorites and feature scans have their own stores.
 var data={}
 for key in ['ok','name','description','key','preview_name','content_id','path','file_size','players','starts','library','official','snowy']:
  data[key]=entry[key]
 config.set_value('metadata','data',data)
 var target=_path(path)
 status=config.save(target+'.tmp')
 if status==OK:status=DirAccess.rename_absolute(target+'.tmp',target)
 return '' if status==OK else 'Cannot save map metadata cache: '+error_string(status)
