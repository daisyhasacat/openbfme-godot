extends CanvasLayer
## OpenBFME LAN content exchange; downloads never change the active match.
signal installed
const DEFAULT_SERVER := "http://192.168.121.42:8765"
const SETTINGS := "user://lan-content.cfg"
const PROGRAMS := ["exe","dll","dylib","so","bat","cmd","sh","com","scr"]
var address: LineEdit
var status: Label
var progress: ProgressBar
var overall_progress: ProgressBar
var overall_label: Label
var rows: VBoxContainer
var request: HTTPRequest
var busy := false
var origin := ""
var staging_path := ""
var download_path := ""
var batch_running := false
var map_packs: Array = []
var builtin_packs: Array = []
var maps_button: Button
var builtin_button: Button

func _exit_tree()->void:
 if request!=null:request.cancel_request()
 if not staging_path.is_empty() and DirAccess.dir_exists_absolute(staging_path):_remove_tree(staging_path)
 if not download_path.is_empty():DirAccess.remove_absolute(download_path)

func _ready() -> void:
 layer = 130
 var backdrop := ColorRect.new()
 backdrop.color = Color(0,0,0,.9)
 backdrop.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
 add_child(backdrop)
 var panel := PanelContainer.new()
 panel.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
 panel.offset_left=32;panel.offset_top=32;panel.offset_right=-32;panel.offset_bottom=-32
 add_child(panel)
 var column := VBoxContainer.new();panel.add_child(column)
 var title := Label.new();title.text="LAN downloads";column.add_child(title)
 var connection := HBoxContainer.new();column.add_child(connection)
 address=LineEdit.new();address.size_flags_horizontal=Control.SIZE_EXPAND_FILL
 var config:=ConfigFile.new();config.load(SETTINGS)
 address.text=str(config.get_value("server","url",DEFAULT_SERVER));connection.add_child(address)
 var connect_button:=Button.new();connect_button.text="Connect";connection.add_child(connect_button)
 connect_button.pressed.connect(_connect_server)
 var bulk:=HFlowContainer.new();column.add_child(bulk)
 maps_button=Button.new();maps_button.text="Download all maps to library";maps_button.disabled=true;bulk.add_child(maps_button)
 maps_button.pressed.connect(func():_download_batch(map_packs,"maps to the map library"))
 builtin_button=Button.new();builtin_button.text="Download all 2.02 content / built-ins";builtin_button.disabled=true;bulk.add_child(builtin_button)
 builtin_button.pressed.connect(func():_download_batch(builtin_packs,"2.02 content / built-ins"))
 var search:=LineEdit.new();search.placeholder_text="Search mods and packs";column.add_child(search)
 search.text_changed.connect(func(text:String):
  for row in rows.get_children():row.visible=text.is_empty() or text.to_lower() in str(row.get_meta("search","")).to_lower())
 var scroll:=ScrollContainer.new();scroll.size_flags_vertical=Control.SIZE_EXPAND_FILL;column.add_child(scroll)
 rows=VBoxContainer.new();rows.size_flags_horizontal=Control.SIZE_EXPAND_FILL;scroll.add_child(rows)
 status=Label.new();status.autowrap_mode=TextServer.AUTOWRAP_WORD_SMART;status.text="Connect to see the packs shared by this computer.";column.add_child(status)
 overall_label=Label.new();overall_label.hide();column.add_child(overall_label)
 overall_progress=ProgressBar.new();overall_progress.name="OverallProgress";overall_progress.hide();column.add_child(overall_progress)
 progress=ProgressBar.new();progress.max_value=100;column.add_child(progress)
 var close:=Button.new();close.text="Close";close.pressed.connect(queue_free);column.add_child(close)
 request=HTTPRequest.new();request.use_threads=true;request.timeout=1800;add_child(request)
 preload("res://scripts/menu_accessibility.gd").prepare(self)
 connect_button.call_deferred("grab_focus")

func _unhandled_input(event:InputEvent)->void:
 if event.is_action_pressed("ui_cancel"):
  get_viewport().set_input_as_handled();queue_free()

func _process(_delta:float)->void:
 if busy and not request.download_file.is_empty():
  var total:=request.get_body_size()
  if total>0:progress.value=100.0*request.get_downloaded_bytes()/total

func _http_get(path:String, destination:String="")->Array:
 request.download_file=destination
 request.body_size_limit=8*1024*1024 if destination.is_empty() else -1
 var error:=request.request(origin+path)
 if error!=OK:return [error,0,[],PackedByteArray()]
 return await request.request_completed

func _json_reply(reply:Array)->Dictionary:
 if int(reply[0])!=HTTPRequest.RESULT_SUCCESS or int(reply[1])!=200:
  status.text="Server request failed (transport %s, HTTP %s)." % [reply[0],reply[1]]
  return {}
 var value=JSON.parse_string(reply[3].get_string_from_utf8())
 if not value is Dictionary:status.text="Invalid server response.";return {}
 return value

func _connect_server()->void:
 if busy or batch_running:return
 origin=address.text.strip_edges().trim_suffix("/")
 if not (origin.begins_with("http://") or origin.begins_with("https://")):
  origin="http://"+origin
 busy=true;status.text="Connecting…"
 var catalog:=_json_reply(await _http_get("/catalog.json"))
 busy=false
 if int(catalog.get("version",0))!=1:
  if not catalog.is_empty():status.text="Unsupported catalogue version."
  return
 var config:=ConfigFile.new();config.set_value("server","url",origin);config.save(SETTINGS)
 for child in rows.get_children():rows.remove_child(child);child.queue_free()
 map_packs=[];builtin_packs=[]
 for value in catalog.get("packs",[]):
  if not value is Dictionary:continue
  var pack:Dictionary=value
  if not valid_id(str(pack.get("id",""))):continue
  if str(pack.get("kind","")) in ["map","map_bundle"]:
   map_packs.append(pack)
   continue
  if str(pack.get("kind",""))=="builtin" or "2.02" in str(pack.get("name","")):
   builtin_packs.append(pack)
  var row:=HBoxContainer.new();rows.add_child(row)
  var label:=Label.new();label.size_flags_horizontal=Control.SIZE_EXPAND_FILL
  label.text="%s · %s · %s" % [pack.get("name","Pack"),pack.get("kind",""),String.humanize_size(int(pack.get("bytes",0)))]
  if bool(pack.get("textures_only",false)):label.text+=" · textures only"
  row.set_meta("search",label.text)
  row.add_child(label)
  var button:=Button.new();button.text="Download";row.add_child(button)
  button.pressed.connect(func():_download(pack))
 maps_button.disabled=map_packs.is_empty();builtin_button.disabled=builtin_packs.is_empty()
 var map_count:=0
 for pack in map_packs:map_count+=int(pack.get("map_count",1))
 maps_button.text="Download all maps to library (%d)"%map_count
 preload("res://scripts/menu_accessibility.gd").prepare(self)
 if not maps_button.disabled:maps_button.grab_focus()
 status.text="%d packs available. Mods take effect after enabling them and restarting; maps appear after reopening the map browser." % catalog.get("packs",[]).size()

func _download_batch(packs:Array, description:String)->void:
 if busy or batch_running:return
 batch_running=true
 var completed:=0
 var skipped:=0
 var total:=0
 var done:=0
 var skipped_items:=0
 for pack in packs:total+=int(pack.get("map_count",1))
 _update_overall(0,total,description,0)
 for pack in packs:
  var present:=false
  if str(pack.kind)=="map_bundle":
   present=DirAccess.dir_exists_absolute("user://map-library/bundles/"+str(pack.id))
  elif str(pack.kind)=="map":
   present=DirAccess.dir_exists_absolute("user://map-library/%s/lan-%s"%["bfme1" if str(pack.get("game",""))=="bfme1" else "rotwk",pack.id])
  elif str(pack.kind)=="builtin":
   present=FileAccess.file_exists("user://mods/"+str(pack.name)+".big")
  else:present=DirAccess.dir_exists_absolute("user://mods/lan-%s"%pack.id)
  if present:
   skipped+=1
   done+=int(pack.get("map_count",1));skipped_items+=int(pack.get("map_count",1))
   _update_overall(done,total,description,skipped_items)
   if skipped%32==0:await get_tree().process_frame
   continue
  if not await _download(pack,true):
   status.text="Stopped after %d of %d: %s"%[completed,packs.size(),status.text]
   batch_running=false;return
  completed+=1
  done+=int(pack.get("map_count",1))
  _update_overall(done,total,description,skipped_items)
 batch_running=false
 status.text="Downloaded %d %s; %d already present. Maps remain in Map Library until you install them in the browser."%[completed,description,skipped]

func _update_overall(done:int,total:int,description:String,skipped:int)->void:
 overall_progress.show();overall_label.show()
 overall_progress.max_value=maxi(total,1)
 overall_progress.value=done
 overall_label.text="Overall: %d / %d %s"%[done,total,description]
 if skipped>0:overall_label.text+=" (%d already present)"%skipped

static func valid_id(id:String)->bool:
 if id.length()!=64:return false
 for c in id:
  if not c in "0123456789abcdef":return false
 return true

static func safe_path(path:String)->bool:
 if path.is_empty() or path.begins_with("/") or "\\" in path or ":" in path:return false
 for part in path.split("/"):
  if part in [".","..",""]:return false
 return not path.get_extension().to_lower() in PROGRAMS

func _download(pack:Dictionary, from_batch:bool=false)->bool:
 if busy or (batch_running and not from_batch):return false
 busy=true;progress.value=0
 status.text="Preparing %s on the host…" % pack.name
 var metadata:=_json_reply(await _http_get("/packs/"+str(pack.id)))
 if metadata.is_empty():busy=false;return false
 if str(metadata.get("id",""))!=str(pack.id) or not valid_id(str(metadata.get("sha256",""))) or str(metadata.get("download",""))!="/files/%s.zip"%pack.id:
  status.text="Invalid pack metadata.";busy=false;return false
 DirAccess.make_dir_recursive_absolute("user://lan-downloads")
 var archive:="user://lan-downloads/%s.zip.part"%pack.id
 download_path=archive
 status.text="Downloading %s…"%pack.name
 var reply:=await _http_get(str(metadata.download),archive)
 request.download_file=""
 if int(reply[0])!=HTTPRequest.RESULT_SUCCESS or int(reply[1])!=200:
  status.text="Download failed (transport %s, HTTP %s)."%[reply[0],reply[1]]
  DirAccess.remove_absolute(archive);busy=false;return false
 status.text="Verifying %s…"%pack.name
 var file:=FileAccess.open(archive,FileAccess.READ)
 var hash:=HashingContext.new();hash.start(HashingContext.HASH_SHA256)
 if file==null:status.text="Cannot read downloaded pack.";busy=false;return false
 while file.get_position()<file.get_length():
  hash.update(file.get_buffer(4*1024*1024))
  await get_tree().process_frame
 var size:=file.get_length();file.close()
 if size!=int(metadata.get("bytes",-1)) or hash.finish().hex_encode()!=str(metadata.sha256):
  status.text="Download checksum or size mismatch. Retry the download."
  DirAccess.remove_absolute(archive);busy=false;return false
 status.text="Installing %s…"%pack.name
 var problem:String=await _install(pack,archive)
 if not staging_path.is_empty() and DirAccess.dir_exists_absolute(staging_path):_remove_tree(staging_path)
 staging_path=""
 DirAccess.remove_absolute(archive)
 download_path=""
 busy=false
 if problem.is_empty():
  progress.value=100;status.text="Installed %s. Enable mods in Mods, or reopen the map browser’s Map Library for maps (install there when ready). Built-in packs use their existing Options switches."%pack.name
  if not str(pack.get("kind","")) in ["map","map_bundle"]:installed.emit()
 else:status.text=problem
 return problem.is_empty()

func _install(pack:Dictionary, archive:String)->String:
 var kind:=str(pack.get("kind",""))
 if not kind in ["mod","builtin","map","map_bundle","aotr"]:return "Unsupported pack type."
 if kind=="map" and not str(pack.get("game","")) in ["bfme1","bfme2"]:return "Unsupported map game."
 var root_path:="user://mods/lan-%s"%pack.id
 if kind=="map_bundle":root_path="user://map-library/bundles/"+str(pack.id)
 if kind=="aotr":root_path="user://games/aotr-lan-%s"%pack.id
 if kind=="map":root_path="user://map-library/%s/lan-%s"%["bfme1" if str(pack.get("game",""))=="bfme1" else "rotwk",pack.id]
 if kind=="builtin":root_path="user://lan-downloads/builtin-%s"%pack.id
 if DirAccess.dir_exists_absolute(root_path):return "This pack is already installed. Remove its folder before replacing it."
 var staging:=root_path+".installing-"+str(Time.get_ticks_usec())
 staging_path=staging
 var zip:=ZIPReader.new()
 if zip.open(archive)!=OK:return "Cannot open downloaded ZIP."
 var seen:={}
 for entry in zip.get_files():
  if entry.ends_with("/"):continue
  if not safe_path(entry) or seen.has(entry.to_lower()):zip.close();return "Unsafe or duplicate path in pack: "+entry
  if kind=="map_bundle" and not (entry.begins_with("rotwk/") or entry.begins_with("bfme1/")):zip.close();return "Invalid map-library game folder."
  seen[entry.to_lower()]=true
 if seen.is_empty():zip.close();return "Pack contains no files."
 zip.close()
 # The LAN protocol uses stored ZIP entries so large BIG files never need a
 # whole-file allocation. The archive SHA-256 was verified before this pass.
 var input:=FileAccess.open(archive,FileAccess.READ)
 if input==null:return "Cannot read installation archive."
 var extracted:={}
 var extracted_maps:=0
 var expanded:int=0
 while input.get_position()+4<=input.get_length():
  var signature:=input.get_32()
  if signature in [0x02014b50,0x06054b50,0x06064b50]:break
  if signature!=0x04034b50:return "Invalid ZIP entry header."
  input.get_16()
  var flags:=input.get_16()
  var method:=input.get_16()
  input.get_16();input.get_16();input.get_32()
  var compressed:int=input.get_32()
  var length:int=input.get_32()
  var name_length:=input.get_16()
  var extra_length:=input.get_16()
  var entry:=input.get_buffer(name_length).get_string_from_utf8()
  var extra:=input.get_buffer(extra_length)
  var at:=0
  while at+4<=extra.size():
   var tag:=extra.decode_u16(at);var count:=extra.decode_u16(at+2);at+=4
   if at+count>extra.size():return "Invalid ZIP extra data."
   if tag==1:
    var cursor:=at
    if length==0xffffffff:
     if cursor+8>at+count:return "Missing ZIP64 size."
     length=extra.decode_u64(cursor);cursor+=8
    if compressed==0xffffffff:
     if cursor+8>at+count:return "Missing ZIP64 compressed size."
     compressed=extra.decode_u64(cursor)
   at+=count
  if method!=0 or (flags & 9)!=0:return "This server must provide unencrypted stored ZIP entries."
  if length<0 or length!=compressed or length>input.get_length()-input.get_position():return "Invalid ZIP entry size."
  if entry.ends_with("/"):
   if length!=0:return "Invalid ZIP directory."
   continue
  if not safe_path(entry) or not seen.has(entry.to_lower()) or extracted.has(entry.to_lower()):return "ZIP directory/header mismatch."
  expanded+=length
  if expanded>128*1024*1024*1024 or extracted.size()>=200000:return "Pack exceeds installation limits."
  extracted[entry.to_lower()]=true
  var target:=staging.path_join(entry)
  if DirAccess.make_dir_recursive_absolute(target.get_base_dir())!=OK:return "Cannot create installation folder."
  var output:=FileAccess.open(target,FileAccess.WRITE)
  if output==null:return "Cannot write installation file."
  var remaining:=length
  while remaining>0:
   var count:=mini(remaining,1024*1024)
   var bytes:=input.get_buffer(count)
   if bytes.size()!=count:return "Truncated ZIP entry."
   output.store_buffer(bytes);remaining-=count
   if output.get_error()!=OK:return "Failed to write installation file."
   await get_tree().process_frame
  var error:=output.get_error();output.close()
  if error!=OK:return "Failed to write installation file."
  if kind=="map_bundle" and entry.get_extension().to_lower()=="map":
   extracted_maps+=1
   _update_overall(extracted_maps,int(pack.get("map_count",0)),"maps extracted",0)
  await get_tree().process_frame
 input.close()
 if extracted.size()!=seen.size():return "Incomplete installation archive."
 if kind=="map_bundle":
  if extracted_maps!=int(pack.get("map_count",0)) or extracted_maps==0:return "Map-library count mismatch."
  DirAccess.make_dir_recursive_absolute(root_path.get_base_dir())
  if DirAccess.rename_absolute(staging,root_path)!=OK:return "Cannot finish map-library extraction."
  return ""
 if kind=="aotr":
  if not FileAccess.file_exists(staging.path_join("aotr/AOTRSkirmish.big")) or not FileAccess.file_exists(staging.path_join("rotwk/#aotr_patch202.big")):return "AOTR pack is missing its game files or runtime dependency."
  DirAccess.make_dir_recursive_absolute(root_path.get_base_dir())
  if DirAccess.rename_absolute(staging,root_path)!=OK:return "Cannot finish AOTR installation."
  var selection:=ConfigFile.new();selection.load("user://game-selection.cfg")
  selection.set_value("aotr","install",ProjectSettings.globalize_path(root_path.path_join("aotr")))
  if selection.save("user://game-selection.cfg")!=OK:return "AOTR files installed, but saving its game path failed."
  return ""
 if kind=="map":
  var entry:=str(pack.get("entry",""))
  if not safe_path(entry) or not entry.get_extension().to_lower()=="map":return "Invalid map entry."
  if not FileAccess.file_exists(staging.path_join(entry)):return "Map file is missing from the pack."
  DirAccess.make_dir_recursive_absolute(root_path.get_base_dir())
  if DirAccess.rename_absolute(staging,root_path)!=OK:return "Cannot finish map-library download."
  return ""
 if kind=="builtin":
  if seen.size()!=1:return "Built-in pack must contain one BIG file."
  var name:String=seen.keys()[0]
  if not name in ["wall_towers.big","rotwk_202_maps.big"]:return "Unknown built-in pack."
  DirAccess.make_dir_recursive_absolute("user://mods")
  var target:="user://mods/"+name
  var source:String=staging.path_join(zip_entry_case(staging,name))
  if FileAccess.file_exists(target):
   if FileAccess.get_sha256(target)==FileAccess.get_sha256(source):_remove_tree(staging);return ""
   return "A different version of this built-in pack already exists. Remove it before replacing it."
  if DirAccess.rename_absolute(source,target)!=OK:return "Cannot install built-in pack."
  _remove_tree(staging)
  return ""
 var game_root:String=load("res://scripts/mod_manager.gd").game_root(staging)
 if game_root.is_empty():return "Downloaded mod contains no game files."
 var relative_root:=game_root.trim_prefix(staging).trim_prefix("/")
 DirAccess.make_dir_recursive_absolute(root_path.get_base_dir())
 if DirAccess.rename_absolute(staging,root_path)!=OK:return "Cannot finish mod installation."
 var config:=ConfigFile.new();config.load("user://mods.cfg")
 var order:Array=Array(config.get_value("mods","order",[]))
 var absolute:=ProjectSettings.globalize_path(root_path if relative_root.is_empty() else root_path.path_join(relative_root))
 if not order.has(absolute):order.append(absolute)
 config.set_value("mods","order",order)
 if bool(pack.get("textures_only",false)):
  var textures:Array=Array(config.get_value("mods","textures_only",[]))
  if not textures.has(absolute):textures.append(absolute)
  config.set_value("mods","textures_only",textures)
 if config.save("user://mods.cfg")!=OK:return "Mod files installed, but saving Mods settings failed."
 return ""

static func zip_entry_case(folder:String, lower:String)->String:
 for name in DirAccess.get_files_at(folder):
  if name.to_lower()==lower:return name
 return lower

static func _remove_tree(path:String)->void:
 if not DirAccess.dir_exists_absolute(path):return
 for folder in DirAccess.get_directories_at(path):_remove_tree(path.path_join(folder))
 for file in DirAccess.get_files_at(path):DirAccess.remove_absolute(path.path_join(file))
 DirAccess.remove_absolute(path)
