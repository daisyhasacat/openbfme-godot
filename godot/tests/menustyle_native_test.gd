## Native presentation-source test for MENUSTYLE-1.
## Run with ROTWK_INSTALL and BFME2_INSTALL set:
## godot --headless --path godot --script res://tests/menustyle_native_test.gd
extends SceneTree

const EXIT_SKIP := 77

var _failures := 0


func _initialize() -> void:
	var code: int = await _run()
	print("MENUSTYLE NATIVE %s" % ["PASS" if code == 0 else ("SKIP" if code == EXIT_SKIP else "FAIL")])
	quit(code)


func _check(ok: bool, message: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", message)
	if not ok:
		_failures += 1


func _shell_cycle(player: Node, world: Node, fs: RefCounted, presentation: RefCounted, style: String) -> void:
	print("  booting %s shell" % style)
	var config: Dictionary = {}
	if presentation != null:
		config["presentation_fs"] = presentation
	var boot: Dictionary = player.boot_shell(fs, world, config)
	_check(boot.ok, "%s shell boot: %s" % [style, str(boot.get("errors", []))])
	if not boot.ok:
		return
	_check(player.shell_push("MainMenu.apt"), "%s shell pushes MainMenu.apt" % style)
	for frame in 12:
		player.tick(0.033)
		player.render(true)
	var top: int = player.shell_top_level()
	var shown: Dictionary = player.shell_invoke(top, "ShowMainMenu", PackedStringArray())
	_check(shown.ok, "%s ShowMainMenu: %s" % [style, str(shown.get("error", ""))])
	for frame in 12:
		player.tick(0.033)
		player.render(true)
	var report: Dictionary = player.get_shell_report()
	_check(report.get("errors", []).is_empty(), "%s shell/render has no reported errors: %s" % [style, str(report.get("errors", []))])
	_check(report.get("presentation_style", "") == ("bfme2-106" if presentation != null else "rotwk-201"),
		"%s presentation provenance is reported as %s" % [style, str(report.get("presentation_style", ""))])
	var render_report: Dictionary = player.get_report()
	_check(render_report.get("errors", []).is_empty() and render_report.get("movie_errors", []).is_empty(),
		"%s render/movie errors are surfaced: %s %s" %
		[style, str(render_report.get("errors", [])), str(render_report.get("movie_errors", []))])
	_check(render_report.get("view3d_presentation_style", "") == ("bfme2-106" if presentation != null else "rotwk-201"),
		"%s decorative View3D uses the presentation source: %s" % [style, str(render_report.get("view3d_presentation_style", ""))])
	if not report.get("errors", []).is_empty() or not render_report.get("errors", []).is_empty() or not render_report.get("movie_errors", []).is_empty():
		print("  info %s full shell report: %s" % [style, str(report)])


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: set ROTWK_INSTALL and BFME2_INSTALL")
		return EXIT_SKIP
	for type_name in ["RetailFileSystem", "GameWorld", "GameAudio", "AptMenuPlayer"]:
		if not ClassDB.class_exists(type_name):
			print("FAIL: the native extension does not register ", type_name)
			return 1

	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mounted: Dictionary = fs.mount_retail()
	_check(mounted.ok, "normal RotWK/BFME2 mount: %s" % str(mounted.get("errors", [])))
	if not mounted.ok:
		return 1
	var shell_fs: RefCounted = fs.create_bfme2_shell()
	_check(shell_fs != null and shell_fs.is_mounted() and shell_fs.is_shell_source(),
		"BFME2 shell source mounts only as presentation data (%s)" % str(fs.get_last_error()))
	if shell_fs == null:
		return 1

	var apt_path := "MainMenu.apt"
	var original_apt_archive: String = fs.get_archive_for_file(apt_path)
	var presentation_apt_archive: String = shell_fs.get_archive_for_file(apt_path)
	_check(not original_apt_archive.is_empty() and original_apt_archive.to_lower().contains("rotwk"),
		"normal MainMenu.apt provenance is RotWK: %s" % original_apt_archive)
	_check(not presentation_apt_archive.is_empty() and presentation_apt_archive.to_lower().contains("bfme2"),
		"presentation MainMenu.apt provenance is BFME2: %s" % presentation_apt_archive)
	var game_data_archive: String = fs.get_archive_for_file("data\\ini\\gamedata.ini")
	var object_archive: String = fs.get_archive_for_file("data\\ini\\object\\goodfaction\\units\\men\\gondorfighter.ini")
	_check(not game_data_archive.is_empty() and not object_archive.is_empty(),
		"original GameData.ini and object INI are present in the normal source")
	_check(fs.get_archive_for_file(apt_path) == original_apt_archive and
		fs.get_archive_for_file("data\\ini\\gamedata.ini") == game_data_archive and
		fs.get_archive_for_file("data\\ini\\object\\goodfaction\\units\\men\\gondorfighter.ini") == object_archive,
		"original APT, GameData, and object provenance remain unchanged")

	var world: Node3D = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup: Dictionary = world.setup(fs)
	_check(setup.ok, "GameWorld setup uses original filesystem: %s" % str(setup.get("errors", [])))
	if not setup.ok:
		return 1

	var audio: Node = ClassDB.instantiate("GameAudio")
	root.add_child(audio)
	var audio_boot: Dictionary = audio.boot(fs, {"seed": 7})
	_check(audio_boot.ok, "original GameAudio boots: %s" % str(audio_boot.get("errors", [])))
	if not audio_boot.ok:
		return 1
	_check(audio.engine_api_installed(), "original AudioApi manager is installed")
	var install_shell_audio: Dictionary = audio.set_shell_source(shell_fs)
	_check(install_shell_audio.ok, "private BFME2 shell audio boots: %s" % str(install_shell_audio.get("errors", [])))
	_check(audio.engine_api_installed(), "BFME2 shell audio does not replace the original AudioApi manager")
	var shell_music: int = audio.play_shell_music(false)
	await create_timer(0.5).timeout
	_check(shell_music >= (1 << 32) + 5 and audio.is_music_playing(),
		"BFME2 shell music starts with a distinct handle: %s %s" % [str(shell_music), audio.get_music_track()])
	var shell_track: String = audio.get_music_track()
	var shell_track_info: Dictionary = audio.get_event_info(shell_track)
	var bfme_music_source := false
	for path in shell_track_info.get("files", []):
		var owner: String = shell_fs.get_archive_for_file(String(path))
		bfme_music_source = bfme_music_source or owner.to_lower().contains("bfme2")
	_check(not shell_track.is_empty() and bfme_music_source,
		"the active shell music track resolves through BFME2 archives: %s %s" % [shell_track, str(shell_track_info.get("files", []))])
	var reset_audio: Dictionary = audio.set_shell_source(null)
	_check(reset_audio.ok and audio.engine_api_installed(), "resetting shell audio preserves the original AudioApi manager")
	audio.stop_music(false)
	var original_music: int = audio.play_shell_music(false)
	await create_timer(0.5).timeout
	var original_track: String = audio.get_music_track()
	var original_track_info: Dictionary = audio.get_event_info(original_track)
	var normal_music_source := false
	for path in original_track_info.get("files", []):
		var owner: String = fs.get_archive_for_file(String(path))
		normal_music_source = normal_music_source or not owner.is_empty()
	_check(original_music >= 5 and not original_track.is_empty() and normal_music_source,
		"reset shell music uses the original manager and mounted retail source: %s %s" % [original_track, str(original_track_info.get("files", []))])
	audio.stop_music(false)

	var player: Node2D = ClassDB.instantiate("AptMenuPlayer")
	root.add_child(player)
	_shell_cycle(player, world, fs, null, "original")
	_shell_cycle(player, world, fs, shell_fs, "BFME2")
	_shell_cycle(player, world, fs, null, "original-again")
	player.queue_free()
	var active_shell: Dictionary = audio.set_shell_source(shell_fs)
	_check(active_shell.ok, "active shell audio installs before shutdown: %s" % str(active_shell.get("errors", [])))
	var active_shell_music: int = audio.play_shell_music(false)
	await create_timer(0.5).timeout
	_check(active_shell_music >= (1 << 32) + 5 and audio.is_music_playing(),
		"active shell audio is playing before shutdown: %s %s" % [str(active_shell_music), audio.get_music_track()])
	audio.shutdown()
	_check(not audio.engine_api_installed() and not audio.is_music_playing(),
		"shutdown stops an active private shell audio child")
	var reboot_audio: Dictionary = audio.boot(fs, {"seed": 7})
	_check(reboot_audio.ok and audio.engine_api_installed(), "audio reboots after private child shutdown: %s" % str(reboot_audio.get("errors", [])))
	var reboot_shell: Dictionary = audio.set_shell_source(shell_fs)
	_check(reboot_shell.ok, "private shell audio reinstalls after reboot: %s" % str(reboot_shell.get("errors", [])))
	var reboot_music: int = audio.play_shell_music(false)
	await create_timer(0.5).timeout
	_check(reboot_music >= (1 << 32) + 5 and audio.is_music_playing(),
		"rebooted private shell audio plays before node teardown: %s %s" % [str(reboot_music), audio.get_music_track()])
	audio.queue_free()
	world.queue_free()
	await process_frame
	print("  %d failed assertion(s)" % _failures)
	return 0 if _failures == 0 else 1
