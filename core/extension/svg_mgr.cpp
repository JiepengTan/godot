#include "svg_mgr.h"

#include "core/io/image_loader.h"
#include "spx_sprite.h"
#include "spx_engine.h"
#include "spx_camera_mgr.h"

SvgManager *SvgManager::singleton = nullptr;

SvgManager *SvgManager::get_singleton() {
	if (!singleton) {
		singleton = memnew(SvgManager);
	}
	return singleton;
}

SvgManager::SvgManager() {
	singleton = this;
}

SvgManager::~SvgManager() {
	svg_registry.clear();
	singleton = nullptr;
}

float SvgManager::SvgInfo::get_max_required_scale() const {
	float max_scale = 1.0f;
	
	for (SpxSprite* sprite : references) {
		if (sprite) {
			Vector2 actual_scale = sprite->get_actual_render_scale();
			float sprite_max = MAX(actual_scale.x, actual_scale.y);
			max_scale = MAX(max_scale, sprite_max);
		}
	}
	
	return max_scale;
}

bool SvgManager::is_svg_file(const String& path) const {
	return path.to_lower().ends_with(".svg");
}

float SvgManager::calculate_optimal_scale_level(float required_scale) const {
	if (required_scale <= 1.0f) {
		return 1.0f;
	}
	
	// Use powers of 2 as scaling levels: 1, 2, 4, 8, 16, ...
	float level = 1.0f;
	while (level < required_scale && level < max_scale_level) {
		level *= 2.0f;
	}
	
	float result = MIN(level, (float)max_scale_level);
	return result;
}

Ref<ImageTexture> SvgManager::get_or_create_svg_texture(const String& svg_path) {
	if (!is_svg_file(svg_path)) {
		return Ref<ImageTexture>();
	}
	
	// If already exists, return directly
	if (svg_registry.has(svg_path)) {
		auto& cacheInfo = svg_registry[svg_path];
		return cacheInfo.texture;
	}
	
	// Create new SVG info and register it first to enable caching
	SvgInfo svg_info;
	svg_info.path = svg_path;
	svg_info.current_scale_level = 1.0f;
	svg_info.raw_size = GdVec2(1, 1);
	
	// Register the info first so that load_svg_at_scale can use caching
	svg_registry[svg_path] = svg_info;
	
	// Now load the texture with caching enabled
	svg_registry[svg_path].texture = load_svg_at_scale(svg_path, 1.0f);
	svg_registry[svg_path].texture->set_path_cache(svg_path);
	if (svg_registry[svg_path].texture.is_valid()) {
		svg_registry[svg_path].raw_size = svg_registry[svg_path].texture->get_size();
	}
	
	if (svg_registry[svg_path].texture.is_valid()) {
		return svg_registry[svg_path].texture;
	}
	
	print_error("[SVG] Failed to create SVG texture: " + svg_path);
	return Ref<ImageTexture>();
}

Ref<ImageTexture> SvgManager::get_or_create_svg_texture_at_scale(const String& svg_path, float scale_level) {
	if (!is_svg_file(svg_path)) {
		return Ref<ImageTexture>();
	}
	
	// Always create a new texture at the specified scale level
	// This is used for animation scaling where we need specific scale versions
	return load_svg_at_scale(svg_path, scale_level);
}

void SvgManager::register_reference(const String& svg_path, SpxSprite* sprite) {
	if (!sprite || !is_svg_file(svg_path)) {
		return;
	}
	
	// Ensure SVG texture exists
	get_or_create_svg_texture(svg_path);
	
	if (!svg_registry.has(svg_path)) {
		print_error("[SVG] Failed to register reference: SVG not found in registry: " + svg_path);
		return;
	}
	
	// Check if already registered
	if (svg_registry[svg_path].references.has(sprite)) {
		return;
	}
	
	// Add reference
	svg_registry[svg_path].references.insert(sprite);
	
	// Check if scaling update is needed
	check_and_update_svg_scale(svg_path);
}

void SvgManager::unregister_reference(const String& svg_path, SpxSprite* sprite) {
	if (!sprite || !svg_registry.has(svg_path)) {
		return;
	}
	
	// Check if reference exists
	if (!svg_registry[svg_path].references.has(sprite)) {
		return;
	}
	
	// Remove reference
	svg_registry[svg_path].references.erase(sprite);
	
	// If no more references, cleanup resources
	if (svg_registry[svg_path].references.is_empty()) {
		cleanup_unused_svg(svg_path);
	} else {
		// Recheck scaling requirements
		check_and_update_svg_scale(svg_path);
	}
}

void SvgManager::on_sprite_scale_changed(SpxSprite* sprite) {
	if (!sprite) {
		return;
	}
	
	// Get all SVG paths associated with this sprite
	HashSet<String> svg_paths = get_sprite_svg_paths(sprite);
	
	if (svg_paths.is_empty()) {
		return;
	}
	
	// Check scaling requirements for each SVG
	for (const String& svg_path : svg_paths) {
		check_and_update_svg_scale(svg_path);
	}
}

void SvgManager::on_sprite_single_texture_scale_changed(SpxSprite* sprite, const String& svg_path) {
	if (!sprite || svg_path.is_empty()) {
		return;
	}
	
	// Only check this one SVG
	check_and_update_svg_scale(svg_path);
}

void SvgManager::on_sprite_animation_scale_changed(SpxSprite* sprite, const String& anim_name) {
	if (!sprite || anim_name.is_empty()) {
		return;
	}
	
	// Get all SVG paths associated with this sprite, then filter for the specified animation
	HashSet<String> all_svg_paths = get_sprite_svg_paths(sprite);
	
	if (all_svg_paths.is_empty()) {
		return;
	}
	
	// Check scaling requirements for each related SVG
	for (const String& svg_path : all_svg_paths) {
		check_and_update_svg_scale(svg_path);
	}
}

void SvgManager::check_and_update_svg_scale(const String& svg_path) {
	if (!svg_registry.has(svg_path)) {
		return;
	}
	
	SvgInfo& svg_info = svg_registry[svg_path];
	
	// Calculate maximum scaling requirement
	float max_required = svg_info.get_max_required_scale();
	
	// Calculate optimal scaling level
	float optimal_level = calculate_optimal_scale_level(max_required);
	
	// Check if upgrade is needed (only support scaling up)
	if (optimal_level > svg_info.current_scale_level) {
		float threshold_value = svg_info.current_scale_level * scale_threshold;
		
		// Check threshold
		if (max_required >= threshold_value) {
			update_svg_texture_data(svg_info, optimal_level);
		} else {
			print_line("[SVG Scale] Threshold not met for", svg_path, "current:", svg_info.current_scale_level, "required:", max_required, "threshold:", threshold_value);
		}
	} 
}

void SvgManager::update_svg_texture_data(SvgInfo& svg_info, float new_scale) {
	if (new_scale <= svg_info.current_scale_level) {
		return; // Only support scaling up
	}
	
	// Check if we already have this scale cached
	Ref<Image> new_image;
	if (svg_info.scale_image_cache.has(new_scale)) {
		new_image = svg_info.scale_image_cache[new_scale];
	} else {
		// Load new high-resolution SVG image
		new_image = load_svg_image_at_scale(svg_info.path, new_scale);
		if (new_image.is_null()) {
			print_error("[SVG] Failed to load SVG at scale " + String::num(new_scale) + ": " + svg_info.path);
			return;
		}
		
		// Cache the loaded image
		svg_info.scale_image_cache[new_scale] = new_image;
		print_line("[SVG Cache] Cached new image for scale", new_scale, "path:", svg_info.path);
	}
	
	// Update texture data (keep object reference unchanged)
	svg_info.texture->set_image(new_image);
	float old_scale = svg_info.current_scale_level;
	svg_info.current_scale_level = new_scale;
	
	
	// Notify all referenced sprites to refresh display without triggering scale checks
	for (SpxSprite* sprite : svg_info.references) {
		if (sprite) {
			// Use SpxSprite's own method to handle display refresh
			sprite->force_redraw();
		}
	}
}

Ref<Image> SvgManager::load_svg_image_at_scale(const String& svg_path, float scale) {
	Ref<Image> image;
	image.instantiate();
	
	Error err = ImageLoader::load_image(svg_path, image, nullptr, ImageFormatLoader::FLAG_NONE, scale);
	if (err != OK) {
		print_error("Failed to load SVG image: " + svg_path + " at scale " + String::num(scale));
		// 回退到默认加载
		err = ImageLoader::load_image(svg_path, image);
		if (err != OK) {
			print_error("Failed to load SVG image with fallback: " + svg_path);
			return Ref<Image>();
		}
	}
	
	return image;
}

Ref<ImageTexture> SvgManager::load_svg_at_scale(const String& svg_path, float scale) {
	// Check if we have this SVG registered and if the scale is cached
	if (svg_registry.has(svg_path) && svg_registry[svg_path].scale_image_cache.has(scale)) {
		Ref<Image> cached_image = svg_registry[svg_path].scale_image_cache[scale];
		Ref<ImageTexture> texture;
		texture.instantiate();
		texture->set_image(cached_image);
		return texture;
	}
	
	// Load new image
	Ref<Image> image = load_svg_image_at_scale(svg_path, scale);
	if (image.is_null()) {
		return Ref<ImageTexture>();
	}
	
	// Cache if this SVG is registered
	if (svg_registry.has(svg_path)) {
		svg_registry[svg_path].scale_image_cache[scale] = image;
	}
	
	Ref<ImageTexture> texture;
	texture.instantiate();
	texture->set_image(image);
	
	return texture;
}

void SvgManager::cleanup_unused_svg(const String& svg_path) {
	if (svg_registry.has(svg_path)) {
		svg_registry.erase(svg_path);
	}
}

HashSet<String> SvgManager::get_sprite_svg_paths(SpxSprite* sprite) {
	HashSet<String> paths;
	
	if (!sprite) {
		return paths;
	}
	
	// 检查所有 SVG 注册表，找到包含该精灵的记录
	for (const KeyValue<String, SvgInfo>& pair : svg_registry) {
		if (pair.value.references.has(sprite)) {
			paths.insert(pair.key);
		}
	}
	
	return paths;
}
void SvgManager::destroy(){
	svg_registry.clear();
	singleton = nullptr;
}
float SvgManager::get_image_raw_scale(const String& path) const{
	if (svg_registry.has(path)) {
		return svg_registry[path].current_scale_level;
	}
	return 1.0f;
}
void SvgManager::print_svg_info() const {
	print_line("=== SVG Global Manager Info ===");
	print_line("Total SVGs: " + String::num(svg_registry.size()));
	print_line("Scale threshold: " + String::num(scale_threshold));
	print_line("Max scale level: " + String::num(max_scale_level));
	
	for (const KeyValue<String, SvgInfo>& pair : svg_registry) {
		const SvgInfo& info = pair.value;
		print_line("SVG: " + pair.key);
		print_line("  Scale: " + String::num(info.current_scale_level));
		print_line("  References: " + String::num(info.references.size()));
		print_line("  Max required: " + String::num(info.get_max_required_scale()));
	}
} 
Vector2 SvgManager::get_image_raw_size(const String& path) const{
	if (svg_registry.has(path)) {
		return svg_registry[path].raw_size;
	}
	return Vector2(1, 1);
}