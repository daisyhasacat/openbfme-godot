## Headless test of lane CAMPSAVE-1 (the owner's campaign report, 2026-10-14: the campaign end to end, saves, the screens between missions): scenes/game.tscn
## runs scripts/campaign_save_test.gd's scenarios as child processes on one temporary Save folder and Options.ini:
##   1. menu: Amon Sul won, the score screen, the auto-save, CampaignMenu.apt (Next Mission), its Save and Load, Next Mission, Dark Eye lost, the menu
##      after the defeat (no auto-save), Replay Mission;
##   2. continue: a restarted game's Load Game with only run 1's auto-save: CampaignMenu.apt, Next Mission;
##   3. continue_command: AptMainMenu::ContinueCampaign without and with 00000000.sav;
##   4. quit: the in-game menu's Restart and Exit Mission in a mission;
##   4b. the in-game Save / Load screens over mission 0 through the real UI (SAVE-UI1's scripts/save_flow.gd run_ui_test on the campaign started from the
##      main menu: Save, typed name, Cancel / Escape, Options, Save, Load, Resume);
##   4c. eight: the whole ANGMAR_CAMPAIGN through its menus (win, score screen, auto-save, CampaignMenu.apt, Next Mission x7, the eighth win);
##   5. bonus: Fornost won, no auto-save, no menu, Options.ini's BCU = true, the main menu.
##
##   godot --headless --path godot --script res://tests/campsave1_test.gd
##
## Needs ROTWK_INSTALL and BFME2_INSTALL (prints SKIP and exits 77 without). Exit codes: 0 pass, 1 fail, 77 skip.
extends SceneTree

var _failures := 0


func _initialize() -> void:
	var code: int = _run()
	print("CAMPSAVE1 %s" % ["PASS" if code == 0 else ("SKIP" if code == 77 else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", what)
	if not ok:
		_failures += 1


## the verdict of a CAMPSAVE1 scenario run: "" when it passed, else why not (a run that printed no check, a failed check, no or two RESULT lines or a
## non-zero exit does not pass)
static func camp_verdict(text: String, code: int) -> String:
	var checks := 0
	var results := 0
	for line in text.split("\n"):
		if line.begins_with("CAMPSAVE1 CHECK ok:"):
			checks += 1
		elif line.begins_with("CAMPSAVE1 CHECK FAILED:"):
			return "a check failed: " + line
		elif line == "CAMPSAVE1 RESULT: ok":
			results += 1
		elif line.begins_with("CAMPSAVE1 RESULT:"):
			return "result line: " + line
	if checks == 0:
		return "no check ran"
	if results != 1:
		return "%d RESULT ok lines (want 1)" % results
	if code != 0:
		return "exit code %d" % code
	return ""


## the verdict of SAVE-UI1's run: its own strict checker (tests/saveui1_test.gd: every named check ok once, the exact RESULT line, exit 0)
static func ui_verdict(text: String, code: int) -> String:
	return load("res://tests/saveui1_test.gd").verdict(text, code)


func _game(args: Array) -> String:
	var output: Array = []
	var code := OS.execute(OS.get_executable_path(), ["--headless", "--path", ProjectSettings.globalize_path("res://"), "--"] + args, output, true)
	var text: String = "".join(output)
	for line in text.split("\n"):
		if line.begins_with("CAMPSAVE1") or line.begins_with("CAMPAIGN ") or line.begins_with("GAME FAIL") or line.contains("SCRIPT ERROR"):
			print("    " + line)
	var why := camp_verdict(text, code)
	_check(why.is_empty(), "%s: exit %d%s" % [" ".join(args), code, "" if why.is_empty() else ": " + why])
	return text


## SAVE-UI1's real-UI flow in the campaign's first mission (its own Options.ini: health bars always on, the control for its occlusion checks)
func _ui_run(args: Array, root: String) -> void:
	var dir := root.path_join("saveui")
	DirAccess.make_dir_recursive_absolute(dir)
	var options := dir.path_join("options.ini")
	var opt := FileAccess.open(options, FileAccess.WRITE)
	opt.store_string("OpenBFMEHealthBars = always\n")
	opt.close()
	var all := ["--headless", "--path", ProjectSettings.globalize_path("res://"), "--"] + args + ["--save-ui-test", "--save-dir=" + dir, "--options-file=" + options,
		"--no-intro", "--movies=off"]
	var output: Array = []
	var code := OS.execute(OS.get_executable_path(), all, output, true)
	var text: String = "".join(output)
	for line in text.split("\n"):
		if line.begins_with("SAVEUI "):
			print("    " + line)
	var why := ui_verdict(text, code)
	_check(why.is_empty(), "the in-game Save / Load flow over mission 0: exit %d%s" % [code, "" if why.is_empty() else ": " + why])
	for f in DirAccess.get_files_at(dir):
		DirAccess.remove_absolute(dir.path_join(f))
	DirAccess.remove_absolute(dir)


## the checkers' own cases (no game needed): each refusal a wrapper must make
func _checker_cases() -> void:
	var saveui = load("res://tests/saveui1_test.gd")
	var good_ui := ""
	for c in saveui.CHECKS:
		good_ui += "SAVEUI check %s: ok detail\n" % c
	good_ui += "SAVEUI RESULT %d checks, 0 failed: %s\n" % [saveui.CHECKS.size(), ",".join(saveui.CHECKS)]
	_check(ui_verdict(good_ui, 0) == "", "ui checker: a full passing run passes")
	_check(ui_verdict("SAVEUI RESULT 0 checks, 0 failed: \n", 0) != "", "ui checker: zero checks with a clean RESULT line fails")
	_check(ui_verdict("", 0) != "", "ui checker: an empty output fails")
	_check(ui_verdict(good_ui, 1) != "", "ui checker: a non-zero exit fails")
	_check(ui_verdict(good_ui.replace("SAVEUI RESULT", "SAVEUI RESUL"), 0) != "", "ui checker: a missing RESULT line (truncated) fails")
	_check(ui_verdict(good_ui.replace("SAVEUI check typed-name: ok detail\n", ""), 0) != "", "ui checker: a missing check fails")
	_check(ui_verdict(good_ui.replace("typed-name: ok", "typed-name: FAILED"), 0) != "", "ui checker: a failed check fails")
	var good_camp := "CAMPSAVE1 CHECK ok: the main menu\nCAMPSAVE1 RESULT: ok\n"
	_check(camp_verdict(good_camp, 0) == "", "campaign checker: a passing run passes")
	_check(camp_verdict("CAMPSAVE1 RESULT: ok\n", 0) != "", "campaign checker: a RESULT with no check before it fails")
	_check(camp_verdict("", 0) != "", "campaign checker: an empty output fails")
	_check(camp_verdict(good_camp, 1) != "", "campaign checker: a non-zero exit fails")
	_check(camp_verdict("CAMPSAVE1 CHECK ok: the main menu\n", 0) != "", "campaign checker: a missing RESULT line (truncated) fails")
	_check(camp_verdict("CAMPSAVE1 CHECK ok: a\nCAMPSAVE1 CHECK FAILED: b\nCAMPSAVE1 RESULT: ok\n", 0) != "", "campaign checker: a failed check beside a RESULT ok fails")
	_check(camp_verdict("CAMPSAVE1 CHECK ok: a\nCAMPSAVE1 RESULT: ok\nCAMPSAVE1 RESULT: ok\n", 0) != "", "campaign checker: two RESULT lines fail")


func _run() -> int:
	_checker_cases()
	if _failures > 0:
		return 1
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("CAMPSAVE1 SKIP: ROTWK_INSTALL / BFME2_INSTALL are not set")
		return 77
	var root := OS.get_temp_dir().path_join("openbfme-campsave1-%d" % OS.get_process_id())
	var save1 := root.path_join("save1")
	var save2 := root.path_join("save2")
	DirAccess.make_dir_recursive_absolute(save1)
	DirAccess.make_dir_recursive_absolute(save2)
	var options := root.path_join("Options.ini")
	var common := ["--movies=off", "--options-file=" + options]
	_game(["--campaign=ANGMAR_CAMPAIGN", "--mission=2", "--campaign-win", "--save-dir=" + save1, "--campsave-test=menu"] + common)
	var auto := ""
	for f in DirAccess.get_files_at(save1):
		if f.begins_with("Auto Save for "):
			auto = f
	_check(not auto.is_empty(), "run 1 wrote the auto-save (%s)" % auto)
	if not auto.is_empty():
		DirAccess.copy_absolute(save1.path_join(auto), save2.path_join(auto))
	_game(["--save-dir=" + save2, "--campsave-test=continue"] + common)
	_game(["--save-dir=" + save2, "--campsave-test=continue_command"] + common)
	_game(["--campaign=ANGMAR_CAMPAIGN", "--mission=1", "--save-dir=" + save2, "--campsave-test=quit"] + common)
	_ui_run(["--campaign-menu=Expansion1Campaign:N", "--res=2560x1440"], root)
	var save8 := root.path_join("save8")
	DirAccess.make_dir_recursive_absolute(save8)
	_game(["--campaign=ANGMAR_CAMPAIGN", "--mission=0", "--campaign-win", "--save-dir=" + save8, "--campsave-test=eight"] + common)
	_game(["--campaign=ANGMAR_CAMPAIGN", "--mission=7", "--campaign-win", "--save-dir=" + save2, "--campsave-test=bonus"] + common)
	_check(FileAccess.get_file_as_string(options).contains("BCU = true"), "the bonus unlock was written to the run's own Options.ini")
	for dir in [save1, save2, save8]:
		for f in DirAccess.get_files_at(dir):
			DirAccess.remove_absolute(dir.path_join(f))
		DirAccess.remove_absolute(dir)
	DirAccess.remove_absolute(options)
	DirAccess.remove_absolute(root)
	return 0 if _failures == 0 else 1
