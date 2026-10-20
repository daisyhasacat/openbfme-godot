extends SceneTree
func _initialize(): call_deferred('_run')
func _run():
 var script=GDScript.new()
 script.source_code="extends 'res://scripts/game.gd'\nvar resumed: Dictionary = {}\nvar snapshot_ok := true\nvar replayed := []\nfunc _replay_restart_buildings(layout: String) -> Dictionary:\n replayed.append(layout)\n return {'ok':true}\nfunc _record_restart_buildings() -> Dictionary:\n return {'ok':snapshot_ok}\nfunc _begin_game(message: Dictionary) -> void:\n resumed=message\n"
 assert(script.reload()==OK)
 var game=script.new()
 var message={'mode':'skirmish','map':'maps/test/test.map','seed':42,'starting_cash':2000,'slots':[{'name':'Player','state':6,'create_a_hero':PackedByteArray([1,2,3])}]}
 var config=ConfigFile.new()
 config.set_value('restart','skirmish',message)
 config.set_value('restart','buildings',game.CONSOLE_RESTART_LAYOUT)
 assert(config.save(game.CONSOLE_RESTART_FILE)==OK)
 assert(game._resume_console_restart())
 assert(game.resumed==message)
 assert(game._restart_building_layout==game.CONSOLE_RESTART_LAYOUT)
 game._restore_restart_buildings();game._restore_restart_buildings()
 assert(game.replayed==[game.CONSOLE_RESTART_LAYOUT])
 assert(not FileAccess.file_exists(game.CONSOLE_RESTART_FILE))
 assert(not game._resume_console_restart())
 config.set_value('restart','skirmish',{})
 assert(config.save(game.CONSOLE_RESTART_FILE)==OK)
 assert(not game._resume_console_restart())
 assert(not FileAccess.file_exists(game.CONSOLE_RESTART_FILE))
 game._state=game.State.PLAYING;game._game_kind='skirmish';game._last_new_game=message
 assert(game._save_console_restart())
 assert(config.load(game.CONSOLE_RESTART_FILE)==OK)
 assert(config.get_value('restart','buildings')==game.CONSOLE_RESTART_LAYOUT)
 DirAccess.remove_absolute(game.CONSOLE_RESTART_FILE)
 game.snapshot_ok=false
 # Expected snapshot failure must leave no restart handoff and keep the process running.
 game._lua_console=load('res://scripts/lua_console.lua').new()
 root.add_child(game._lua_console)
 assert(not game._save_console_restart())
 assert(not FileAccess.file_exists(game.CONSOLE_RESTART_FILE))
 game._lua_console.free()
 game.free()
 print('CONSOLE RESTART PASS');quit(0)
