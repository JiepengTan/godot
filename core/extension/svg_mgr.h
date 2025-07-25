#ifndef SVG_MANAGER_H
#define SVG_MANAGER_H

#include "core/math/vector2.h"
#include "core/templates/hash_map.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/sprite_frames.h"

class SvgManager {
public:
	static SvgManager *get_singleton();
	
	SvgManager();
	~SvgManager();

private:
	// 新的简化数据结构
	HashMap<String, Ref<ImageTexture>> svg_image_cache;     // "scale@图片路径" -> ImageTexture
	HashMap<String, Ref<SpriteFrames>> svg_animation_cache; // "scale@动画名" -> SpriteFrames
	HashMap<String, Vector2> svg_image_raw_size_cache;
	
	HashMap<String, bool> is_svg_animation_registry;
	static SvgManager *singleton;

public:

	// 核心接口
	Ref<SpriteFrames> get_svg_animation(const String& anim_name, int scale);
	Ref<ImageTexture> get_svg_image(const String& image_path, int scale);
	Vector2 get_image_raw_size(const String& image_path);
	
	// 工具方法
	String make_image_key(const String& path, int scale);     // "scale@path"
	String make_animation_key(const String& name, int scale); // "scale@name"
	
	// 基础方法
	bool is_svg_file(const String& path) const;
	bool is_svg_animation(const String& anim_name);
	void set_is_svg_animation(const String& anim_name, bool is_svg_animation);
	
	// Utility methods
	int calculate_optimal_scale_level(Vector2 required_scale);
	void destroy();
private:
	// 内部创建方法
	Ref<SpriteFrames> create_svg_animation(const String& anim_name, int scale);
	Ref<ImageTexture> load_svg_image_at_scale(const String& path, int scale);
	Ref<SpriteFrames> create_single_image_animation(const String& image_path, int scale);
	Ref<SpriteFrames> create_multi_frame_animation(const String& anim_name, int scale);
};

#endif // SVG_MANAGER_H 