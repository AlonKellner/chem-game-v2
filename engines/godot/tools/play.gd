extends SceneTree
# A play timed (make pass-cost): the host given a "chem-play/1" (lab/play.hpp), each of its marks' frames timed,
# then frames in which every pixel's history starts again. Bounded: the play's own frames, and a limit besides.
#   godot --path engines/godot -s tools/play.gd -- <play.json>
func _initialize() -> void:
	var view := SubViewport.new()
	view.size = Vector2i(1920, 1080)
	view.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(view)
	var lab := ChemLab.new()
	lab.tutorial_path = "" # no tutorial: every gesture is taken
	view.add_child(lab)
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	await process_frame
	if not lab.play(FileAccess.get_file_as_string(OS.get_cmdline_user_args()[0])):
		push_error("play.gd: the play was not read")
	for i in 6000:
		if not lab.playing(): break
		await process_frame
	for row in lab.played():
		print("%-8s %4d frames: a frame %6.2f ms (the worst %6.2f)" % row)
	lab.set_optics_restarting(true) # the worst frame: every pixel's history starts again
	for i in 13:
		if i == 3: view.set_meta("t", Time.get_ticks_usec())
		await process_frame
	print("restart    10 frames: a frame %6.2f ms" % (float(Time.get_ticks_usec() - view.get_meta("t")) / 10000.0))
	print("play.gd: done, at step ", JSON.parse_string(lab.report())["step"])
	quit(0)
