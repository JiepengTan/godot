@tool
extends SceneTree
## Command-line export script for SPX TileMap data
##
## Usage: godot --headless --path <project_path> -s export_cli.gd
##
## Configuration is done via constants below:

const TileMapExtractor = preload("res://addons/spx_tilemap_exporter/tilemap_extractor.gd")

# ============================================================================
# Configuration - modify these as needed
# ============================================================================
const SCENE_PATH = "res://main.tscn"
const OUTPUT_PATH = "res://export_spx/main.json"
# ============================================================================

func _init() -> void:
	print("SPX TileMap Export CLI")
	print("======================")
	print("Scene:  ", SCENE_PATH)
	print("Output: ", OUTPUT_PATH)
	print("")
	
	# Load the scene
	var packed_scene = load(SCENE_PATH)
	if not packed_scene:
		printerr("ERROR: Failed to load scene: ", SCENE_PATH)
		quit(1)
		return
	
	if not packed_scene is PackedScene:
		printerr("ERROR: Loaded resource is not a PackedScene: ", SCENE_PATH)
		quit(1)
		return
	
	var scene_root = packed_scene.instantiate()
	if not scene_root:
		printerr("ERROR: Failed to instantiate scene")
		quit(1)
		return
	
	# Find all TileMapLayer nodes
	var layers = _find_tilemap_layers(scene_root)
	if layers.is_empty():
		printerr("ERROR: No TileMapLayer or TileMap nodes found in the scene")
		scene_root.queue_free()
		quit(1)
		return
	
	print("Found ", layers.size(), " TileMapLayer(s)")
	
	# Check if any layer has a TileSet
	var has_tileset = false
	for layer in layers:
		if layer.tile_set:
			has_tileset = true
			break
	
	if not has_tileset:
		printerr("ERROR: No TileSet found in TileMapLayer nodes")
		scene_root.queue_free()
		quit(1)
		return
	
	# Ensure export directory exists
	var export_dir = OUTPUT_PATH.get_base_dir()
	var global_export_dir = ProjectSettings.globalize_path(export_dir)
	if not DirAccess.dir_exists_absolute(global_export_dir):
		var err = DirAccess.make_dir_recursive_absolute(global_export_dir)
		if err != OK:
			printerr("ERROR: Failed to create export directory: ", global_export_dir)
			scene_root.queue_free()
			quit(1)
			return
	
	# Export using TileMapExtractor
	var extractor = TileMapExtractor.new()
	var global_output_path = ProjectSettings.globalize_path(OUTPUT_PATH)
	var result = extractor.export_tilemap(layers, global_output_path)
	
	if result.success:
		print("")
		print("Export successful!")
		print("  Output: ", global_output_path)
		print("  Layers: ", result.layer_count)
		print("  Textures: ", result.texture_count)
		scene_root.queue_free()
		quit(0)
	else:
		printerr("ERROR: Export failed: ", result.error)
		scene_root.queue_free()
		quit(1)


func _find_tilemap_layers(node: Node) -> Array[TileMapLayer]:
	var layers: Array[TileMapLayer] = []
	_find_tilemap_layers_recursive(node, layers)
	return layers


func _find_tilemap_layers_recursive(node: Node, layers: Array[TileMapLayer]) -> void:
	if node is TileMapLayer:
		layers.append(node)
	elif node is TileMap:
		# TileMap node contains TileMapLayer as internal children
		for child in node.get_children(true):
			if child is TileMapLayer:
				layers.append(child)
	
	# Continue searching in regular children
	for child in node.get_children():
		_find_tilemap_layers_recursive(child, layers)
