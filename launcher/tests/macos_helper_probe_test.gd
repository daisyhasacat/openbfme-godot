## Lane MACPLAY-1 (Sol r3, P2): the launcher's question "is this transaction's update helper still running?" (MacOSUpdate.
## _helper_process_status, which every helper retirement asks) on any system with a build of tools/release/macos_bundle_swap.c beside
## the running Godot (<app>/Contents/MacOS/openbfme-bundle-swap; tools/release/test_macplay.py builds it for Linux with the libproc shim):
##   godot --headless --path launcher --script res://tests/macos_helper_probe_test.gd -- --txid=<id> [--pid=<n>]
## prints "PROBE DIR <helper transaction dir>" and, with --pid, "PROBE known=<bool> active=<bool>", then exits 0.
extends SceneTree

const MacOSUpdate := preload("res://scripts/core/macos_update.gd")


func _init() -> void:
	var txid := ""
	var pid := -1
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--txid="):
			txid = arg.trim_prefix("--txid=")
		elif arg.begins_with("--pid="):
			pid = int(arg.trim_prefix("--pid="))
	if txid == "":
		printerr("PROBE needs --txid")
		quit(2)
		return
	print("PROBE DIR " + MacOSUpdate._helper_transaction_dir(txid))
	if pid > 0:
		var status: Dictionary = MacOSUpdate._helper_process_status(
			{"txid": txid, "apply_pid": pid, "helper": MacOSUpdate._local_swap_helper()})
		print("PROBE known=%s active=%s" % [status.known, status.active])
	quit(0)
