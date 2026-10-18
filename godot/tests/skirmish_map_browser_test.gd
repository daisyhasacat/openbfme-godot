extends SceneTree
func _initialize():call_deferred('_run')
func _run():
 root.size=Vector2i(1440,1000)
 var fs=ClassDB.instantiate('RetailFileSystem');assert(fs.mount_retail().ok)
 var world=ClassDB.instantiate('GameWorld');root.add_child(world);assert(world.setup(fs).ok)
 var shell=ClassDB.instantiate('AptMenuPlayer');root.add_child(shell)
 assert(shell.boot_shell(fs,world,{'skirmish_ini':'/tmp/openbfme-map-browser-test.ini'}).ok)
 assert(shell.shell_push('Skirmish.apt'))
 for i in 80:shell.tick(.03)
 assert(shell.lobby_apply({'profile':'MapBrowserTest'}).ok)
 for i in 30:shell.tick(.03)
 var catalog=shell.lobby_map_catalog();assert(catalog.size()>1)
 var original=shell.lobby_state().map
 var browser=load('res://scripts/skirmish_map_browser.gd').new();browser.shell=shell;browser.fs=fs;browser.resolver=world;root.add_child(browser)
 browser.definitions=load('res://scripts/skirmish_map_definitions.gd').new();browser.definitions.setup(fs,'/tmp/openbfme-browser-definitions-'+str(Time.get_ticks_usec()))
 var library_dir='/tmp/openbfme-map-library-'+str(Time.get_ticks_usec())
 assert(DirAccess.make_dir_recursive_absolute(library_dir)==OK)
 var fixture=catalog[0]
 for entry in catalog:
  if 'buckland' in str(entry.key).to_lower():fixture=entry;break
 var library_file=FileAccess.open(library_dir.path_join('Library fixture.map'),FileAccess.WRITE)
 library_file.store_buffer(fs.read_file(fixture.key));library_file.close()
 var sidecar=FileAccess.open(library_dir.path_join('map.ini'),FileAccess.WRITE)
 sidecar.store_string('; library preview sidecar');sidecar.close()
 browser.metadata_cache_root=library_dir+'-metadata-cache'
 browser.favorites_path=library_dir+'-favorites.cfg'
 browser.install_root=library_dir.path_join('installed')
 browser.library_path=library_dir
 browser.show_browser();assert(browser.open and not shell.is_processing_input())
 assert(not shell.visible and not shell.is_processing(),'Covered lobby is suspended')
 # Choose a two-player Buckland fixture, retaining the lobby's mounted map set.
 var pick=0
 for i in browser.filtered.size():
  if 'buckland' in str(browser.filtered[i].key).to_lower():pick=i;break
 browser.maps.select(pick);browser._select(pick)
 for i in 400:
  await process_frame
  if browser.objects!=null:break
 assert(browser.terrain!=null,'3D terrain preview loaded')
 for frame in 4:await process_frame
 # UPDATE_ONCE draws one frame; the node's property keeps reading ONCE afterwards, so the check is that nothing asks for every frame.
 assert(browser.viewport.render_target_update_mode!=SubViewport.UPDATE_ALWAYS,'Idle preview does not render continuously')
 assert(browser.objects_builder.get_stats().animated_drawables==0,'Preview objects are static')
 browser._camera_pose()
 assert(browser.viewport.render_target_update_mode==SubViewport.UPDATE_ONCE,'Camera movement redraws preview')
 assert(browser.objects!=null and browser.objects.get_parent()==browser.terrain,'Map objects rendered')
 # Filter and sort the list without losing the selection.
 var before=browser.filtered.size()
 browser.type_filter.select(1);browser._filter()
 assert(browser.filtered.size()>0 and browser.filtered.size()<=before and browser.filtered.all(func(e):return e.official),'Official filter')
 browser.type_filter.select(0)
 for i in browser.players_filter.item_count:
  if browser.players_filter.get_item_id(i)==2:browser.players_filter.select(i)
 browser._filter();assert(browser.filtered.size()>0 and browser.filtered.all(func(e):return e.players==2),'Player-count filter')
 browser.players_filter.select(0);browser.sort.select(3);browser._filter()
 for i in browser.filtered.size()-1:assert(browser.filtered[i].players>=browser.filtered[i+1].players,'Most players first')
 browser.sort.select(0);browser._filter();assert(browser.filtered.size()==before)
 assert(browser.start_buttons.size()>0,'Fortress positions are selectable')
 assert(browser.tabs.get_tab_count()==3,'Three setup tabs')
 assert(browser.draft.rule_definitions.size()==4,'Vanilla skirmish rules')
 # OpenBFME's Infinite starting resources: the last InitialResources choice, 10,000,000, accepted by the lobby
 var resources=browser.draft.rule_definitions.filter(func(r):return int(r.id)==4)[0]
 assert(resources.choices[-1].label=='Infinite' and int(resources.choices[-1].value)==10000000,'Infinite starting resources offered')
 assert(shell.lobby_apply({'browser_rules':[{'id':4,'value':10000000}]}).ok,'Infinite starting resources accepted')
 assert(not shell.lobby_apply({'browser_rules':[{'id':4,'value':9999999}]}).ok,'Other off-table amounts still rejected')
 assert(shell.lobby_apply({'browser_rules':[{'id':4,'value':int(resources.default)}]}).ok)
 browser._assign(0,int(browser.selected.starts[0].index))
 var factions=browser.draft.factions.filter(func(f):return not str(f.building).is_empty())
 assert(factions.size()>1)
 browser.draft.slots[0].template=factions[0].value;browser._refresh_cards()
 var flag:Dictionary=world.get_placement_ghost('CastleFlag')
 assert(not str(flag.model).is_empty(),'CastleFlag preview resolves from retail data')
 assert(browser.fortress_model.contains(str(flag.model)+';'),'Unassigned start uses CastleFlag')
 var unused_index=int(browser.selected.starts[1].index)
 browser._assign(1,unused_index)
 browser.draft.slots[1].template=factions[0].value
 browser.draft.slots[1].state=1;browser._refresh_cards()
 assert(browser.fortress_model.contains(str(flag.model)+';'),'Closed assigned slot uses CastleFlag')
 browser._assign(1,-1)
 var first_model=browser.fortress_model
 assert(not first_model.is_empty(),'Faction fortress resolved')
 browser.draft.slots[0].template=factions[1].value;browser._refresh_cards()
 assert(browser.fortress_model!=first_model,'Faction changes preview fortress')
 browser.tabs.current_tab=1
 assert(browser.start_buttons[0].card.get_child_count()==4,'Owner, AI, faction and team controls')
 browser.draft.slots[1].state=3;browser.draft.slots[1].team=0
 browser.draft.slots[0].team=0
 browser._assign(1,int(browser.selected.starts[1].index))
 # Controls must preserve negative values (random faction / no team) through their metadata.
 var team_control=browser.start_buttons[0].card.get_child(3)
 team_control.item_selected.emit(0)
 assert(browser.draft.slots[0].team==-1,'No-team choice keeps its negative value')
 browser.draft.slots[0].team=0
 browser._assign(0,int(browser.selected.starts[1].index))
 assert(browser.assignments[1]==int(browser.selected.starts[0].index),'Moving into occupied start swaps players')
 browser._assign(0,int(browser.selected.starts[0].index))
 browser.rule_edits[4]=4000
 browser.draft.legendary_heroes=2
 browser.draft.fortress_construction=true
 assert(browser.start_buttons[0].point.y>1,'Fortress sits on the terrain')
 assert('height samples' in browser.stats.text,'Stats derived from terrain data')
 browser.start_buttons[0].button.pressed.emit()
 assert(browser.assignments[0]==int(browser.selected.starts[0].index))
 assert(shell.lobby_state().map==original,'Preview selection does not alter lobby')
 if '--visual' in OS.get_cmdline_user_args():
  for tab in 3:
   browser.tabs.current_tab=tab
   await process_frame
   await RenderingServer.frame_post_draw
   root.get_texture().get_image().save_png('/tmp/skirmish-map-browser-tab-%d.png'%tab)
  browser.tabs.current_tab=1
  await process_frame
  await RenderingServer.frame_post_draw
  root.get_texture().get_image().save_png('/tmp/skirmish-map-browser.png')
 var chosen=browser.selected.key
 browser._apply();assert(not browser.open and shell.lobby_state().map==chosen,'Commit selected map: '+browser.message.text)
 assert(shell.lobby_state().slots[0].start==0,'Selected fortress position reaches lobby')
 assert(shell.visible,'Lobby visibility restored')
 assert(shell.lobby_state().slots[1].state==3,'AI difficulty committed')
 assert(shell.lobby_state().slots[0].team==0,'Team committed')
 assert(shell.lobby_state().rules[4]==4000,'Vanilla rules committed')
 assert(shell.lobby_state().legendary_heroes==2,'Extension committed')
 assert(shell.lobby_state().fortress_construction,'Fortress construction rule committed')
 browser.show_browser()
 browser.draft.slots[1].state=5;browser.rule_edits[4]=1000
 var original_catalog=browser.catalog
 browser.catalog=[{'key':'standard','name':'Standard','players':2,'official':true,'definition':{'ok':true,'is_fortress':false,'counts':{'outpost':1,'inn':0,'signal_fire':0}}},{'key':'fortress','name':'Fortress','players':4,'official':true,'definition':{'ok':true,'is_fortress':true,'counts':{'outpost':0,'inn':1,'signal_fire':1}}},{'key':'pending','name':'Pending','players':2,'official':true}]
 browser.feature_filter.select(1);browser._filter();assert(browser.filtered.size()==1 and browser.filtered[0].key=='fortress','Fortress filter excludes unscanned maps')
 browser.feature_filter.select(2);browser._filter();assert(browser.filtered.size()==1 and browser.filtered[0].key=='standard','Standard filter')
 browser.feature_filter.select(3);browser._filter();assert(browser.filtered.size()==1 and browser.filtered[0].key=='standard','Outpost filter')
 browser.feature_filter.select(4);browser._filter();assert(browser.filtered.size()==1 and browser.filtered[0].key=='fortress','Inn filter')
 browser.feature_filter.select(5);browser._filter();assert(browser.filtered.size()==1 and browser.filtered[0].key=='fortress','Signal fire filter')
 browser.feature_filter.select(0)
 browser.catalog[0].definition.snowy=true
 browser.catalog[1].definition.snowy=false
 browser.snow_filter.button_pressed=true;browser._filter()
 assert(browser.filtered.size()==1 and browser.filtered[0].key=='standard','Snow filter uses scanned map weather and excludes pending maps')
 browser.catalog[1].snowy=true;browser._filter()
 assert(browser.filtered.size()==2,'Library metadata also supports snow filtering')
 browser.snow_filter.button_pressed=false
 browser.catalog=original_catalog;browser.feature_filter.select(0)
 browser.close_browser()
 assert(shell.lobby_state().slots[1].state==3 and shell.lobby_state().rules[4]==4000,'Cancel discards player and rule edits')
 # Library browsing shares previews and filters, but neither action can commit it.
 browser.show_browser()
 var playable_before=shell.lobby_map_catalog()
 var map_before=shell.lobby_state().map
 browser.source_tabs.current_tab=1
 assert(browser.filtered.size()==1 and browser.selected.library,'Library source lists disk maps')
 assert(browser.use_button.disabled and browser.play_button.disabled,'Library actions disabled')
 assert(not browser._commit(),'Library commit refused independently of button state')
 browser._play();assert(browser.open and shell.lobby_state().map==map_before,'Library Play cannot change/start the lobby')
 for frame in 400:
  await process_frame
  if browser.loaded_key==browser.selected.key:break
 assert(browser.terrain!=null and browser.objects!=null,'Library terrain and objects preview')
 assert(browser.selected.has('snowy'),'Library weather is read from map data')
 assert(browser.selected.players==fixture.players,'Library player count from map waypoints')
 assert(browser.selected.name!='Library fixture','Display name comes from map data, not renamed disk file')
 assert(not 'Map Library ·' in browser.selected.description,'No generated map description')
 browser._toggle_favorite();assert(browser.favorites.has(browser._favorite_id(browser.selected)),'Library map favorited')
 browser.favorites={};browser._load_favorites();assert(browser.favorites.has(browser._favorite_id(browser.selected)),'Favorites persist')
 assert(browser.selected.key!=fixture.key,'Library preview cannot shadow installed map')
 var sidecar_key='maps/'+str(browser.selected.preview_name)+'/map.ini'
 assert(fs.read_file(sidecar_key).get_string_from_utf8()=='; library preview sidecar','Library sidecars resolve under the isolated preview directory')
 assert(shell.lobby_map_catalog()==playable_before,'Library aliases never enter playable catalog')
 assert(browser.selected.get('definition',{}).get('ok',false),'Library feature scan')
 var cached_name=browser.selected.name;var cached_description=browser.selected.description
 var cache_reader=load('res://scripts/skirmish_map_metadata.gd').new();cache_reader.setup(fs,browser.metadata_cache_root)
 var cached_map=cache_reader.cached(browser.selected.path)
 assert(cached_map.name==cached_name and cached_map.description==cached_description,'Names and descriptions persist in a fresh metadata cache session')
 var strings_path=library_dir.path_join('map.str')
 var strings_file=FileAccess.open(strings_path,FileAccess.WRITE);strings_file.store_string('TEST:Name\n"New text"\nEND\n');strings_file.close()
 assert(cache_reader.cached(browser.selected.path).is_empty(),'Changing map.str invalidates cached metadata')
 DirAccess.remove_absolute(strings_path)
 browser._discover_library()
 assert(browser.library_queue.is_empty() and browser.library_catalog[0].name==cached_name,'Reopening library immediately restores cached names without map scans')
 var uninstalled_key=browser.selected.key
 var library_favorite=browser._favorite_id(browser.selected)
 var library_source=browser.selected.path
 var installed_dir=''
 browser._install_selected()
 assert(browser.source_tabs.current_tab==0 and browser.selected.get('installed',false),'Install adds map and selects it under Loaded maps')
 assert(not browser.play_button.disabled and browser._commit(),'Installed map is playable: '+browser.message.text)
 installed_dir=str(browser.selected.path).get_base_dir()
 assert(browser._favorite_id(browser.selected)==library_favorite and browser.favorite_button.text=='★ Favorited','Favorite follows installed copy')
 assert(fs.install_library_map(library_source,browser.install_root).ok,'Install is idempotent')
 var reopened_fs=ClassDB.instantiate('RetailFileSystem');assert(reopened_fs.mount_retail().ok)
 var persisted=reopened_fs.installed_maps(browser.install_root)
 assert(persisted.size()==1 and persisted[0].content_id==library_favorite and not persisted[0].library,'Installed map survives a fresh filesystem session')
 reopened_fs=null
 browser._toggle_favorite();assert(not browser.favorites.has(library_favorite),'Loaded map favorite toggles')
 browser._toggle_favorite();browser.favorites_filter.button_pressed=true
 assert(browser.filtered.size()==1 and browser.filtered[0].installed,'Favorites-only filter works for loaded maps')
 browser.favorites_filter.button_pressed=false
 browser.close_browser()
 # Play: commits like Use map and presses the lobby's start; the lobby queues the game on its next update.
 browser.show_browser();browser.rule_edits[4]=10000000
 browser._play();assert(not browser.open,'Play closes the browser: '+browser.message.text)
 var started={}
 for i in 60:
  shell.tick(.03);started=shell.take_new_game()
  if not started.is_empty():break
 assert(not started.is_empty(),'Play starts the game')
 assert(int(started.rules[4])==10000000,'Infinite starting resources reach the game')
 assert(world.set_simulation_profile('uber' if int(started.get('simulation_profile',0))==1 else 'faithful').ok)
 var preview_only=started.duplicate(true);preview_only.map=uninstalled_key
 assert(not world.prepare_new_game(preview_only).ok,'Uninstalled preview alias stays outside simulation map cache')
 var prepared:Dictionary=world.prepare_new_game(started)
 assert(prepared.ok,'Installed map reaches simulation preparation: '+str(prepared.get('errors',[])))
 var live:Dictionary=world.start_new_game({'logic_thread':false})
 assert(live.ok,'Installed map loads into a live game: '+str(live.get('errors',[])))
 for file in DirAccess.get_files_at(installed_dir):DirAccess.remove_absolute(installed_dir.path_join(file))
 DirAccess.remove_absolute(installed_dir);DirAccess.remove_absolute(browser.install_root)
 DirAccess.remove_absolute(browser.favorites_path)
 for file in DirAccess.get_files_at(browser.metadata_cache_root):DirAccess.remove_absolute(browser.metadata_cache_root.path_join(file))
 DirAccess.remove_absolute(browser.metadata_cache_root)
 DirAccess.remove_absolute(library_dir.path_join('Library fixture.map'));DirAccess.remove_absolute(library_dir.path_join('map.ini'));DirAccess.remove_absolute(library_dir)
 browser.free();shell.free();world.free();print('MAP BROWSER PASS');quit(0)
