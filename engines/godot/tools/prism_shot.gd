extends SceneTree

# Pictures of the prism demo (make prism-shots -> build/screenshots/prism_*.png): as it opens, turned a
# little, and moved. Every wait is a fixed number of frames: a script on the engine's thread never waits
# for something that may not come.
#
#   godot --path engines/godot -s tools/prism_shot.gd -- <output directory>

var _demo: ChemOpticsDemo
var _view: SubViewport

func _settle(frames: int) -> void:
	for i in frames:
		await process_frame

func _shoot(dir: String, name: String) -> void:
	await _settle(60)
	if DisplayServer.get_name() == "headless":
		return
	_view.get_texture().get_image().save_png(dir.path_join("prism_" + name + ".png"))

func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	var dir: String = args[0] if args.size() > 0 else "user://"
	DirAccess.make_dir_recursive_absolute(dir)
	_view = SubViewport.new()
	_view.size = Vector2i(1920, 1080)
	_view.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(_view)
	_demo = ChemOpticsDemo.new()
	_demo.demo = "prism"
	_view.add_child(_demo)
	var shown := TextureRect.new()
	shown.texture = _view.get_texture()
	shown.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	shown.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.add_child(shown)
	await process_frame
	print("prism_shot.gd: the optical pass ", "runs" if _demo.optics_running() else "is off: " + _demo.optics_why())
	await _shoot(dir, "1_open")
	_demo.turn(0.06)
	await _shoot(dir, "2_turned")
	_demo.turn(-0.06)
	# dragged 40 pixels to the left from its middle
	_demo.pointer(1030, 540, true)
	_demo.pointer(990, 540, true)
	_demo.pointer(990, 540, false)
	await _shoot(dir, "3_moved")
	print("prism_shot.gd: done")
	quit(0)
