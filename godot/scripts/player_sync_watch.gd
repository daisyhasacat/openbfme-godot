extends Node
## Poll lightweight file metadata; wait for two stable observations before upload.
var worker: CanvasLayer
var observed = ""
var uploaded = ""
var configured = ""
var retry_at = 0
var elapsed = 0.0
var next_check = 0 ## a sync without a local change, so games and profiles from other devices arrive (merged files)
var warned = ""
var hashes = {} ## path -> [size, modified time, sha256]: a file is read again only when these change or it is recent

func _ready():
 process_mode=Node.PROCESS_MODE_ALWAYS
 worker=preload("res://scripts/player_sync.gd").new()
 worker.background=true
 add_child(worker)
 for child in worker.get_children():
  if child is CanvasItem:child.hide()

func _process(delta):
 elapsed+=delta
 if elapsed<2.0:return
 elapsed=0
 poll()

func poll():
 if worker.busy:return
 var config=ConfigFile.new()
 if config.load(worker.SETTINGS)!=OK:return
 var address=str(config.get_value("sync","server",""))
 var user=str(config.get_value("sync","username",""))
 if address.is_empty() or user.is_empty():return
 var identity=address+"/"+user
 if identity!=configured:
  configured=identity;observed="";uploaded="";retry_at=0
 worker.server.text=address;worker.username.text=user
 worker._roots()
 var files=worker.local_files()
 var keys=files.keys();keys.sort()
 var parts=PackedStringArray([worker.scope])
 for key in keys:
  var path=files[key]
  var file=FileAccess.open(path,FileAccess.READ)
  if file==null:return
  var size=file.get_length();file.close()
  # Modified times have one-second resolution, so a file can change twice within one stamp: a file written in the last few
  # seconds is hashed again on every observation, older files reuse their hash while size and modified time are unchanged.
  var modified=FileAccess.get_modified_time(path)
  var known:Array=hashes.get(path,[])
  if known.is_empty() or known[0]!=size or known[1]!=modified or Time.get_unix_time_from_system()-modified<=5:
   known=[size,modified,FileAccess.get_sha256(path)];hashes[path]=known
  parts.append(str(key)+":"+str(size)+":"+str(known[2]))
 var fingerprint="\n".join(parts).sha256_text()
 if fingerprint!=observed:
  observed=fingerprint;retry_at=0;return
 if Time.get_ticks_msec()<retry_at:return
 if fingerprint==uploaded and Time.get_ticks_msec()<next_check:return
 next_check=Time.get_ticks_msec()+60000
 await worker._sync()
 if worker.succeeded:
  uploaded=fingerprint;retry_at=0;warned=""
 else:
  retry_at=Time.get_ticks_msec()+30000
  # an unreachable server fails every 30 s: log each distinct failure once
  if worker.status.text!=warned:
   warned=worker.status.text
   push_warning("Automatic player sync: "+warned)
