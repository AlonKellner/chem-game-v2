extends GutTest

# The Godot host (native/hosts/godot: ChemLab, the game on the C API alone).
# Stage 0: the extension loads, the node names the build, and the library's
# reports reach Godot's error log.

func test_the_extension_registers_the_host_node() -> void:
	assert_true(ClassDB.class_exists("ChemLab"))

func test_the_host_names_the_build() -> void:
	var lab := ChemLab.new()
	add_child_autofree(lab)
	assert_true(lab.build_name().begins_with("chem-game-v2 "))

func test_the_main_scene_is_the_host() -> void:
	var scene: PackedScene = load("res://scenes/main.tscn")
	var node := scene.instantiate()
	add_child_autofree(node)
	assert_true(node is ChemLab)
