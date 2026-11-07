extends SceneTree

class Setup extends RefCounted:
 func check(_rotwk: String, _bfme2: String) -> Dictionary:
  return {"ok":false,"errors":["RotWK missing"],"bfme2":{"ok":true,"errors":[]}}

class Filesystem extends RefCounted:
 var route := ""
 func mount_bfme2_path(_bfme2: String) -> Dictionary:
  route = "bfme2"
  return {"ok":false,"errors":["fixture mount rejection"]}
 func mount_retail_paths(_rotwk: String, _bfme2: String) -> Dictionary:
  route = "rotwk"
  return {"ok":false,"errors":["fixture mount rejection"]}

func _initialize() -> void: call_deferred("_run")

func _run() -> void:
 var screen = load("res://scripts/release/first_run.gd").new()
 root.add_child(screen)
 screen._setup = Setup.new()
 var check: Dictionary = screen._check_folders("", "bfme2 fixture")
 assert(check.ok and check.errors.is_empty())
 assert(not screen._check_folders("rotwk fixture", "bfme2 fixture").ok)
 var fs := Filesystem.new()
 screen._fs = fs
 assert(not (await screen._mount_and_remember("", "bfme2 fixture")).ok)
 assert(fs.route == "bfme2")
 assert(not (await screen._mount_and_remember("rotwk fixture", "bfme2 fixture")).ok)
 assert(fs.route == "rotwk")
 screen.free()
 print("Optional RotWK first-run: PASS (BFME2 validation and both mount routes)")
 quit()
