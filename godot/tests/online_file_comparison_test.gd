extends SceneTree
const Diagnostics = preload("res://scripts/online_compatibility.gd")

func _initialize() -> void:
	var report: String = Diagnostics.compare([["engine/src/a.cpp", "a".repeat(32), 0], ["entry/ini.big/a.ini", "b".repeat(32), 10]], [["engine/src/a.cpp", "c".repeat(32), 0], ["entry/ini.big/b.ini", "d".repeat(32), 20]])
	assert(report.contains("engine/src/a.cpp\n  You:  %s (0 bytes)\n  Host: %s (0 bytes)" % ["a".repeat(32), "c".repeat(32)]))
	assert(report.contains("entry/ini.big/a.ini\n  You:  %s (10 bytes)\n  Host: MISSING" % "b".repeat(32)))
	assert(report.contains("entry/ini.big/b.ini\n  You:  MISSING\n  Host: %s (20 bytes)" % "d".repeat(32)))
	assert(report.contains("3 differing files/settings; 2 local records; 2 host records."))
	assert(Diagnostics.compare([["a", "a".repeat(32), 0], ["a", "a".repeat(32), 0]], []).contains("duplicate"))
	assert(Diagnostics.compare([], []).contains("All supplied file checksums match"))
	var rows: Array = []
	for i in 600: rows.append(["entry/file-%d" % i, "a".repeat(32), 10])
	var pages: Array = Diagnostics._pages(rows)
	assert(pages.size() == 3)
	assert(pages[0].size() == 256)
	assert(pages[1].size() == 256)
	assert(pages[2].size() == 88)
	print("Per-file comparisons: 10 checks passed")
	quit()
