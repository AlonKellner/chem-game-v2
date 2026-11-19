extends SceneTree

# Screenshots of the host at five moments of the demo, for looking at what a
# player sees (make screenshots -> build/screenshots/*.png). It plays with the
# pointer, as tests/test_host_smoke.gd does. At each moment the optical pass
# alone, settled, is held to what each pixel should show (GAME_SPEC §18: O4), and
# at the end the GPU's time for one pass is measured.
#
#   godot --path engines/godot -s tools/screenshot.gd -- <output directory>

var _lab: ChemLab
var _view: SubViewport # the game's own 1920 x 1080, whatever the window is
var _failed := false

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
	# the game is paused: the optical pass settles (each pixel's history), in a bounded number of frames
	for i in 40:
		await process_frame
	if DisplayServer.get_name() == "headless": # it paints nothing
		return
	var image := _view.get_texture().get_image()
	image.save_png(dir.path_join(name + ".png"))
	_lab.set_flat_visible(false)
	await process_frame
	await process_frame
	var alone := _view.get_texture().get_image()
	alone.convert(Image.FORMAT_RGBA8)
	var parity := _lab.optics_parity(alone.get_data())
	_lab.set_flat_visible(true)
	if parity.size() != 5 or parity[0] == 0 or parity[2] > 0:
		_failed = true
		push_error("screenshot.gd: O4: the GPU's settled picture is visibly not what it should be at " + name)
	if parity.size() == 5:
		print("O4 %s: %d pixels in bodies, the largest difference %d on the eye metric (at %d, %d), %d visible" % [name, parity[0], parity[1], parity[3], parity[4], parity[2]])
		if parity[2] > 0:
			alone.save_png(dir.path_join(name + "_optics_alone.png"))

# The time of a frame with the optical pass run `extra` more times: one pass is
# too quick to time alone, and a display hands frames out in whole refreshes,
# so the cost of one is the slope between two heavy loads. The scene is still:
# this is a settled pass's cost (each pixel finds its content unchanged and asks for nothing).
func _time(extra: int) -> float:
	_lab.set_optics_extra(extra)
	for i in 5:
		await process_frame
	var start := Time.get_ticks_usec()
	for i in 20:
		await process_frame
	var ms := float(Time.get_ticks_usec() - start) / 20000.0
	_lab.set_optics_extra(0)
	return ms

func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	var dir: String = args[0] if args.size() > 0 else "user://"
	DirAccess.make_dir_recursive_absolute(dir)
	_view = SubViewport.new()
	_view.size = Vector2i(1920, 1080)
	_view.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(_view)
	_lab = ChemLab.new()
	_view.add_child(_lab)
	var shown := TextureRect.new()
	shown.texture = _view.get_texture()
	shown.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	shown.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.add_child(shown)
	await process_frame
	await _play(dir)
	if DisplayServer.get_name() != "headless":
		DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
		var few := await _time(10)
		var many := await _time(30)
		print("a frame with 10 more optical passes: %.2f ms; with 30: %.2f ms; one settled pass at 1920 x 1080: %.3f ms" % [few, many, (many - few) / 20.0])
		_lab.set_optics_restarting(true) # the worst frame: every pixel's history starts again
		few = await _time(2)
		many = await _time(6)
		_lab.set_optics_restarting(false)
		print("every history starting again: a frame with 2 more passes: %.2f ms; with 6: %.2f ms; one such pass: %.3f ms" % [few, many, (many - few) / 4.0])
	print("screenshot.gd: done, at step ", JSON.parse_string(_lab.report())["step"])
	quit(1 if _failed else 0)

func _play(dir: String) -> void:
	_click("pause")
	_drag("item:flask", "square:11,12")
	_drag("item:burner", "item:flask")
	_drag("item:jar", "square:4,13")
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
	_drag("item:balloon", "slot:1") # its goal (GAME_SPEC §13.5)
	_click("valve:burner@0")
	if not _watch(func(): return _celsius("flask") < 30.0, "the flask to cool"):
		return
	_click("pause")
	_drag("item:acid", "square:7,12")
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
	_drag("item:beaker", "square:19,12")
	_drag("item:beaker", "item:condenser")
	# a monitor on the flask's water, rigged to close the valve and wired to an alarm, as the tutorial asks (GAME_SPEC §13.3)
	_drag("drawer:monitor", "square:1,1")
	for what in ["edit:item:flask", "edit:quantity", "edit:pick:water", "edit:compare:below", "edit:value:0"]:
		_click(what)
	for c in "12":
		_lab.key(0, c.unicode_at(0))
	await _shoot(dir, "6_monitor_editor")
	_click("edit:set")
	_drag("port:monitor1", "item:burner")
	_click("edit:on:value")
	_lab.key(0, "0".unicode_at(0))
	_click("edit:set")
	_drag("drawer:alarm", "square:3,1")
	_drag("port:monitor1", "item:alarm1")
	_click("valve:burner@1")
	if not _watch(func(): return _grams("flask", "water") < 30.0, "the solution to boil down"):
		return
	_click("pause")
	await _shoot(dir, "4_boiling")
	_click("speed:600")
	if not _watch(func(): return _grams("flask", "water") < 12.0, "the solution to boil down"):
		return
	if _lab.paused(): # the alarm stopped the clock at 12 g, and the rig closed the valve
		_click("pause")
	if not _watch(func(): return _celsius("flask") < 21.0, "the flask to cool"):
		return
	for i in 90:
		_lab.frame(1.0 / 60.0)
	_click("pause")
	await _shoot(dir, "5_crystals")
	_click("notices") # what the lab told of the demo (GAME_SPEC §13.4)
	await _shoot(dir, "10_notices")
	_click("notices")
	_click("item:flask") # chosen, it is looked inside (GAME_SPEC §13.6)
	_click("debug")
	await _shoot(dir, "13_debug")
	_click("debug")
	# Stage 14: signals and a circuit, on a bench of its own, without the tutorial
	_lab.queue_free()
	await process_frame
	_lab = ChemLab.new()
	_lab.tutorial_path = ""
	_view.add_child(_lab)
	await process_frame
	_click("pause")
	_drag("item:flask", "square:11,12")
	_drag("item:burner", "item:flask")
	for made in [["timer", "square:2,1"], ["monitor", "square:4,2"], ["alarm", "square:6,1"]]:
		_drag("drawer:" + made[0], made[1])
		if _lab.locate("edit:cancel") != Vector2(-1, -1):
			_click("edit:cancel")
	_drag("port:timer1", "item:monitor1")
	_drag("port:monitor1", "item:alarm1")
	_drag("port:monitor1", "item:burner")
	_click("edit:set")
	_click("item:timer1")
	var at := _lab.locate("item:monitor1")
	_lab.pointer(at.x, at.y, true, 2) # with Shift (CG_POINTER_SHIFT): added to the selection
	_lab.pointer(at.x, at.y, false, 2)
	await _shoot(dir, "7_chosen")
	_click("circuit:create")
	await _shoot(dir, "8_circuit")
	_click("part:open")
	await _shoot(dir, "9_circuit_open")
	_click("circuit:close")
	# Stage 16: a writer, its words being typed (GAME_SPEC §13.4)
	_drag("drawer:writer", "square:2,4")
	_click("edit:level:warning")
	_click("edit:says")
	for c in "the flask is nearly dry":
		_lab.key(0, c.unicode_at(0))
	await _shoot(dir, "11_writer")
	_click("edit:cancel")
	# Stage 17: a goal of the player's, in the editor (GAME_SPEC §13.5)
	_click("goal:4")
	_click("edit:name")
	for c in "blue liquor":
		_lab.key(0, c.unicode_at(0))
	_click("edit:term:0:what")
	_click("edit:pick:copper_sulfate_aq")
	_click("edit:term:add")
	_click("edit:term:1:measure")
	await _shoot(dir, "12_goal_editor")
