extends SceneTree
func _initialize():call_deferred('run')
func run():
 var url:=OS.get_environment('OPENBFME_LAN_BULK_TEST_URL')
 if url.is_empty():print('LAN BULK SKIP: synthetic server URL required');quit(77);return
 assert(ProjectSettings.get_setting('application/config/name')=='OpenBFMELanBulkTest','Use an isolated bulk-test project')
 var client=load('res://scripts/lan_content.gd').new();root.add_child(client)
 client.address.text=url;await client._connect_server()
 assert(client.map_packs.size()==2)
 assert(client.rows.get_child_count()==2,'Only texture and built-in rows; maps grouped')
 assert(client.builtin_packs.size()==2)
 assert(not client.maps_button.disabled and not client.builtin_button.disabled)
 for pack in client.map_packs:client._remove_tree('user://map-library/rotwk/lan-%s'%pack.id)
 for pack in client.builtin_packs:
  if pack.kind=='builtin':DirAccess.remove_absolute('user://mods/'+str(pack.name)+'.big')
  else:client._remove_tree('user://mods/lan-%s'%pack.id)
 # This test must run in an isolated userdata profile: packs contain synthetic bytes.
 var counts:Array=[]
 client.overall_progress.value_changed.connect(func(value):counts.append(int(value)))
 await client._download_batch(client.map_packs,'maps')
 assert(counts==[1,2],'Overall bar advances once for each completed map')
 assert(client.overall_progress.visible and client.overall_progress.max_value==2)
 assert(client.overall_label.text=='Overall: 2 / 2 maps')
 assert(client.status.text.begins_with('Downloaded 2 '),client.status.text)
 assert(not DirAccess.dir_exists_absolute('user://installed-maps'))
 for pack in client.map_packs:
  var path='user://map-library/rotwk/lan-%s/%s'%[pack.id,pack.entry]
  assert(FileAccess.file_exists(path))
 await client._download_batch(client.map_packs,'maps')
 assert('2 already present' in client.status.text,client.status.text)
 assert(client.overall_progress.value==2)
 assert(client.overall_label.text=='Overall: 2 / 2 maps (2 already present)')
 await client._download_batch(client.builtin_packs,'built-ins')
 assert(client.status.text.begins_with('Downloaded 2 '),client.status.text)
 assert(FileAccess.get_file_as_string('user://mods/wall_towers.big')=='synthetic builtin')
 await client._download_batch([client.map_packs[0],{'id':'d'.repeat(64),'name':'Missing fixture','kind':'map','game':'bfme2'}],'maps')
 assert(client.status.text.begins_with('Stopped '))
 assert(client.overall_progress.value==1,'Failed download does not count as completed')
 assert(client.overall_progress.max_value==2)
 assert(client.overall_label.text=='Overall: 1 / 2 maps (1 already present)')
 var fs=ClassDB.instantiate('RetailFileSystem')
 var browser=load('res://scripts/skirmish_map_browser.gd').new();browser.fs=fs;root.add_child(browser)
 browser.library_path='user://map-library/rotwk'
 browser._discover_library()
 assert(browser.library_catalog.size()==2,'Downloaded maps visible in browser library')
 browser.free();client.queue_free()
 await process_frame
 print('LAN BULK PASS: grouped catalogue, queues, resume, library discovery, built-ins')
 quit()
