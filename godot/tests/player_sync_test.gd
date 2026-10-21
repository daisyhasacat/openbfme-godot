extends SceneTree
var failed=false
func _initialize():call_deferred("run")
func check(ok:bool,message:String):
 if not ok:printerr(message);failed=true
func sha256(bytes:PackedByteArray)->String:
 var hashing=HashingContext.new();hashing.start(HashingContext.HASH_SHA256);hashing.update(bytes);return hashing.finish().hex_encode()
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
 DirAccess.remove_absolute("user://game_records.jsonl")
 DirAccess.remove_absolute("user://Skirmish.ini") # profiles are merged (never dropped): start each run without leftovers
 var path="user://map-favorites.cfg"
 var file=FileAccess.open(path,FileAccess.WRITE);file.store_string("original profile");file.close()
 await client._sync()
 check(client.status.text.begins_with("Synced"),client.status.text)
 var key=client.scope+"player/map-favorites.cfg" # the namespace (selected game) is known after the first sync
 DirAccess.remove_absolute(path)
 await client._sync()
 check(FileAccess.get_file_as_string(path)=="original profile","Missing file restored from server")
 # A second device changes the server after our last sync.
 var base=FileAccess.get_sha256(path)
 var response=await client._call(url+"/v1/"+client.username.text+"/file/"+key.uri_encode(),HTTPClient.METHOD_PUT,"other device".to_utf8_buffer(),base)
 check(response[1]==200,"Second device upload")
 file=FileAccess.open(path,FileAccess.WRITE);file.store_string("local competing change");file.close()
 await client._sync()
 check(FileAccess.get_file_as_string(path)=="other device","Remote conflict downloaded")
 check("1 conflicts preserved" in client.status.text,"Conflict reported")
 var preserved=false
 for folder in DirAccess.get_directories_at("user://sync-conflicts"):
  var backup="user://sync-conflicts/"+folder+"/"+key
  if FileAccess.file_exists(backup) and FileAccess.get_file_as_string(backup)=="local competing change":preserved=true
 check(preserved,"Competing local bytes preserved")
 check(client._target(client.scope+"player/../bad")=="","Traversal rejected")
 check(client._target(client.scope+"player/install-paths.cfg")=="","Device paths excluded")
 var game_scope=client.scope;client.scope="bfme1-"
 check("bfme1-Skirmish.ini" in client.player_files() and not "Skirmish.ini" in client.player_files(),"BFME1 syncs its own profile file")
 check(client._is_profiles("bfme1-player/bfme1-Skirmish.ini"),"BFME1 profiles are merged")
 client.scope=game_scope
 var watch=load("res://scripts/player_sync_watch.gd").new();root.add_child(watch)
 watch.set_process(false)
 file=FileAccess.open(path,FileAccess.WRITE);file.store_string("automatic profile change");file.close()
 await watch.poll()
 await watch.poll()
 var remote=await client._call(url+"/v1/"+client.username.text+"/file/"+key.uri_encode())
 check(remote[3].get_string_from_utf8()=="automatic profile change","Automatic watcher uploads a stable change")
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
   remote=await client._call(url+"/v1/"+client.username.text+"/file/"+key.uri_encode())
   if remote[3].get_string_from_utf8()=="timer driven change":break
 check(remote[3].get_string_from_utf8()=="timer driven change","Watcher timer uploads a change without a manual poll")
 # a same-size rewrite within the same second is still noticed (recent files are hashed)
 watch.set_process(false)
 file=FileAccess.open(path,FileAccess.WRITE);file.store_string("timer driven chang2");file.close()
 await watch.poll();await watch.poll()
 remote=await client._call(url+"/v1/"+client.username.text+"/file/"+key.uri_encode())
 check(remote[3].get_string_from_utf8()=="timer driven chang2","Same-size rewrite within one second uploads")
 # once the file is no longer recent its fingerprint is unchanged: no further sync
 var settled=watch.uploaded
 await create_timer(6.0).timeout
 await watch.poll()
 check(watch.observed==settled,"An unchanged file keeps its fingerprint after it stops being recent")

 # Profiles: Skirmish.ini holds every profile in one file and is merged, never replaced by a stale or empty list
 var hailey=":0:H_00a_00i_00l_00e_00y_00:Map = maps\\a.map\n"
 var gimli=":0:G_00i_00m_00l_00i_00:Map = maps\\b.map\n"
 var two=gimli+hailey+"CurrentUserName = H_00a_00i_00l_00e_00y_00\nUserNames = H_00a_00i_00l_00e_00y_00_2C_00G_00i_00m_00l_00i_00_2C_00\n"
 var merged=client.merge_profiles(two,"UserNames = \n")
 check(merged==two,"Merging an empty profile list keeps every profile: "+merged)
 merged=client.merge_profiles("CurrentUserName = B_00o_00b_00\nUserNames = B_00o_00b_00_2C_00\n",two)
 check("UserNames = B_00o_00b_00_2C_00H_00a_00i_00l_00e_00y_00_2C_00G_00i_00m_00l_00i_00_2C_00" in merged and "CurrentUserName = B_00o_00b_00" in merged and hailey in merged,"Merge unions profiles and keeps this device's current one: "+merged)
 check("Map = maps\\c.map" in client.merge_profiles(two,two.replace("a.map","c.map"),false),"Server entries win when only the server changed")
 var profiles_path="user://Skirmish.ini";var profiles_key=client.scope+"player/Skirmish.ini"
 file=FileAccess.open(profiles_path,FileAccess.WRITE);file.store_string(two);file.close()
 await client._sync()
 var server_profiles=await client._call(url+"/v1/"+client.username.text+"/file/"+profiles_key.uri_encode())
 check(server_profiles[3].get_string_from_utf8()==two,"Profiles uploaded: "+server_profiles[3].get_string_from_utf8())
 # another device (or a stale in-memory list) uploads an empty profile list
 response=await client._call(url+"/v1/"+client.username.text+"/file/"+profiles_key.uri_encode(),HTTPClient.METHOD_PUT,"UserNames = \n".to_utf8_buffer(),FileAccess.get_sha256(profiles_path))
 check(response[1]==200,"Empty profile list uploaded by another device")
 file=FileAccess.open(profiles_path,FileAccess.WRITE);file.store_string(two.replace("a.map","d.map"));file.close()
 watch.worker.server.text=url;watch.worker.username.text=client.username.text
 await watch.worker._sync()
 server_profiles=await client._call(url+"/v1/"+client.username.text+"/file/"+profiles_key.uri_encode())
 check(server_profiles[3].get_string_from_utf8()==two.replace("a.map","d.map"),"Automatic sync restores every profile and the local change to the server: "+server_profiles[3].get_string_from_utf8())
 check(FileAccess.get_file_as_string(profiles_path)==two.replace("a.map","d.map"),"Automatic sync leaves the in-memory profile file alone")
 # a profile added and a setting changed on another device arrive with Sync now
 var bob=two.replace("UserNames = H","UserNames = B_00o_00b_00_2C_00H").replace("a.map","e.map")
 response=await client._call(url+"/v1/"+client.username.text+"/file/"+profiles_key.uri_encode(),HTTPClient.METHOD_PUT,bob.to_utf8_buffer(),sha256(server_profiles[3]))
 check(response[1]==200,"Another device adds a profile")
 await client._sync()
 var local_profiles=FileAccess.get_file_as_string(profiles_path)
 check("B_00o_00b_00_2C_00" in local_profiles and "Map = maps\\e.map" in local_profiles and "G_00i_00m_00l_00i_00_2C_00" in local_profiles,"Sync now merges the other device's profile and setting locally: "+local_profiles)

 # Game records (the stats pages): every game of both devices, once, oldest first; automatic sync writes them locally too
 var game_a='{"v":1,"id":"sk-a","end":100}'
 var game_b='{"v":1,"id":"sk-b","end":200}'
 var game_c='{"v":1,"id":"sk-c","end":300}'
 check(client.merge_records(game_a+"\n"+game_c+"\n",game_b+"\n"+game_a+"\n")==game_a+"\n"+game_b+"\n"+game_c+"\n","Records merge by id in time order")
 var records_path="user://game_records.jsonl";var records_key=client.scope+"player/game_records.jsonl"
 file=FileAccess.open(records_path,FileAccess.WRITE);file.store_string(game_a+"\n");file.close()
 await client._sync()
 var server_records=await client._call(url+"/v1/"+client.username.text+"/file/"+records_key.uri_encode())
 response=await client._call(url+"/v1/"+client.username.text+"/file/"+records_key.uri_encode(),HTTPClient.METHOD_PUT,(game_a+"\n"+game_b+"\n").to_utf8_buffer(),sha256(server_records[3]))
 check(response[1]==200,"Another device uploads its games")
 file=FileAccess.open(records_path,FileAccess.READ_WRITE);file.seek_end();file.store_string(game_c+"\n");file.close()
 await watch.worker._sync()
 check(FileAccess.get_file_as_string(records_path)==game_a+"\n"+game_b+"\n"+game_c+"\n","Automatic sync merges the other device's games locally: "+FileAccess.get_file_as_string(records_path))
 server_records=await client._call(url+"/v1/"+client.username.text+"/file/"+records_key.uri_encode())
 check(server_records[3].get_string_from_utf8()==game_a+"\n"+game_b+"\n"+game_c+"\n","Automatic sync uploads every game")
 DirAccess.remove_absolute(records_path)
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
 if not failed:print("PLAYER SYNC PASS: upload, restore, conflict backup, path restrictions, profile and game record merge")
 quit(1 if failed else 0)
