extends GutTest

# The Godot host (native/hosts/godot: ChemLab, the game on the C API alone): it
# loads a bench, forwards the pointer, advances frames and paints the draw list.
# The game and its screen are tested natively (make check); this is the wiring.

var _lab: ChemLab

func before_each() -> void:
	_lab = ChemLab.new()
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
	_lab.pointer(a.x, a.y, false)
	_lab.pointer(a.x, a.y, true)
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
	_drag("item:flask", "cell:3,1")
	assert_eq(_item("flask")["place"], "bench")
	_drag("item:burner", "item:flask")
	assert_eq(_item("burner")["under"], "flask")
	_drag("item:balloon", "item:flask")
	assert_eq(_item("balloon")["on"], "flask")
	_drag("item:condenser", "item:flask")
	assert_string_contains(_lab.message(), "taken")

func test_frames_advance_the_clock_and_keep_every_total() -> void:
	var before: int = JSON.parse_string(_lab.report())["step"]
	_lab.frame(1.0)
	var report: Dictionary = JSON.parse_string(_lab.report())
	assert_eq(int(report["step"]) - before, 60)
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
