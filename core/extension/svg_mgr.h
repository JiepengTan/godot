#ifndef SVG_MANAGER_H
#define SVG_MANAGER_H

#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "scene/resources/image_texture.h"

class SpxSprite;

class SvgManager {
public:
	static SvgManager *get_singleton();
	
	SvgManager();
	~SvgManager();

private:
	struct SvgInfo {
		String path;
		Vector2 raw_size;
		Ref<ImageTexture> texture;      // global shared texture
		float current_scale_level;      // current scale level (1, 2, 4, 8, 16...)
		HashSet<SpxSprite*> references; // all sprites that reference this SVG
		
		// Add scale-level cache to avoid re-parsing same scale
		HashMap<float, Ref<Image>> scale_image_cache; // scale -> cached Image
		
		float get_max_required_scale() const;
		
		SvgInfo() {
			current_scale_level = 1.0f;
		}
	};
	
	HashMap<String, SvgInfo> svg_registry;  // path -> SvgInfo
	float scale_threshold = 1.5f;           // upgrade threshold
	int max_scale_level = 16;               // max scale level
	
	static SvgManager *singleton;

public:
	// 主要接口
	Ref<ImageTexture> get_or_create_svg_texture(const String& svg_path);
	Ref<ImageTexture> get_or_create_svg_texture_at_scale(const String& svg_path, float scale_level);
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
	Ref<Image> load_svg_image_at_scale(const String& svg_path, float scale); // Add separate method for Image loading
	void cleanup_unused_svg(const String& svg_path);
	
	// 获取精灵关联的所有SVG路径
	HashSet<String> get_sprite_svg_paths(SpxSprite* sprite);
};

#endif // SVG_MANAGER_H 