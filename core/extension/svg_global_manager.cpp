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
	for (SpxSprite* sprite : references) {
		if (sprite) {
			Vector2 actual_scale = sprite->get_actual_render_scale();
			float sprite_max = MAX(actual_scale.x, actual_scale.y);
			max_scale = MAX(max_scale, sprite_max);
		}
	}
	return max_scale;
}

bool SvgGlobalManager::is_svg_file(const String& path) const {
	return path.to_lower().ends_with(".svg");
}

float SvgGlobalManager::calculate_optimal_scale_level(float required_scale) const {
	if (required_scale <= 1.0f) {
		return 1.0f;
	}
	
	// 使用 2 的幂次作为缩放级别: 1, 2, 4, 8, 16, ...
	float level = 1.0f;
	while (level < required_scale && level < max_scale_level) {
		level *= 2.0f;
	}
	
	return MIN(level, (float)max_scale_level);
}

Ref<ImageTexture> SvgGlobalManager::get_or_create_svg_texture(const String& svg_path) {
	if (!is_svg_file(svg_path)) {
		return Ref<ImageTexture>();
	}
	
	// 如果已经存在，直接返回
	if (svg_registry.has(svg_path)) {
		return svg_registry[svg_path].texture;
	}
	
	// 创建新的 SVG 信息
	SvgInfo svg_info;
	svg_info.path = svg_path;
	svg_info.current_scale_level = 1.0f;
	svg_info.texture = load_svg_at_scale(svg_path, 1.0f);
	
	if (svg_info.texture.is_valid()) {
		svg_registry[svg_path] = svg_info;
		return svg_info.texture;
	}
	
	return Ref<ImageTexture>();
}

void SvgGlobalManager::register_reference(const String& svg_path, SpxSprite* sprite) {
	if (!sprite || !is_svg_file(svg_path)) {
		return;
	}
	
	// 确保 SVG 纹理存在
	get_or_create_svg_texture(svg_path);
	
	if (!svg_registry.has(svg_path)) {
		return;
	}
	
	// 添加引用
	svg_registry[svg_path].references.insert(sprite);
	
	// 检查是否需要更新缩放
	check_and_update_svg_scale(svg_path);
}

void SvgGlobalManager::unregister_reference(const String& svg_path, SpxSprite* sprite) {
	if (!sprite || !svg_registry.has(svg_path)) {
		return;
	}
	
	// 移除引用
	svg_registry[svg_path].references.erase(sprite);
	
	// 如果没有引用了，清理资源
	if (svg_registry[svg_path].references.is_empty()) {
		cleanup_unused_svg(svg_path);
	} else {
		// 重新检查缩放需求
		check_and_update_svg_scale(svg_path);
	}
}

void SvgGlobalManager::on_sprite_scale_changed(SpxSprite* sprite) {
	if (!sprite) {
		return;
	}
	
	// 获取该精灵关联的所有 SVG 路径
	HashSet<String> svg_paths = get_sprite_svg_paths(sprite);
	
	// 检查每个 SVG 的缩放需求
	for (const String& svg_path : svg_paths) {
		check_and_update_svg_scale(svg_path);
	}
}

void SvgGlobalManager::check_and_update_svg_scale(const String& svg_path) {
	if (!svg_registry.has(svg_path)) {
		return;
	}
	
	SvgInfo& svg_info = svg_registry[svg_path];
	
	// 计算最大缩放需求
	float max_required = svg_info.get_max_required_scale();
	
	// 计算最优缩放级别
	float optimal_level = calculate_optimal_scale_level(max_required);
	
	// 检查是否需要升级（只支持放大）
	if (optimal_level > svg_info.current_scale_level) {
		// 检查阈值
		if (max_required >= svg_info.current_scale_level * scale_threshold) {
			update_svg_texture_data(svg_info, optimal_level);
		}
	}
}

void SvgGlobalManager::update_svg_texture_data(SvgInfo& svg_info, float new_scale) {
	if (new_scale <= svg_info.current_scale_level) {
		return; // 只支持放大
	}
	
	// 加载新的高分辨率 SVG 图像
	Ref<Image> new_image;
	new_image.instantiate();
	
	Error err = ImageLoader::load_image(svg_info.path, new_image, nullptr, new_scale);
	if (err != OK) {
		print_error("Failed to load SVG at scale " + String::num(new_scale) + ": " + svg_info.path);
		return;
	}
	
	// 更新纹理数据（保持对象引用不变）
	svg_info.texture->set_image(new_image);
	svg_info.current_scale_level = new_scale;
	
	print_line("SVG updated to scale " + String::num(new_scale) + ": " + svg_info.path + 
	          " (references: " + String::num(svg_info.references.size()) + ")");
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
		print_line("Cleaning up unused SVG: " + svg_path);
		svg_registry.erase(svg_path);
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