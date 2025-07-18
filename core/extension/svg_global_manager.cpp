/**************************************************************************/
/*  svg_global_manager.cpp                                               */
/**************************************************************************/
/*                         This file is part of:                         */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                         */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                               */
/*                                                                         */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                         */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "svg_global_manager.h"

#include "core/io/image_loader.h"
#include "spx_sprite.h"
#include "spx_engine.h"
#include "spx_camera_mgr.h"

SvgGlobalManager *SvgGlobalManager::singleton = nullptr;

SvgGlobalManager *SvgGlobalManager::get_singleton() {
	return singleton;
}

SvgGlobalManager::SvgGlobalManager() {
	singleton = this;
}

SvgGlobalManager::~SvgGlobalManager() {
	svg_registry.clear();
	singleton = nullptr;
}

float SvgGlobalManager::SvgInfo::get_max_required_scale() const {
	float max_scale = 1.0f;
	print_line("[SVG] Calculating max required scale from " + String::num(references.size()) + " references:");
	
	for (SpxSprite* sprite : references) {
		if (sprite) {
			Vector2 actual_scale = sprite->get_actual_render_scale();
			float sprite_max = MAX(actual_scale.x, actual_scale.y);
			print_line("[SVG]   Sprite " + String::num_uint64((uint64_t)sprite) + 
			          ": scale=(" + String::num(actual_scale.x) + ", " + String::num(actual_scale.y) + 
			          ") max=" + String::num(sprite_max));
			max_scale = MAX(max_scale, sprite_max);
		} else {
			print_line("[SVG]   Found null sprite reference!");
		}
	}
	
	print_line("[SVG] Max required scale: " + String::num(max_scale));
	return max_scale;
}

bool SvgGlobalManager::is_svg_file(const String& path) const {
	return path.to_lower().ends_with(".svg");
}

float SvgGlobalManager::calculate_optimal_scale_level(float required_scale) const {
	if (required_scale <= 1.0f) {
		print_line("[SVG] Required scale <= 1.0, returning 1.0");
		return 1.0f;
	}
	
	// 使用 2 的幂次作为缩放级别: 1, 2, 4, 8, 16, ...
	float level = 1.0f;
	while (level < required_scale && level < max_scale_level) {
		level *= 2.0f;
	}
	
	float result = MIN(level, (float)max_scale_level);
	print_line("[SVG] Calculated optimal scale: " + String::num(required_scale) + 
	          " -> " + String::num(result) + " (max allowed: " + String::num(max_scale_level) + ")");
	return result;
}

Ref<ImageTexture> SvgGlobalManager::get_or_create_svg_texture(const String& svg_path) {
	if (!is_svg_file(svg_path)) {
		print_line("[SVG] Not an SVG file: " + svg_path);
		return Ref<ImageTexture>();
	}
	
	// 如果已经存在，直接返回
	if (svg_registry.has(svg_path)) {
		print_line("[SVG] Returning existing texture for: " + svg_path + 
		          " (current scale: " + String::num(svg_registry[svg_path].current_scale_level) + 
		          ", references: " + String::num(svg_registry[svg_path].references.size()) + ")");
		auto& cacheInfo = svg_registry[svg_path];
		return cacheInfo.texture;
	}
	
	print_line("[SVG] Creating new SVG texture: " + svg_path);
	
	// 创建新的 SVG 信息
	SvgInfo svg_info;
	svg_info.path = svg_path;
	svg_info.current_scale_level = 1.0f;
	svg_info.texture = load_svg_at_scale(svg_path, 1.0f);
	svg_registry[svg_path] = svg_info;
	if (svg_info.texture.is_valid()) {
		print_line("[SVG] Successfully created SVG texture at 1.0x scale: " + svg_path);
		return svg_info.texture;
	}
	
	print_error("[SVG] Failed to create SVG texture: " + svg_path);
	return Ref<ImageTexture>();
}

void SvgGlobalManager::register_reference(const String& svg_path, SpxSprite* sprite) {
	if (!sprite || !is_svg_file(svg_path)) {
		if (!sprite) {
			print_line("[SVG] register_reference: sprite is null for path: " + svg_path);
		}
		return;
	}
	
	print_line("[SVG] Registering reference for sprite " + String::num_uint64((uint64_t)sprite) + 
	          " to SVG: " + svg_path);
	
	// 确保 SVG 纹理存在
	get_or_create_svg_texture(svg_path);
	
	if (!svg_registry.has(svg_path)) {
		print_error("[SVG] Failed to register reference: SVG not found in registry: " + svg_path);
		return;
	}
	
	// 检查是否已经注册过
	if (svg_registry[svg_path].references.has(sprite)) {
		print_line("[SVG] Reference already exists for sprite " + String::num_uint64((uint64_t)sprite) + 
		          " to SVG: " + svg_path);
		return;
	}
	
	// 添加引用
	svg_registry[svg_path].references.insert(sprite);
	print_line("[SVG] Added reference. Total references for " + svg_path + ": " + 
	          String::num(svg_registry[svg_path].references.size()));
	
	// 检查是否需要更新缩放
	check_and_update_svg_scale(svg_path);
}

void SvgGlobalManager::unregister_reference(const String& svg_path, SpxSprite* sprite) {
	if (!sprite || !svg_registry.has(svg_path)) {
		if (!sprite) {
			print_line("[SVG] unregister_reference: sprite is null for path: " + svg_path);
		} else {
			print_line("[SVG] unregister_reference: SVG not found in registry: " + svg_path);
		}
		return;
	}
	
	print_line("[SVG] Unregistering reference for sprite " + String::num_uint64((uint64_t)sprite) + 
	          " from SVG: " + svg_path);
	
	// 检查引用是否存在
	if (!svg_registry[svg_path].references.has(sprite)) {
		print_line("[SVG] Reference not found for sprite " + String::num_uint64((uint64_t)sprite) + 
		          " in SVG: " + svg_path);
		return;
	}
	
	// 移除引用
	svg_registry[svg_path].references.erase(sprite);
	print_line("[SVG] Removed reference. Remaining references for " + svg_path + ": " + 
	          String::num(svg_registry[svg_path].references.size()));
	
	// 如果没有引用了，清理资源
	if (svg_registry[svg_path].references.is_empty()) {
		print_line("[SVG] No more references, cleaning up: " + svg_path);
		cleanup_unused_svg(svg_path);
	} else {
		print_line("[SVG] Rechecking scale requirements after reference removal: " + svg_path);
		// 重新检查缩放需求
		check_and_update_svg_scale(svg_path);
	}
}

void SvgGlobalManager::on_sprite_scale_changed(SpxSprite* sprite) {
	if (!sprite) {
		print_line("[SVG] on_sprite_scale_changed: sprite is null");
		return;
	}
	
	Vector2 actual_scale = sprite->get_actual_render_scale();
	print_line("[SVG] Sprite " + String::num_uint64((uint64_t)sprite) + 
	          " scale changed to: (" + String::num(actual_scale.x) + ", " + String::num(actual_scale.y) + ")");
	
	// 这是旧的通用方法，现在推荐使用模式特定的方法
	print_line("[SVG] Warning: Using generic on_sprite_scale_changed, consider using mode-specific methods");
	
	// 获取该精灵关联的所有 SVG 路径
	HashSet<String> svg_paths = get_sprite_svg_paths(sprite);
	
	if (svg_paths.is_empty()) {
		print_line("[SVG] Sprite " + String::num_uint64((uint64_t)sprite) + " has no SVG references");
		return;
	}
	
	print_line("[SVG] Found " + String::num(svg_paths.size()) + " SVG references for sprite " + 
	          String::num_uint64((uint64_t)sprite));
	
	// 检查每个 SVG 的缩放需求
	for (const String& svg_path : svg_paths) {
		print_line("[SVG] Checking scale requirements for: " + svg_path);
		check_and_update_svg_scale(svg_path);
	}
}

void SvgGlobalManager::on_sprite_single_texture_scale_changed(SpxSprite* sprite, const String& svg_path) {
	if (!sprite) {
		print_line("[SVG] on_sprite_single_texture_scale_changed: sprite is null");
		return;
	}
	
	if (svg_path.is_empty()) {
		print_line("[SVG] on_sprite_single_texture_scale_changed: svg_path is empty");
		return;
	}
	
	Vector2 actual_scale = sprite->get_actual_render_scale();
	print_line("[SVG] *** SINGLE TEXTURE MODE *** Sprite " + String::num_uint64((uint64_t)sprite) + 
	          " scale changed to: (" + String::num(actual_scale.x) + ", " + String::num(actual_scale.y) + 
	          ") for SVG: " + svg_path);
	
	// 只检查这一个 SVG
	check_and_update_svg_scale(svg_path);
}

void SvgGlobalManager::on_sprite_animation_scale_changed(SpxSprite* sprite, const String& anim_name) {
	if (!sprite) {
		print_line("[SVG] on_sprite_animation_scale_changed: sprite is null");
		return;
	}
	
	if (anim_name.is_empty()) {
		print_line("[SVG] on_sprite_animation_scale_changed: anim_name is empty");
		return;
	}
	
	Vector2 actual_scale = sprite->get_actual_render_scale();
	print_line("[SVG] *** ANIMATION MODE *** Sprite " + String::num_uint64((uint64_t)sprite) + 
	          " scale changed to: (" + String::num(actual_scale.x) + ", " + String::num(actual_scale.y) + 
	          ") for animation: " + anim_name);
	
	// 获取该精灵关联的所有 SVG 路径，然后筛选出属于指定动画的 SVG
	HashSet<String> all_svg_paths = get_sprite_svg_paths(sprite);
	HashSet<String> animation_svg_paths;
	
	// 通过检查动画帧来确定哪些 SVG 属于这个动画
	// 注意：这需要访问精灵的动画帧信息，这里简化为检查所有关联的 SVG
	// 在实际使用中，可能需要更精确的筛选逻辑
	
	if (all_svg_paths.is_empty()) {
		print_line("[SVG] Sprite " + String::num_uint64((uint64_t)sprite) + " has no SVG references for animation: " + anim_name);
		return;
	}
	
	print_line("[SVG] Found " + String::num(all_svg_paths.size()) + " SVG references for animation: " + anim_name);
	
	// 检查每个相关 SVG 的缩放需求
	for (const String& svg_path : all_svg_paths) {
		print_line("[SVG] Checking animation SVG scale requirements for: " + svg_path);
		check_and_update_svg_scale(svg_path);
	}
}

void SvgGlobalManager::check_and_update_svg_scale(const String& svg_path) {
	if (!svg_registry.has(svg_path)) {
		print_line("[SVG] check_and_update_svg_scale: SVG not found in registry: " + svg_path);
		return;
	}
	
	SvgInfo& svg_info = svg_registry[svg_path];
	
	// 计算最大缩放需求
	float max_required = svg_info.get_max_required_scale();
	
	// 计算最优缩放级别
	float optimal_level = calculate_optimal_scale_level(max_required);
	
	// 检查是否需要升级（只支持放大）
	if (optimal_level > svg_info.current_scale_level) {
		float threshold_value = svg_info.current_scale_level * scale_threshold;
		print_line("[SVG] Upgrade candidate: optimal=" + String::num(optimal_level) + 
		          " > current=" + String::num(svg_info.current_scale_level) + 
		          ", threshold=" + String::num(threshold_value));
		
		// 检查阈值
		if (max_required >= threshold_value) {
			update_svg_texture_data(svg_info, optimal_level);
		} 
	} else {
		print_line("[SVG] No upgrade needed: optimal=" + String::num(optimal_level) + 
		          " <= current=" + String::num(svg_info.current_scale_level));
	}
}

void SvgGlobalManager::update_svg_texture_data(SvgInfo& svg_info, float new_scale) {
	if (new_scale <= svg_info.current_scale_level) {
		return; // 只支持放大
	}
	
	print_line("[SVG] *** UPDATING SVG TEXTURE ***");
	print_line("[SVG] Path: " + svg_info.path);
	print_line("[SVG] Current scale: " + String::num(svg_info.current_scale_level));
	print_line("[SVG] New scale: " + String::num(new_scale));
	print_line("[SVG] Affected references: " + String::num(svg_info.references.size()));
	
	// 加载新的高分辨率 SVG 图像
	Ref<Image> new_image;
	new_image.instantiate();
	
	print_line("[SVG] Loading SVG image at " + String::num(new_scale) + "x scale...");
	Error err = ImageLoader::load_image(svg_info.path, new_image, nullptr,  ImageFormatLoader::FLAG_NONE, new_scale);
	if (err != OK) {
		print_error("[SVG] *** FAILED *** to load SVG at scale " + String::num(new_scale) + ": " + svg_info.path + 
		           " (error code: " + String::num(err) + ")");
		return;
	}
	
	Vector2i image_size = new_image->get_size();
	print_line("[SVG] Successfully loaded image: " + String::num(image_size.x) + "x" + String::num(image_size.y));
	
	// 更新纹理数据（保持对象引用不变）
	print_line("[SVG] Updating texture data...");
	svg_info.texture->set_image(new_image);
	float old_scale = svg_info.current_scale_level;
	svg_info.current_scale_level = new_scale;
	
	// 通知所有引用的精灵刷新显示
	print_line("[SVG] Notifying " + String::num(svg_info.references.size()) + " sprites to refresh display...");
	for (SpxSprite* sprite : svg_info.references) {
		if (sprite && sprite->anim2d) {
			// 强制刷新 AnimatedSprite2D 的显示
			sprite->on_svg_changed();
		}
	}
	
	print_line("[SVG] *** SUCCESS *** SVG texture updated from " + String::num(old_scale) + "x to " + 
	          String::num(new_scale) + "x: " + svg_info.path + 
	          " (image size: " + String::num(image_size.x) + "x" + String::num(image_size.y) + 
	          ", references: " + String::num(svg_info.references.size()) + ")");
}

Ref<ImageTexture> SvgGlobalManager::load_svg_at_scale(const String& svg_path, float scale) {
	Ref<Image> image;
	image.instantiate();
	
	Error err = ImageLoader::load_image(svg_path, image, nullptr, scale);
	if (err != OK) {
		// 回退到默认加载
		err = ImageLoader::load_image(svg_path, image);
		if (err != OK) {
			print_error("Failed to load SVG: " + svg_path);
			return Ref<ImageTexture>();
		}
	}
	
	Ref<ImageTexture> texture;
	texture.instantiate();
	texture->set_image(image);
	
	return texture;
}

void SvgGlobalManager::cleanup_unused_svg(const String& svg_path) {
	if (svg_registry.has(svg_path)) {
		const SvgInfo& info = svg_registry[svg_path];
		print_line("[SVG] *** CLEANUP *** Removing unused SVG: " + svg_path + 
		          " (was at " + String::num(info.current_scale_level) + "x scale)");
		svg_registry.erase(svg_path);
		print_line("[SVG] Total SVGs in registry: " + String::num(svg_registry.size()));
	} else {
		print_line("[SVG] cleanup_unused_svg: SVG not found in registry: " + svg_path);
	}
}

HashSet<String> SvgGlobalManager::get_sprite_svg_paths(SpxSprite* sprite) {
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

void SvgGlobalManager::print_svg_info() const {
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