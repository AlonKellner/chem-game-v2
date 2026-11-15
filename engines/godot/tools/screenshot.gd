extends SceneTree

# Screenshots of the host at five moments of the demo, for looking at what a
# player sees (make screenshots -> build/screenshots/*.png). It plays with the
# pointer, as tests/test_host_smoke.gd does.
#
#   godot --path engines/godot -s tools/screenshot.gd -- <output directory>

var _lab: ChemLab

func _drag(what: String, to: String) -> void:
	var a := _lab.locate(what)
	var b := _lab.locate(to)
	_lab.pointer(a.x, a.y, false)
	_lab.pointer(a.x, a.y, true)
	_lab.pointer(b.x, b.y, true)
	_lab.pointer(b.x, b.y, false)

func _click(what: String) -> void:
	var a := _lab.locate(what)
	_lab.pointer(a.x, a.y, true)
	_lab.pointer(a.x, a.y, false)

func _grams(id: String, species: String) -> float:
	for item in JSON.parse_string(_lab.report())["items"]:
		if item["id"] == id:
			for row in item.get("contents", []):
				if row["species"] == species:
					return row["g"]
	return 0.0

func _celsius(id: String) -> float:
	for item in JSON.parse_string(_lab.report())["items"]:
		if item["id"] == id:
			return item["celsius"]
	return 0.0

# frames at the chosen speed until `done`, or a bound: a script on the engine's
# own thread must never wait for something that may not come
func _watch(done: Callable, what: String) -> bool:
	for i in 2000:
		if done.call():
			return true
		_lab.frame(1.0 / 60.0)
	push_error("screenshot.gd: gave up waiting for " + what)
	return false

func _shoot(dir: String, name: String) -> void:
	await process_frame
	await process_frame
	var image := root.get_texture().get_image()
	if image != null: # a headless Godot paints nothing
		image.save_png(dir.path_join(name + ".png"))

func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	var dir: String = args[0] if args.size() > 0 else "user://"
	DirAccess.make_dir_recursive_absolute(dir)
	_lab = ChemLab.new()
	root.add_child(_lab)
	await process_frame
	await _play(dir)
	print("screenshot.gd: done, at step ", JSON.parse_string(_lab.report())["step"])
	quit()

func _play(dir: String) -> void:
	_click("pause")
	_drag("item:flask", "cell:3,1")
	_drag("item:burner", "item:flask")
	_drag("item:jar", "cell:1,1")
	_drag("item:jar", "item:flask")
	_click("panel:tip:malachite")
	_drag("item:balloon", "item:flask")
	_click("valve:burner@1")
	await _shoot(dir, "1_set_up")
	_click("speed:600")
	if not _watch(func(): return _grams("flask", "malachite") < 2.0, "the malachite to decompose"):
		return
	_click("pause")
	await _shoot(dir, "2_decomposing")
	_click("speed:600")
	if not _watch(func(): return _grams("flask", "malachite") < 0.005, "the malachite to be gone"):
		return
	_drag("item:balloon", "outbox")
	_click("valve:burner@0")
	if not _watch(func(): return _celsius("flask") < 30.0, "the flask to cool"):
		return
	_click("pause")
	_drag("item:acid", "cell:2,1")
	_drag("item:acid", "item:flask")
	await _shoot(dir, "3_pour_panel") # nothing is clicked while the panel is open: a click outside it closes it
	_click("panel:pour")
	_click("stir:flask")
	_click("valve:burner@0.2")
	_click("speed:600")
	if not _watch(func(): return _grams("flask", "copper_oxide") < 0.001, "the oxide to dissolve"):
		return
	_click("stir:flask")
	_drag("item:condenser", "item:flask")
	_drag("item:beaker", "cell:5,1")
	_drag("item:beaker", "item:condenser")
	_click("valve:burner@1")
	if not _watch(func(): return _grams("flask", "water") < 30.0, "the solution to boil down"):
		return
	_click("pause")
	await _shoot(dir, "4_boiling")
	_click("speed:600")
	if not _watch(func(): return _grams("flask", "water") < 10.0, "the solution to boil down"):
		return
	_click("valve:burner@0")
	if not _watch(func(): return _celsius("flask") < 21.0, "the flask to cool"):
		return
	for i in 90:
		_lab.frame(1.0 / 60.0)
	_click("pause")
	await _shoot(dir, "5_crystals")
