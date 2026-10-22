extends CanvasLayer
## Player sync UI and transfer worker. Never sync installation data or device paths.
const SETTINGS = "user://player-sync.cfg"
const FILES = ["Skirmish.ini", "game_records.jsonl", "map-favorites.cfg", "controller_options.cfg", "adaptive-hud-layout.json", "wall_balance.cfg"]
const FOLDERS = ["Save", "Profiles", "Replays", "Campaign", "HeroStats", "map_edits"]
var server: LineEdit
var username: LineEdit
var status: Label
var button: Button
var request: HTTPRequest
var busy = false
var roots = {}
var scope = "rotwk-"
var background = false
var succeeded = false

func _ready():
 add_to_group("player_sync_workers")
 layer = 130
 var backdrop = ColorRect.new();backdrop.color=Color(0,0,0,.95)
 backdrop.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT);add_child(backdrop)
 var panel = VBoxContainer.new();panel.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
 panel.offset_left=48;panel.offset_top=48;panel.offset_right=-48;panel.offset_bottom=-48;add_child(panel)
 var title=Label.new();title.text="Player sync";panel.add_child(title)
 var config=ConfigFile.new();config.load(SETTINGS)
 server=LineEdit.new();server.placeholder_text="Server URL";server.text=str(config.get_value("sync","server","http://192.168.121.42:8766"));panel.add_child(server)
 username=LineEdit.new();username.placeholder_text="Username (letters, numbers, _ or -)";username.text=str(config.get_value("sync","username",""));panel.add_child(username)
 var help=Label.new();help.text="Saves, profiles, heroes, hero statistics, replays and player preferences.\nUse the same username on each device. Changes upload automatically after configuring sync. Use Sync now in the main menu to download changes.\nConflicting files are kept locally in sync-conflicts. Deletions are not shared.";help.autowrap_mode=TextServer.AUTOWRAP_WORD_SMART;panel.add_child(help)
 button=Button.new();button.text="Sync now";button.pressed.connect(_sync);panel.add_child(button)
 status=Label.new();status.autowrap_mode=TextServer.AUTOWRAP_WORD_SMART;panel.add_child(status)
 var close=Button.new();close.text="Close";close.pressed.connect(func():if not busy:queue_free());panel.add_child(close)
 request=HTTPRequest.new();request.max_redirects=0;request.use_threads=true;request.timeout=120;request.body_size_limit=128*1024*1024;add_child(request)
 preload("res://scripts/menu_accessibility.gd").prepare(self)

func _call(url:String, method:int=HTTPClient.METHOD_GET, data:PackedByteArray=PackedByteArray(), base:String="") -> Array:
 var headers=PackedStringArray(["Content-Type: application/octet-stream", "If-Match: "+base])
 if request.request_raw(url,headers,method,data)!=OK:return []
 return await request.request_completed

func _scan(folder:String, prefix:String, result:Dictionary):
 if not DirAccess.dir_exists_absolute(folder):return
 var dir=DirAccess.open(folder)
 if dir==null:return
 for name in dir.get_files():
  if not name.begins_with(".") and not name.ends_with(".sync-part") and not dir.is_link(name):result[prefix+name]=folder.path_join(name)
 for name in dir.get_directories():
  if not name.begins_with(".") and not dir.is_link(name):_scan(folder.path_join(name),prefix+name+"/",result)

func _roots():
 var release=get_tree().root.get_node_or_null("Release")
 scope=(str(release.selected_game()) if release!=null else "rotwk")+"-"
 roots={scope+"player":"user://"}
 # game.gd owns the actual save/hero folders, including explicit overrides.
 var game=get_tree().current_scene
 if game!=null and game.get("_save_flow")!=null:
  var save:String=game._save_flow.save_dir()
  if not save.is_empty():roots[scope+"saves"]=save
  var heroes:String=game._cah_profile_dir()
  if not heroes.is_empty() and ProjectSettings.globalize_path(heroes).simplify_path()!=ProjectSettings.globalize_path(save).simplify_path():roots[scope+"heroes"]=heroes

func _target(key:String)->String:
 var parts=key.split("/")
 if parts.size()<2 or not roots.has(parts[0]):return ""
 for part in parts:
  if part in ["", ".", ".."] or ":" in part or "\\" in part:return ""
 var relative=key.substr(key.find("/")+1)
 if parts[0]==scope+"player" and not (relative in player_files() or (parts.size()==2 and relative.ends_with("RealTimeStats.ini")) or parts[1] in FOLDERS):return ""
 var current=ProjectSettings.globalize_path(str(roots[parts[0]]))
 for part in relative.split("/"):
  var dir=DirAccess.open(current)
  if dir!=null and dir.is_link(part):return ""
  current=current.path_join(part)
 return current

## the user-folder files of the selected game: BFME1 keeps its profiles in bfme1-Skirmish.ini (game.gd), RotWK and BFME2 in Skirmish.ini
func player_files()->Array:
 var files=FILES.duplicate()
 if scope=="bfme1-":files[files.find("Skirmish.ini")]="bfme1-Skirmish.ini"
 return files

## Skirmish.ini holds every profile in one file, so whole-file sync lets a stale or empty list replace profiles made on another device
## (or by this game before a download). Profile files are merged instead: every profile of both versions is kept (deletions are not
## shared); where both have an entry, this device's wins when it changed since the last sync, else the server's; this device's current
## profile stays current.
func _is_profiles(key:String)->bool:
 return key==scope+"player/Skirmish.ini" or key==scope+"player/bfme1-Skirmish.ini"

## game_records.jsonl (the Profile / Stats pages' games, GameRecords.h) is merged as well: every game of both versions once (by its id),
## oldest first, the newest 5000 kept like the game's own reader. The game only appends to the file and can reload it, so this merge is
## written locally even by automatic sync and the stats pages are refreshed at once.
func _is_records(key:String)->bool:
 return key==scope+"player/game_records.jsonl"

func merge_records(ours:String, theirs:String)->String:
 var seen={};var rows=[]
 for text in [ours,theirs]:
  for line in text.split("\n",false):
   if line.strip_edges().is_empty():continue
   var record=JSON.parse_string(line)
   var id=str(record.get("id","")) if record is Dictionary and not str(record.get("id","")).is_empty() else line
   if seen.has(id):continue
   seen[id]=true
   rows.append([int(record.get("end",0)) if record is Dictionary else 0,id,line])
 rows.sort_custom(func(a,b):return a[0]<b[0] or (a[0]==b[0] and a[1]<b[1]))
 if rows.size()>5000:rows=rows.slice(rows.size()-5000)
 return "".join(rows.map(func(r):return r[2]+"\n"))

func _ini(text:String)->Dictionary:
 var values={}
 for line in text.split("\n"):
  var at=line.find("=")
  if at>0:values[line.substr(0,at).strip_edges()]=line.substr(at+1).strip_edges()
 return values

func merge_profiles(ours:String, theirs:String, ours_win:=true)->String:
 var mine=_ini(ours);var other=_ini(theirs)
 var merged=other.duplicate() if ours_win else mine.duplicate()
 merged.merge(mine if ours_win else other,true)
 # UserNames: quoted-printable names, each followed by an encoded "," (_2C_00); names compare without case like the game's
 var names=PackedStringArray();var seen={}
 for list in [str(mine.get("UserNames","")),str(other.get("UserNames",""))]:
  for name in list.split("_2C_00",false):
   if not name.strip_edges().is_empty() and not seen.has(name.to_lower()):seen[name.to_lower()]=true;names.append(name)
 if not names.is_empty() or merged.has("UserNames"):merged["UserNames"]="".join(Array(names).map(func(n):return n+"_2C_00"))
 merged["CurrentUserName"]=mine.get("CurrentUserName","") if not str(mine.get("CurrentUserName","")).is_empty() else other.get("CurrentUserName","")
 if str(merged["CurrentUserName"]).is_empty() and not (mine.has("CurrentUserName") or other.has("CurrentUserName")):merged.erase("CurrentUserName")
 # the game's writer: every key in byte order as "key = value" (OptionPreferences::text), with the file's own line ending
 var eol="\r\n" if "\r\n" in ours or (ours.is_empty() and "\r\n" in theirs) else "\n"
 var keys=merged.keys();keys.sort()
 var out=""
 for key in keys:out+=str(key)+" = "+str(merged[key])+eol
 return out

func local_files()->Dictionary:
 var local={}
 for key in roots:
  if key==scope+"player":
   for name in DirAccess.get_files_at("user://"):
    if (name in player_files() or name.ends_with("RealTimeStats.ini")) and FileAccess.file_exists("user://"+name):local[scope+"player/"+name]="user://"+name
   for name in FOLDERS:_scan("user://"+name,scope+"player/"+name+"/",local)
  else:_scan(roots[key],key+"/",local)
 return local

func _sync():
 succeeded=false
 if busy:return
 for worker in get_tree().get_nodes_in_group("player_sync_workers"):
  if worker!=self and worker.busy:
   if not background:status.text="Automatic sync is uploading changes; try again in a moment."
   return
 var game=get_tree().current_scene
 if not background and game!=null and game.get("_save_flow")!=null and game._save_flow._in_game:
  status.text="Return to the main menu before syncing.";return
 var regex=RegEx.new();regex.compile("^[A-Za-z0-9_-]{1,64}$")
 if regex.search(username.text)==null:status.text="Enter a username using 1–64 letters, numbers, _ or -.";return
 var origin=server.text.strip_edges().trim_suffix("/")
 if not (origin.begins_with("http://") or origin.begins_with("https://")):status.text="Enter an http:// or https:// server URL.";return
 busy=true;button.disabled=true;server.editable=false;username.editable=false
 var config=ConfigFile.new();config.set_value("sync","server",origin);config.set_value("sync","username",username.text)
 if not background and config.save(SETTINGS)!=OK:_finish("Could not save sync settings.");return
 var url=origin+"/v1/"+username.text
 status.text="Reading server files…"
 var response=await _call(url+"/manifest/current")
 if response.is_empty() or response[0]!=HTTPRequest.RESULT_SUCCESS or response[1]!=200:_finish("Cannot reach sync server.");return
 var remote=JSON.parse_string(response[3].get_string_from_utf8())
 if not remote is Dictionary:_finish("Invalid server manifest.");return
 _roots()
 var local=local_files()
 var state_path="user://sync-state-"+(origin+"/"+username.text).sha256_text()+".cfg"
 var state=ConfigFile.new();state.load(state_path)
 var keys=local.keys()
 for key in remote:
  if str(key).begins_with(scope) and not keys.has(key):keys.append(key)
 var count=0;var conflicts=0
 for key in keys:
  var target=_target(key)
  if target.is_empty():_finish("Server returned an unsupported path: "+key);return
  var old=str(state.get_value("files",key,""));var theirs=str(remote.get(key,""))
  var ours=FileAccess.get_sha256(target) if FileAccess.file_exists(target) else ""
  status.text="Syncing %d / %d: %s"%[count+1,keys.size(),key]
  if (_is_profiles(key) or _is_records(key)) and ours!=theirs and not ours.is_empty() and not theirs.is_empty():
   var fetched=await _call(url+"/file/"+str(key).uri_encode())
   if fetched.is_empty() or fetched[0]!=HTTPRequest.RESULT_SUCCESS or fetched[1]!=200:_finish("Download failed: "+key);return
   var check_hash=HashingContext.new();check_hash.start(HashingContext.HASH_SHA256);check_hash.update(fetched[3])
   if check_hash.finish().hex_encode()!=theirs:_finish("Download checksum failed: "+key);return
   var local_bytes=FileAccess.get_file_as_bytes(target)
   if FileAccess.get_sha256(target)!=ours:_finish("File changed during sync; try again: "+key);return
   var merged_text=merge_records(local_bytes.get_string_from_utf8(),fetched[3].get_string_from_utf8()) if _is_records(key) else merge_profiles(local_bytes.get_string_from_utf8(),fetched[3].get_string_from_utf8(),ours!=old)
   var merged_bytes=merged_text.to_utf8_buffer()
   var merged_hash_ctx=HashingContext.new();merged_hash_ctx.start(HashingContext.HASH_SHA256);merged_hash_ctx.update(merged_bytes)
   var merged_hash=merged_hash_ctx.finish().hex_encode()
   if merged_hash!=theirs:
    var put=await _call(url+"/file/"+str(key).uri_encode(),HTTPClient.METHOD_PUT,merged_bytes,theirs)
    if put.is_empty() or put[0]!=HTTPRequest.RESULT_SUCCESS or put[1]!=200:_finish("Upload failed or another device changed the file; sync again: "+key);return
   # the running game holds its profiles in memory: automatic sync only uploads; Sync now (main menu, then restart) writes the merge.
   # Game records are only appended to by the game and are reloaded below, so both write them.
   if merged_hash!=ours and (not background or _is_records(key)):
    if FileAccess.get_sha256(target)!=ours:_finish("File changed during sync; try again: "+key);return
    var part=FileAccess.open(target+".sync-part",FileAccess.WRITE)
    if part==null:_finish("Cannot write: "+key);return
    part.store_buffer(merged_bytes);var part_error=part.get_error();part.close()
    if part_error!=OK or FileAccess.get_sha256(target+".sync-part")!=merged_hash:
     DirAccess.remove_absolute(target+".sync-part");_finish("Incomplete disk write: "+key);return
    if DirAccess.rename_absolute(target+".sync-part",target)!=OK:_finish("Cannot replace: "+key);return
    # the stats pages read the records the game loaded: hand them the merged file
    if _is_records(key) and game!=null and game.get("_platform_ready") and game.get("_shell")!=null:game._shell.set_game_records(merged_text)
   state.set_value("files",key,merged_hash)
   if state.save(state_path)!=OK:_finish("Cannot save sync state.");return
   count+=1
   continue
  if background and ours!=theirs and not theirs.is_empty() and (ours.is_empty() or ours==old or theirs!=old):
   # Background work must not replace files held in memory by the game.
   if not ours.is_empty() and ours!=old:
    conflicts+=1
    push_warning("Automatic sync conflict: "+key+"; use Sync now in the main menu to preserve both versions.")
   continue
  if ours==theirs:pass
  elif not theirs.is_empty() and (ours.is_empty() or ours==old or (ours!=old and theirs!=old)):
   var result=await _call(url+"/file/"+str(key).uri_encode())
   if result.is_empty() or result[0]!=HTTPRequest.RESULT_SUCCESS or result[1]!=200:_finish("Download failed: "+key);return
   var hash=HashingContext.new();hash.start(HashingContext.HASH_SHA256);hash.update(result[3])
   if hash.finish().hex_encode()!=theirs:_finish("Download checksum failed: "+key);return
   # Refuse overwriting a file changed while the request was in flight.
   if (FileAccess.get_sha256(target) if FileAccess.file_exists(target) else "")!=ours:_finish("File changed during sync; try again: "+key);return
   if not ours.is_empty() and ours!=old:
    var backup="user://sync-conflicts/"+str(Time.get_unix_time_from_system()).replace(".","-")+"/"+key
    DirAccess.make_dir_recursive_absolute(backup.get_base_dir())
    if DirAccess.copy_absolute(target,backup)!=OK:_finish("Cannot preserve conflict: "+key);return
    conflicts+=1
   DirAccess.make_dir_recursive_absolute(target.get_base_dir())
   var file=FileAccess.open(target+".sync-part",FileAccess.WRITE)
   if file==null:_finish("Cannot write: "+key);return
   file.store_buffer(result[3]);var write_error=file.get_error();file.close()
   if write_error!=OK or FileAccess.get_sha256(target+".sync-part")!=theirs:
    DirAccess.remove_absolute(target+".sync-part");_finish("Incomplete disk write: "+key);return
   if DirAccess.rename_absolute(target+".sync-part",target)!=OK:_finish("Cannot replace: "+key);return
  elif not ours.is_empty():
   var file=FileAccess.open(target,FileAccess.READ)
   if file==null or file.get_length()>128*1024*1024:_finish("Cannot upload file (limit 128 MiB): "+key);return
   var size=file.get_length();var data=file.get_buffer(size);var read_error=file.get_error();file.close()
   var upload_hash=HashingContext.new();upload_hash.start(HashingContext.HASH_SHA256);upload_hash.update(data)
   if read_error!=OK or data.size()!=size or upload_hash.finish().hex_encode()!=ours:
    _finish("File changed or could not be read completely: "+key);return
   var result=await _call(url+"/file/"+str(key).uri_encode(),HTTPClient.METHOD_PUT,data,theirs)
   if result.is_empty() or result[0]!=HTTPRequest.RESULT_SUCCESS or result[1]!=200:_finish("Upload failed or another device changed the file; sync again: "+key);return
   theirs=ours
  state.set_value("files",key,theirs)
  if state.save(state_path)!=OK:_finish("Cannot save sync state.");return
  count+=1
 succeeded=true
 _finish("Synced %d files. %d conflicts preserved in sync-conflicts. Restart to reload profiles and heroes."%[count,conflicts])

func _finish(message:String):
 status.text=message;busy=false;button.disabled=false;server.editable=true;username.editable=true
