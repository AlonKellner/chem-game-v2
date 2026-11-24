extends GutTest

# The Godot host (native/hosts/godot: ChemLab, the game on the C API alone): it
# loads a bench, forwards the pointer, advances frames and paints the draw list.
# The game and its screen are tested natively (make check); this is the wiring.

var _lab: ChemLab

func before_each() -> void:
	_lab = ChemLab.new()
	_lab.tutorial_path = "" # the wiring alone: a tutorial holds the clock while it asks for a gesture
	add_child_autofree(_lab)
	await wait_process_frames(2)

func _item(id: String) -> Dictionary:
	var report: Dictionary = JSON.parse_string(_lab.report())
	for item in report["items"]:
		if item["id"] == id:
			return item
	return {}

func _drag(what: String, to: String) -> void:
	var a := _lab.locate(what)
	var b := _lab.locate(to)
	assert_ne(a, Vector2(-1, -1), what + " is on the screen")
	assert_ne(b, Vector2(-1, -1), to + " is on the screen")
	_lab.pointer(a.x, a.y, true)
	if what.begins_with("item:"): # carried (Stage 27): a click picks it up, and a click puts it down
		_lab.pointer(a.x, a.y, false)
		b = _lab.locate(to)
	else:
		_lab.pointer((a.x + b.x) / 2, (a.y + b.y) / 2, true)
	_lab.pointer(b.x, b.y, true)
	_lab.pointer(b.x, b.y, false)

func test_the_extension_registers_the_host_node() -> void:
	assert_true(ClassDB.class_exists("ChemLab"))
	assert_true(_lab.build_name().begins_with("chem-game-v2 "))

func test_the_main_scene_is_the_host() -> void:
	var scene: PackedScene = load("res://scenes/main.tscn")
	var node := scene.instantiate()
	add_child_autofree(node)
	assert_true(node is ChemLab)

func test_it_opens_the_bench_with_everything_in_the_cabinet() -> void:
	assert_eq(_item("flask")["place"], "cabinet")
	assert_eq(_item("burner")["place"], "cabinet")
	assert_eq(_lab.message(), "")

func test_dragging_puts_equipment_on_the_bench_and_together() -> void:
	_drag("item:flask", "square:11,12")
	assert_eq(_item("flask")["place"], "bench")
	_drag("item:burner", "item:flask")
	assert_eq(_item("burner")["under"], "flask")
	_drag("item:balloon", "item:flask")
	assert_eq(_item("balloon")["on"], "flask")
	_drag("item:condenser", "item:flask")
	assert_string_contains(_lab.message(), "taken")

func test_it_opens_with_the_tutorial_and_follows_it() -> void:
	assert_eq(_lab.tutorial(), PackedInt32Array([-1, 0, 0, 1]), "none when its path is empty; strict as opened")
	_lab = ChemLab.new()
	add_child_autofree(_lab)
	await wait_process_frames(2)
	assert_eq(_lab.tutorial_path, "res://data/tutorials/demo.json")
	var t := _lab.tutorial()
	assert_eq(t.size(), 4)
	assert_eq(t[3], 1, "strict")
	assert_eq(t[0], 0, "running")
	assert_eq(t[1], 0, "at its first step")
	assert_gt(t[2], 20)
	_lab.frame(1.0)
	assert_eq(int(JSON.parse_string(_lab.report())["step"]), 0, "the clock waits while it asks for a gesture")
	_drag("item:burner", "square:11,12")
	assert_string_contains(_lab.message(), "Not now", "anything but the step's gesture is refused")
	_drag("item:flask", "square:11,12")
	assert_eq(_lab.tutorial()[1], 1, "the flask on the bench passes the first step")

func test_it_lists_the_experiments_and_a_free_tutorial_takes_any_gesture() -> void:
	_lab = ChemLab.new()
	add_child_autofree(_lab)
	await wait_process_frames(2)
	assert_eq(_lab.experiments_path, "res://data/experiments.json")
	assert_eq(_lab.experiment(), "demo", "the demo, as opened, is the catalogue's first")
	_lab.set_strict(false)
	assert_eq(_lab.tutorial()[3], 0, "free")
	_drag("item:burner", "square:19,12")
	assert_eq(_lab.message(), "", "free: a gesture the step does not ask for is taken")
	assert_eq(_item("burner")["place"], "bench")
	assert_true(_lab.choose("vacuum"), "an experiment opened by its id")
	assert_eq(_lab.experiment(), "vacuum")
	assert_eq(_item("flask")["place"], "bench", "the vacuum's bench")
	assert_eq(_lab.tutorial()[3], 0, "the mode stays")
	assert_false(_lab.choose("kettle"))

func _click(what: String) -> void:
	var a := _lab.locate(what)
	assert_ne(a, Vector2(-1, -1), what + " is on the screen")
	_lab.pointer(a.x, a.y, false)
	_lab.pointer(a.x, a.y, true)
	_lab.pointer(a.x, a.y, false)

func test_a_timer_typed_in_rings_an_alarm_and_pauses_the_clock() -> void:
	_drag("drawer:timer", "square:1,1")
	_click("edit:value")
	# the engine's own key events reach the editor's field
	for c in "3":
		var press := InputEventKey.new()
		press.pressed = true
		press.unicode = c.unicode_at(0)
		get_viewport().push_input(press, true)
	var enter := InputEventKey.new()
	enter.pressed = true
	enter.keycode = KEY_ENTER
	get_viewport().push_input(enter, true)
	await wait_process_frames(2)
	_drag("drawer:alarm", "square:3,1")
	_drag("port:timer1", "item:alarm1")
	assert_eq(_lab.signals().size(), 2, "a timer and an alarm")
	assert_false(_lab.paused(), "the clock plays on once the timer is set")
	_lab.frame(3.5)
	var alarm: Dictionary = _lab.signals()[1]
	assert_true(alarm["ringing"], "the alarm rang when the timer reached zero")
	assert_true(_lab.paused(), "the clock stopped when it rang")

func test_frames_advance_the_clock_and_keep_every_total() -> void:
	var before: int = JSON.parse_string(_lab.report())["step"]
	_lab.frame(1.0)
	var report: Dictionary = JSON.parse_string(_lab.report())
	# 60 steps, and the step the engine's own earlier frames may have left owing (a fraction carried just under one)
	assert_between(int(report["step"]) - before, 60, 61)
	assert_true(report["conserved"]["unchanged"])
	await wait_process_frames(3) # the engine's own frames advance it too, and paint it
	assert_gt(int(JSON.parse_string(_lab.report())["step"]), int(report["step"]))

func test_the_engines_mouse_reaches_the_screen() -> void:
	var at := _lab.locate("pause")
	var press := InputEventMouseButton.new()
	press.button_index = MOUSE_BUTTON_LEFT
	press.pressed = true
	press.position = at
	press.global_position = at
	# in the viewport's own coordinates: a headless window has no size to scale them by
	get_viewport().push_input(press, true)
	var release := press.duplicate()
	release.pressed = false
	get_viewport().push_input(release, true)
	await wait_process_frames(2)
	var paused_at: int = JSON.parse_string(_lab.report())["step"]
	await wait_process_frames(5)
	assert_eq(int(JSON.parse_string(_lab.report())["step"]), paused_at, "paused: the clock stands")

func test_the_wheel_a_pinch_and_a_key_reach_the_rooms_view() -> void:
	# (Stage 42: the wheel zooms the room about the pointer, a pinch too; Home sees it whole again)
	var at := _lab.locate("square:10,6")
	var wheel := InputEventMouseButton.new()
	wheel.button_index = MOUSE_BUTTON_WHEEL_UP
	wheel.pressed = true
	wheel.factor = 1.0
	wheel.position = at
	wheel.global_position = at
	get_viewport().push_input(wheel, true)
	assert_almost_eq(_lab.get_view_scale(), 1.25, 0.001, "a notch of the wheel: a quarter closer")
	var pinch := InputEventMagnifyGesture.new()
	pinch.factor = 2.0
	pinch.position = at
	get_viewport().push_input(pinch, true)
	assert_almost_eq(_lab.get_view_scale(), 2.5, 0.001, "a pinch")
	var home := InputEventKey.new()
	home.pressed = true
	home.keycode = KEY_HOME
	get_viewport().push_input(home, true)
	assert_eq(_lab.get_view_scale(), 1.0, "the whole room again")

func test_the_optical_pass_lies_between_the_two_flat_layers() -> void:
	# (GAME_SPEC §12.2: a compute shader over the whole screen, which needs a GPU: headless it is
	# off and says why; its settled picture against the truth: make screenshots)
	assert_eq(_lab.get_child_count(), 2)
	var optics := _lab.get_child(0) as TextureRect
	assert_not_null(optics, "the optical pass is the first child")
	assert_eq(optics.size, Vector2(1920, 1080))
	if _lab.optics_running():
		assert_not_null(optics.texture, "it shows what the pass computes")
	else:
		assert_ne(_lab.optics_why(), "", "off, and it says why")
	assert_eq(_lab.optics_budget, 16.0, "at most 16 points a pixel a frame, by default")
	_lab.optics_budget = 4.0
	assert_eq(_lab.optics_budget, 4.0, "a setting: accuracy for speed")
	assert_eq(_lab.optics_parity(PackedByteArray()).size(), 0, "a picture of the wrong size is not compared")

func test_the_debug_panels_export_is_written_to_user_saves() -> void:
	for what in ["debug", "debug:export"]:
		var at := _lab.locate(what)
		assert_ne(at, Vector2(-1, -1), what + " is on the screen")
		_lab.pointer(at.x, at.y, true)
		_lab.pointer(at.x, at.y, false)
	_lab.frame(0.0)
	var path := _lab.exported()
	assert_string_starts_with(path, "user://saves/")
	var saved: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(path))
	assert_eq(saved["format"], "chem-save/1")
	DirAccess.remove_absolute(path)
