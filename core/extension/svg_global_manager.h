/**************************************************************************/
/*  svg_global_manager.h                                                 */
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

#ifndef SVG_GLOBAL_MANAGER_H
#define SVG_GLOBAL_MANAGER_H

#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "scene/resources/image_texture.h"

class SpxSprite;

class SvgGlobalManager {
public:
	static SvgGlobalManager *get_singleton();
	
	SvgGlobalManager();
	~SvgGlobalManager();

private:
	struct SvgInfo {
		String path;
		Vector2 raw_size;
		Ref<ImageTexture> texture;      // global shared texture
		float current_scale_level;      // current scale level (1, 2, 4, 8, 16...)
		HashSet<SpxSprite*> references; // all sprites that reference this SVG
		
		float get_max_required_scale() const;
		
		SvgInfo() {
			current_scale_level = 1.0f;
		}
	};
	
	HashMap<String, SvgInfo> svg_registry;  // path -> SvgInfo
	float scale_threshold = 1.5f;           // upgrade threshold
	int max_scale_level = 16;               // max scale level
	
	static SvgGlobalManager *singleton;

public:
	// 主要接口
	Ref<ImageTexture> get_or_create_svg_texture(const String& svg_path);
	void register_reference(const String& svg_path, SpxSprite* sprite);
	void unregister_reference(const String& svg_path, SpxSprite* sprite);
	void on_sprite_scale_changed(SpxSprite* sprite);
	void on_sprite_single_texture_scale_changed(SpxSprite* sprite, const String& svg_path);
	void on_sprite_animation_scale_changed(SpxSprite* sprite, const String& anim_name);
	
	// 配置接口
	void set_scale_threshold(float threshold) { scale_threshold = threshold; }
	float get_scale_threshold() const { return scale_threshold; }
	void set_max_scale_level(int max_level) { max_scale_level = max_level; }
	int get_max_scale_level() const { return max_scale_level; }
	
	// 调试接口
	int get_svg_count() const { return svg_registry.size(); }
	void print_svg_info() const;

	Vector2 get_image_raw_size(const String& path) const;
	float get_image_raw_scale(const String& path) const;
	void destroy();
	bool is_svg_file(const String& path) const;
private:
	// 内部方法
	float calculate_optimal_scale_level(float required_scale) const;
	void check_and_update_svg_scale(const String& svg_path);
	void update_svg_texture_data(SvgInfo& svg_info, float new_scale);
	Ref<ImageTexture> load_svg_at_scale(const String& svg_path, float scale);
	void cleanup_unused_svg(const String& svg_path);
	
	// 获取精灵关联的所有SVG路径
	HashSet<String> get_sprite_svg_paths(SpxSprite* sprite);
};

#endif // SVG_GLOBAL_MANAGER_H 