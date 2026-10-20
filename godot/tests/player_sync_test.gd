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
 var autowatch=root.get_node_or_null("Release/PlayerSyncWatch")
 if autowatch!=null:autowatch.queue_free()
 await process_frame
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
 var watch=load("res://scripts/player_sync_watch.gd").new();root.add_child(watch)
 watch.set_process(false)
 file=FileAccess.open(path,FileAccess.WRITE);file.store_string("automatic profile change");file.close()
 await watch.poll()
 await watch.poll()
 var remote=await client._call(url+"/v1/"+client.username.text+"/file/"+(client.scope+"player/Skirmish.ini").uri_encode())
 check(remote[3].get_string_from_utf8()=="automatic profile change","Automatic watcher uploads stable profile change")
 file=FileAccess.open("user://Save/MyHero_fixture.cah",FileAccess.WRITE)
 if file==null:
  DirAccess.make_dir_recursive_absolute("user://Save")
  file=FileAccess.open("user://Save/MyHero_fixture.cah",FileAccess.WRITE)
 file.store_string("new hero");file.close()
 await watch.poll();await watch.poll()
 remote=await client._call(url+"/v1/"+client.username.text+"/file/"+(client.scope+"player/Save/MyHero_fixture.cah").uri_encode())
 check(remote[3].get_string_from_utf8()=="new hero","Automatic watcher uploads new hero")
 # the real timer path: processing alone notices a change and uploads it once it is stable
 watch.set_process(true)
 file=FileAccess.open(path,FileAccess.WRITE);file.store_string("timer driven change");file.close()
 var deadline=Time.get_ticks_msec()+15000
 while Time.get_ticks_msec()<deadline:
  await create_timer(0.5).timeout
  if not watch.worker.busy and watch.uploaded==watch.observed and not watch.uploaded.is_empty():
   remote=await client._call(url+"/v1/"+client.username.text+"/file/"+(client.scope+"player/Skirmish.ini").uri_encode())
   if remote[3].get_string_from_utf8()=="timer driven change":break
 check(remote[3].get_string_from_utf8()=="timer driven change","Watcher timer uploads a change without a manual poll")
 # a same-size rewrite within the same second is still noticed (recent files are hashed)
 watch.set_process(false)
 file=FileAccess.open(path,FileAccess.WRITE);file.store_string("timer driven chang2");file.close()
 await watch.poll();await watch.poll()
 remote=await client._call(url+"/v1/"+client.username.text+"/file/"+(client.scope+"player/Skirmish.ini").uri_encode())
 check(remote[3].get_string_from_utf8()=="timer driven chang2","Same-size rewrite within one second uploads")
 # once the file is no longer recent its fingerprint is unchanged: no further sync
 var settled=watch.uploaded
 await create_timer(6.0).timeout
 await watch.poll()
 check(watch.observed==settled,"An unchanged file keeps its fingerprint after it stops being recent")
 watch.worker.succeeded=true
 watch.worker.busy=true
 await client._sync()
 check(client.status.text.begins_with("Automatic sync is uploading"),"Manual sync explains a running automatic sync: "+client.status.text)
 watch.worker.busy=false
 client.busy=true
 await watch.worker._sync()
 check(not watch.worker.succeeded,"Skipped overlapping sync is not successful")
 client.busy=false
 watch.queue_free()
 client.queue_free()
 await process_frame
 if not failed:print("PLAYER SYNC PASS: upload, restore, conflict backup, path restrictions")
 quit(1 if failed else 0)
