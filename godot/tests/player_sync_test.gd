extends SceneTree
var failed=false
func _initialize():call_deferred("run")
func check(ok:bool,message:String):
 if not ok:printerr(message);failed=true
func run():
 if str(ProjectSettings.get_setting("application/config/name"))!="OpenBFMELanBulkTest":quit(77);return
 var url=OS.get_environment("OPENBFME_SYNC_TEST_URL")
 if url.is_empty():quit(77);return
 check(load("res://scripts/openbfme_options_screen.gd")!=null,"Options screen compiles")
 var client=load("res://scripts/player_sync.gd").new();root.add_child(client)
 client.server.text=url;client.username.text="fixture_"+str(Time.get_ticks_usec())
 var path="user://Skirmish.ini"
 var file=FileAccess.open(path,FileAccess.WRITE);file.store_string("original profile");file.close()
 await client._sync()
 check(client.status.text.begins_with("Synced"),client.status.text)
 DirAccess.remove_absolute(path)
 await client._sync()
 check(FileAccess.get_file_as_string(path)=="original profile","Missing file restored from server")
 # A second device changes the server after our last sync.
 var base=FileAccess.get_sha256(path)
 var response=await client._call(url+"/v1/"+client.username.text+"/file/"+(client.scope+"player/Skirmish.ini").uri_encode(),HTTPClient.METHOD_PUT,"other device".to_utf8_buffer(),base)
 check(response[1]==200,"Second device upload")
 file=FileAccess.open(path,FileAccess.WRITE);file.store_string("local competing change");file.close()
 await client._sync()
 check(FileAccess.get_file_as_string(path)=="other device","Remote conflict downloaded")
 check("1 conflicts preserved" in client.status.text,"Conflict reported")
 var preserved=false
 for folder in DirAccess.get_directories_at("user://sync-conflicts"):
  var backup="user://sync-conflicts/"+folder+"/"+client.scope+"player/Skirmish.ini"
  if FileAccess.file_exists(backup) and FileAccess.get_file_as_string(backup)=="local competing change":preserved=true
 check(preserved,"Competing local bytes preserved")
 check(client._target(client.scope+"player/../bad")=="","Traversal rejected")
 check(client._target(client.scope+"player/install-paths.cfg")=="","Device paths excluded")
 client.queue_free()
 await process_frame
 if not failed:print("PLAYER SYNC PASS: upload, restore, conflict backup, path restrictions")
 quit(1 if failed else 0)
