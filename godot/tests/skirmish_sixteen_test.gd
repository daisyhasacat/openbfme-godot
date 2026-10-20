extends SceneTree
## Opt-in installed-map integration: -- <mounted merged-map key>.
func _initialize():call_deferred('_run')
func _run():
 var args=OS.get_cmdline_user_args()
 if args.size()!=1:print('SKIP: supply an installed 32-start merged-map key');quit(77);return
 var fs=ClassDB.instantiate('RetailFileSystem');assert(fs.mount_retail().ok)
 var world=ClassDB.instantiate('GameWorld');root.add_child(world);assert(world.setup(fs).ok)
 var shell=ClassDB.instantiate('AptMenuPlayer');root.add_child(shell)
 assert(shell.boot_shell(fs,world,{'skirmish_ini':'/tmp/openbfme-sixteen-test.ini'}).ok)
 assert(shell.shell_push('Skirmish.apt'))
 for i in 80:shell.tick(.03)
 assert(shell.lobby_apply({'profile':'SixteenTest'}).ok)
 for i in 30:shell.tick(.03)
 var browser=load('res://scripts/skirmish_map_browser.gd').new();browser.shell=shell;browser.fs=fs;browser.resolver=world;root.add_child(browser)
 browser.show_browser()
 var entries=browser.catalog.filter(func(e):return e.key==args[0])
 assert(entries.size()==1,'Installed map in playable catalog')
 browser.selected=entries[0]
 assert(int(browser.selected.players)==32)
 browser._fill_players()
 assert(browser.draft.slots.size()==16 and browser.assignments.size()==16)
 # Use the last available start as well as positions in every tile.
 browser.assignments[15]=31
 for i in 16:
  browser.draft.slots[i].template=browser.draft.factions.filter(func(f):return not str(f.building).is_empty())[0].value
  browser.draft.slots[i].team=i/4
 assert(browser._commit(),'Sixteen-slot browser commit: '+browser.message.text)
 var state=shell.lobby_state()
 assert(state.slots.size()==16 and state.slots[15].start==31 and state.slots[15].team==3)
 browser._play()
 var message={}
 for i in 60:
  shell.tick(.03);message=shell.take_new_game()
  if not message.is_empty():break
 assert(not message.is_empty(),'Sixteen-player Play queues game')
 assert(message.slots.size()==16)
 assert(world.set_simulation_profile('uber' if int(message.get('simulation_profile',0))==1 else 'faithful').ok)
 var prepared=world.prepare_new_game(message)
 assert(prepared.ok,'Prepare sixteen: '+str(prepared.get('errors',[])))
 var live=world.start_new_game({'logic_thread':false})
 assert(live.ok,'Load sixteen: '+str(live.get('errors',[])))
 var status=world.get_player_status_state()
 assert(status.slots.size()==16)
 for slot in status.slots:assert(slot.has_player,'Every active slot has a player')
 browser.free();shell.free();world.free()
 print('SIXTEEN SKIRMISH PASS');quit(0)
