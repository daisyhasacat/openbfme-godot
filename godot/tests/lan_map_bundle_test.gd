extends SceneTree
func _initialize():call_deferred('run')
func run():
 var url:=OS.get_environment('OPENBFME_MAP_BUNDLE_TEST_URL')
 if url.is_empty():print('MAP BUNDLE SKIP: synthetic server URL required');quit(77);return
 assert(ProjectSettings.get_setting('application/config/name')=='OpenBFMELanBulkTest')
 var client=load('res://scripts/lan_content.gd').new();root.add_child(client)
 client.address.text=url;await client._connect_server()
 assert(client.map_packs.size()==1,'One map-library package')
 var pack:Dictionary=client.map_packs[0]
 assert(pack.kind=='map_bundle' and int(pack.map_count)==2)
 var folder='user://map-library/bundles/'+str(pack.id)
 client._remove_tree(folder)
 var counts:Array=[]
 client.overall_progress.value_changed.connect(func(value):counts.append(int(value)))
 await client._download_batch(client.map_packs,'maps to the map library')
 assert(client.status.text.begins_with('Downloaded 1 '),client.status.text)
 assert(client.overall_progress.value==2 and client.overall_progress.max_value==2)
 assert(counts.has(1),'Progress advances while extracting maps')
 var fs=ClassDB.instantiate('RetailFileSystem')
 var browser=load('res://scripts/skirmish_map_browser.gd').new();browser.fs=fs;root.add_child(browser)
 browser.library_path='user://empty-map-bundle-library'
 browser._discover_library()
 var bundled:Array=browser.library_catalog.filter(func(map):return str(pack.id) in str(map.path))
 assert(bundled.size()==2,'Extracted maps discovered in browser library')
 assert(not DirAccess.dir_exists_absolute('user://installed-maps'))
 await client._download_batch(client.map_packs,'maps to the map library')
 assert('1 already present' in client.status.text)
 assert(client.overall_label.text=='Overall: 2 / 2 maps to the map library (2 already present)')
 browser.free();client.queue_free();await process_frame
 print('MAP BUNDLE PASS: one archive, streaming extraction progress, library discovery and resume')
 quit()
