@tool
extends EditorPlugin

const TileMapExtractor = preload("res://addons/spx_tilemap_exporter/tilemap_extractor.gd")

var _extractor: TileMapExtractor
var _file_dialog: EditorFileDialog


func _enter_tree() -> void:
	_extractor = TileMapExtractor.new()
	add_tool_menu_item("SPX Export TileMap...", _on_export_tilemap_pressed)


func _exit_tree() -> void:
	remove_tool_menu_item("SPX Export TileMap...")
	if _file_dialog:
		_file_dialog.queue_free()
		_file_dialog = null
	if _extractor:
		_extractor = null


func _on_export_tilemap_pressed() -> void:
	# Get current edited scene root
	var edited_scene = get_editor_interface().get_edited_scene_root()
	if not edited_scene:
		_show_error("No scene is currently open.")
		return
	
	# Find all TileMapLayer nodes in the scene (including from TileMap nodes)
	var layers = _find_tilemap_layers(edited_scene)
	if layers.is_empty():
		_show_error("No TileMapLayer or TileMap nodes found in the current scene.")
		return
	
	# Check if any layer has a TileSet
	var has_tileset = false
	for layer in layers:
		if layer.tile_set:
			has_tileset = true
			break
	
	if not has_tileset:
		_show_error("No TileSet found in TileMapLayer nodes.")
		return
	
	# Show file dialog to select export directory
	_show_export_dialog(edited_scene, layers)


func _find_tilemap_layers(node: Node) -> Array[TileMapLayer]:
	var layers: Array[TileMapLayer] = []
	_find_tilemap_layers_recursive(node, layers)
	return layers


func _find_tilemap_layers_recursive(node: Node, layers: Array[TileMapLayer]) -> void:
	if node is TileMapLayer:
		layers.append(node)
	elif node is TileMap:
		# TileMap node contains TileMapLayer as internal children
		# Use get_children(true) to include internal nodes
		for child in node.get_children(true):
			if child is TileMapLayer:
				layers.append(child)
	
	# Continue searching in regular children
	for child in node.get_children():
		_find_tilemap_layers_recursive(child, layers)


func _show_export_dialog(scene_root: Node, layers: Array[TileMapLayer]) -> void:
	if _file_dialog:
		_file_dialog.queue_free()
	
	_file_dialog = EditorFileDialog.new()
	_file_dialog.file_mode = EditorFileDialog.FILE_MODE_SAVE_FILE
	_file_dialog.access = EditorFileDialog.ACCESS_FILESYSTEM
	_file_dialog.add_filter("*.json", "SPX TileMap JSON")
	
	# Default filename based on scene name
	var scene_name = scene_root.scene_file_path.get_file().get_basename()
	if scene_name.is_empty():
		scene_name = scene_root.name
	_file_dialog.current_file = scene_name + ".json"
	
	_file_dialog.file_selected.connect(_on_export_path_selected.bind(scene_root, layers))
	
	get_editor_interface().get_base_control().add_child(_file_dialog)
	_file_dialog.popup_centered_ratio(0.6)


func _on_export_path_selected(path: String, scene_root: Node, layers: Array[TileMapLayer]) -> void:
	var result = _extractor.export_tilemap(layers, path)
	
	if result.success:
		_show_info("Export successful!\n\nExported to: %s\nLayers: %d\nTextures copied: %d" % [
			path,
			result.layer_count,
			result.texture_count
		])
	else:
		_show_error("Export failed:\n" + result.error)


func _show_error(message: String) -> void:
	var dialog = AcceptDialog.new()
	dialog.title = "SPX TileMap Export Error"
	dialog.dialog_text = message
	dialog.confirmed.connect(dialog.queue_free)
	dialog.canceled.connect(dialog.queue_free)
	get_editor_interface().get_base_control().add_child(dialog)
	dialog.popup_centered()


func _show_info(message: String) -> void:
	var dialog = AcceptDialog.new()
	dialog.title = "SPX TileMap Export"
	dialog.dialog_text = message
	dialog.confirmed.connect(dialog.queue_free)
	dialog.canceled.connect(dialog.queue_free)
	get_editor_interface().get_base_control().add_child(dialog)
	dialog.popup_centered()
