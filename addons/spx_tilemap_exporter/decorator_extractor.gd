@tool
extends RefCounted
## Decorator data extractor for SPX JSON format export
##
## This class extracts Sprite2D nodes and prefab instances from Godot scenes
## and exports them to a JSON format compatible with SPX runtime loading.
## 
## Coordinate system conversion:
## - Godot: Y-axis points down, rotation in radians
## - SPX: Y-axis points up, rotation in degrees with +90 offset

class_name DecoratorExtractor

## Export result structure
class ExportResult:
	var success: bool = false
	var error: String = ""
	var decorator_count: int = 0
	var texture_count: int = 0


## Decorator data structure matching tscn_parser's DecoratorNode
class DecoratorData:
	var name: String = ""
	var path: String = ""  # Texture path relative to export directory
	var position: Vector2 = Vector2.ZERO
	var scale: Vector2 = Vector2.ONE
	var rotation: float = 0.0  # In degrees, already converted for SPX
	var z_index: int = 0
	var pivot: Vector2 = Vector2.ZERO
	var collider_type: String = "none"  # none, auto, circle, rect, capsule, polygon
	var collider_pivot: Vector2 = Vector2.ZERO
	var collider_params: Array = []
	var texture_size: Vector2 = Vector2.ZERO
	
	func to_dict() -> Dictionary:
		var dict: Dictionary = {
			"name": name,
			"path": path,
			"position": {"x": position.x, "y": position.y},
			"scale": {"x": scale.x, "y": scale.y},
			"rotation": rotation,
			"z_index": z_index,
			"pivot": {"x": pivot.x, "y": pivot.y},
			"texture_size": {"x": texture_size.x, "y": texture_size.y}
		}
		
		if collider_type != "none":
			dict["collider_type"] = collider_type
			dict["collider_pivot"] = {"x": collider_pivot.x, "y": collider_pivot.y}
			dict["collider_params"] = collider_params
		
		return dict


# ============================================================================
# Constants
# ============================================================================

## Groups or name patterns to exclude from export
var exclude_patterns: Array[String] = ["_ignore", "_skip"]


# ============================================================================
# Main Export Function
# ============================================================================

## Main export function
## textures_subdir: subdirectory name for textures (e.g., "decorator" for _export/scene/decorator/)
func export_decorators(scene_root: Node, export_path: String, node_offset: Vector2 = Vector2.ZERO, textures_subdir: String = "textures") -> ExportResult:
	var result = ExportResult.new()
	
	if scene_root == null:
		result.error = "Scene root is null"
		return result
	
	# Get export directory
	var export_dir = export_path.get_base_dir()
	
	# Collect all decorators from scene
	var decorators: Array[DecoratorData] = []
	_collect_decorators_recursive(scene_root, decorators, node_offset, export_dir, textures_subdir, result)
	
	if decorators.is_empty():
		result.error = "No decorators found in scene"
		return result
	
	# Build the JSON data structure
	var json_data: Dictionary = {
		"version": 1,
		"decorators": []
	}
	
	for decorator in decorators:
		json_data["decorators"].append(decorator.to_dict())
	
	# Write JSON file
	var json_string = JSON.stringify(json_data, "\t")
	var file = FileAccess.open(export_path, FileAccess.WRITE)
	if not file:
		result.error = "Failed to create file: " + export_path
		return result
	
	file.store_string(json_string)
	file.close()
	
	result.success = true
	result.decorator_count = decorators.size()
	return result


# ============================================================================
# Node Collection
# ============================================================================

## Recursively collect decorator nodes from scene tree
func _collect_decorators_recursive(node: Node, decorators: Array[DecoratorData], node_offset: Vector2, export_dir: String, textures_subdir: String, result: ExportResult) -> void:
	# Skip excluded nodes
	if _should_exclude(node):
		return
	
	# Check if this is a Sprite2D node
	if node is Sprite2D:
		var decorator = _extract_sprite2d(node as Sprite2D, node_offset, export_dir, textures_subdir, result)
		if decorator != null:
			decorators.append(decorator)
	
	# Check if this is a prefab instance (has scene_file_path)
	elif _is_prefab_instance(node):
		var decorator = _extract_prefab_instance(node, node_offset, export_dir, textures_subdir, result)
		if decorator != null:
			decorators.append(decorator)
	
	# Recursively process children
	for child in node.get_children():
		_collect_decorators_recursive(child, decorators, node_offset, export_dir, textures_subdir, result)


## Check if node should be excluded from export
func _should_exclude(node: Node) -> bool:
	# Skip if node is in "ignore" group
	if node.is_in_group("spx_ignore"):
		return true
	
	# Skip TileMap related nodes (they are exported separately)
	if node is TileMapLayer or node is TileMap:
		return true
	
	# Skip based on name patterns
	for pattern in exclude_patterns:
		if node.name.contains(pattern):
			return true
	
	return false


## Check if node is a prefab instance (instantiated PackedScene)
func _is_prefab_instance(node: Node) -> bool:
	# A prefab instance has a scene_file_path set
	if node.scene_file_path.is_empty():
		return false
	
	# Must be a Node2D or derived
	if not node is Node2D:
		return false
	
	# Must have a Sprite2D child or be a visual node
	return _has_sprite_child(node)


## Check if node has any Sprite2D descendants
func _has_sprite_child(node: Node) -> bool:
	for child in node.get_children():
		if child is Sprite2D:
			return true
		if _has_sprite_child(child):
			return true
	return false


# ============================================================================
# Sprite2D Extraction
# ============================================================================

## Extract decorator data from a Sprite2D node
func _extract_sprite2d(sprite: Sprite2D, node_offset: Vector2, export_dir: String, textures_subdir: String, result: ExportResult) -> DecoratorData:
	if sprite.texture == null:
		return null
	
	var decorator = DecoratorData.new()
	decorator.name = sprite.name
	
	# Get global transform for correct world position
	var global_pos = sprite.global_position
	
	# Apply coordinate conversion: Y-axis flip
	decorator.position = Vector2(
		global_pos.x + node_offset.x,
		-(global_pos.y + node_offset.y)  # Flip Y axis
	)
	
	# Get scale from global transform
	decorator.scale = sprite.global_scale
	
	# Convert rotation: radians to degrees + 90 offset for SPX
	decorator.rotation = rad_to_deg(sprite.global_rotation) + 90.0
	
	# Z-index
	decorator.z_index = sprite.z_index
	
	# Get texture info
	var texture = sprite.texture
	decorator.texture_size = texture.get_size()
	
	# Copy texture and get relative path
	decorator.path = _copy_texture(texture, export_dir, textures_subdir, result)
	
	# Calculate pivot based on sprite offset
	# Godot pivot is from center by default, SPX expects offset from center
	var sprite_offset = sprite.offset
	var pivot = Vector2(-sprite_offset.x, sprite_offset.y)  # Flip Y
	pivot = pivot + decorator.texture_size / 2.0  # Convert to center-based
	decorator.pivot = pivot
	
	# Extract collision data from sibling or child collision shapes
	_extract_collision_data(sprite, decorator)
	
	return decorator


# ============================================================================
# Prefab Instance Extraction
# ============================================================================

## Extract decorator data from a prefab instance
func _extract_prefab_instance(node: Node, node_offset: Vector2, export_dir: String, textures_subdir: String, result: ExportResult) -> DecoratorData:
	var node2d = node as Node2D
	if node2d == null:
		return null
	
	# Find the first Sprite2D child to get texture info
	var sprite = _find_first_sprite(node)
	if sprite == null or sprite.texture == null:
		return null
	
	var decorator = DecoratorData.new()
	decorator.name = node.name
	
	# Get global transform
	var global_pos = node2d.global_position
	
	# Apply coordinate conversion
	decorator.position = Vector2(
		global_pos.x + node_offset.x,
		-(global_pos.y + node_offset.y)
	)
	
	decorator.scale = node2d.global_scale
	decorator.rotation = rad_to_deg(node2d.global_rotation) + 90.0
	decorator.z_index = node2d.z_index
	
	# Get texture from child sprite
	var texture = sprite.texture
	decorator.texture_size = texture.get_size()
	decorator.path = _copy_texture(texture, export_dir, textures_subdir, result)
	
	# Calculate pivot from sprite's local position and offset
	var sprite_local_pos = sprite.position
	var sprite_offset = sprite.offset
	var pivot = Vector2(
		-sprite_local_pos.x - sprite_offset.x,
		sprite_local_pos.y + sprite_offset.y  # Y already flipped
	)
	pivot = pivot + decorator.texture_size / 2.0
	decorator.pivot = pivot
	
	# Get sprite's local z_index if set
	if sprite.z_index != 0:
		decorator.z_index = sprite.z_index
	
	# Extract collision from prefab
	_extract_collision_from_node(node, decorator)
	
	return decorator


## Find the first Sprite2D in node hierarchy
func _find_first_sprite(node: Node) -> Sprite2D:
	for child in node.get_children():
		if child is Sprite2D:
			return child as Sprite2D
	
	# Search deeper
	for child in node.get_children():
		var found = _find_first_sprite(child)
		if found != null:
			return found
	
	return null


# ============================================================================
# Collision Extraction
# ============================================================================

## Extract collision data from a Sprite2D's related collision shapes
func _extract_collision_data(sprite: Sprite2D, decorator: DecoratorData) -> void:
	var parent = sprite.get_parent()
	if parent == null:
		return
	
	# Look for collision shapes in parent's children (siblings of sprite)
	for sibling in parent.get_children():
		if _try_extract_collision(sibling, sprite.global_position, decorator):
			return
	
	# Look for collision shapes in sprite's own children
	for child in sprite.get_children():
		if _try_extract_collision(child, sprite.global_position, decorator):
			return


## Extract collision from any node in hierarchy
func _extract_collision_from_node(node: Node, decorator: DecoratorData) -> void:
	# Search for CollisionShape2D or CollisionPolygon2D in all descendants
	var collision_node = _find_collision_node(node)
	if collision_node == null:
		return
	
	var reference_pos = (node as Node2D).global_position if node is Node2D else Vector2.ZERO
	_try_extract_collision(collision_node, reference_pos, decorator)


## Find first collision node in hierarchy
func _find_collision_node(node: Node) -> Node:
	for child in node.get_children():
		if child is CollisionShape2D or child is CollisionPolygon2D:
			return child
		var found = _find_collision_node(child)
		if found != null:
			return found
	return null


## Try to extract collision data from a node
func _try_extract_collision(node: Node, reference_pos: Vector2, decorator: DecoratorData) -> bool:
	if node is CollisionShape2D:
		return _extract_from_collision_shape(node as CollisionShape2D, reference_pos, decorator)
	elif node is CollisionPolygon2D:
		return _extract_from_collision_polygon(node as CollisionPolygon2D, reference_pos, decorator)
	return false


## Extract collision from CollisionShape2D
func _extract_from_collision_shape(collision: CollisionShape2D, reference_pos: Vector2, decorator: DecoratorData) -> bool:
	var shape = collision.shape
	if shape == null:
		return false
	
	# Calculate collider pivot relative to sprite center
	var collision_global_pos = collision.global_position
	var relative_pos = collision_global_pos - reference_pos
	
	# Convert to SPX coordinate system
	var collider_pivot = Vector2(relative_pos.x, -relative_pos.y)
	
	if shape is RectangleShape2D:
		var rect_shape = shape as RectangleShape2D
		decorator.collider_type = "rect"
		decorator.collider_pivot = collider_pivot
		decorator.collider_params = [rect_shape.size.x, rect_shape.size.y]
		return true
	
	elif shape is CircleShape2D:
		var circle_shape = shape as CircleShape2D
		decorator.collider_type = "circle"
		decorator.collider_pivot = collider_pivot
		decorator.collider_params = [circle_shape.radius]
		return true
	
	elif shape is CapsuleShape2D:
		var capsule_shape = shape as CapsuleShape2D
		decorator.collider_type = "capsule"
		decorator.collider_pivot = collider_pivot
		decorator.collider_params = [capsule_shape.radius, capsule_shape.height]
		return true
	
	elif shape is ConvexPolygonShape2D:
		var poly_shape = shape as ConvexPolygonShape2D
		decorator.collider_type = "polygon"
		decorator.collider_pivot = collider_pivot
		var params: Array = []
		for point in poly_shape.points:
			params.append(point.x)
			params.append(-point.y)  # Flip Y
		decorator.collider_params = params
		return true
	
	return false


## Extract collision from CollisionPolygon2D
func _extract_from_collision_polygon(collision: CollisionPolygon2D, reference_pos: Vector2, decorator: DecoratorData) -> bool:
	var polygon = collision.polygon
	if polygon.is_empty():
		return false
	
	# Calculate collider pivot
	var collision_global_pos = collision.global_position
	var relative_pos = collision_global_pos - reference_pos
	var collider_pivot = Vector2(relative_pos.x, -relative_pos.y)
	
	decorator.collider_type = "polygon"
	decorator.collider_pivot = collider_pivot
	
	var params: Array = []
	for point in polygon:
		params.append(point.x)
		params.append(-point.y)  # Flip Y
	decorator.collider_params = params
	
	return true


# ============================================================================
# Texture Handling
# ============================================================================

## Copy texture to export directory and return relative path
func _copy_texture(texture: Texture2D, export_dir: String, textures_subdir: String, result: ExportResult) -> String:
	var texture_path = texture.resource_path
	if texture_path.is_empty():
		push_warning("Texture has no resource path, skipping copy")
		return ""
	
	var filename = texture_path.get_file()
	
	# Create textures subdirectory
	var textures_dir = export_dir.path_join(textures_subdir)
	if not DirAccess.dir_exists_absolute(textures_dir):
		var err = DirAccess.make_dir_recursive_absolute(textures_dir)
		if err != OK:
			push_warning("Failed to create textures directory: %s (error: %d)" % [textures_dir, err])
			return ""
	
	var dest_path = textures_dir.path_join(filename)
	
	# Get the global path for the source texture
	var source_global_path = ProjectSettings.globalize_path(texture_path)
	
	# Check if file already exists at destination
	if FileAccess.file_exists(dest_path):
		return textures_subdir.path_join(filename)
	
	# Copy the file
	var err = DirAccess.copy_absolute(source_global_path, dest_path)
	if err != OK:
		push_warning("Failed to copy texture: %s -> %s (error: %d)" % [source_global_path, dest_path, err])
		return textures_subdir.path_join(filename)
	
	result.texture_count += 1
	return textures_subdir.path_join(filename)


# ============================================================================
# Coordinate Transform Utilities
# ============================================================================

## Apply final pivot adjustments for SPX
## Called after all data is collected
func _finalize_pivot(decorator: DecoratorData) -> void:
	# Final pivot calculation for SPX:
	# pivot = pivot + texture_size / 2 (already done)
	# collider_pivot = collider_pivot - pivot (relative to sprite anchor)
	decorator.collider_pivot = decorator.collider_pivot - decorator.pivot
