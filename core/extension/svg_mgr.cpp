#include "svg_mgr.h"

#include "core/io/image_loader.h"
#include "spx_res_mgr.h"
#include "spx_engine.h"

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
	svg_image_cache.clear();
	svg_animation_cache.clear();
	singleton = nullptr;
}

bool SvgManager::is_svg_file(const String& path) const {
	return path.to_lower().ends_with(".svg");
}

String SvgManager::make_image_key(const String& path, int scale) {
	return String::num(scale) + "@" + path;
}

String SvgManager::make_animation_key(const String& name, int scale) {
	return String::num(scale) + "@" + name;
}

Ref<ImageTexture> SvgManager::get_svg_image(const String& image_path, int scale) {
	if (!is_svg_file(image_path)) {
		return Ref<ImageTexture>();
	}
	
	String key = make_image_key(image_path, scale);
	
	if (svg_image_cache.has(key)) {
		return svg_image_cache[key];
	}
	
	return load_svg_image_at_scale(image_path, scale);
}

Ref<SpriteFrames> SvgManager::get_svg_animation(const String& anim_name, int scale) {
	String key = make_animation_key(anim_name, scale);
	
	if (svg_animation_cache.has(key)) {
		return svg_animation_cache[key];
	}
	
	return create_svg_animation(anim_name, scale);
}
Vector2 SvgManager::get_image_raw_size(const String& path){
	if(svg_image_raw_size_cache.has(path)){
		return svg_image_raw_size_cache[path];
	}
	return Vector2(1,1);
}

Ref<ImageTexture> SvgManager::load_svg_image_at_scale(const String& path, int scale) {
	String key = make_image_key(path, scale);
	
	if (svg_image_cache.has(key)) {
		return svg_image_cache[key];
	}
	

	// 加载SVG图片
	Ref<Image> image;
	image.instantiate();
	Error err = ImageLoader::load_image(path, image, nullptr, 
									  ImageFormatLoader::FLAG_NONE, (float)scale);
	if (err == OK) {
		Ref<ImageTexture> texture;
		texture.instantiate();
		texture->set_image(image);
		texture->set_path_cache(path);
		
		if(!svg_image_raw_size_cache.has(path)){
			svg_image_raw_size_cache[path] = Vector2(image->get_width()/scale,image->get_height()/scale);
		}
		// 缓存纹理
		svg_image_cache[key] = texture;
		return texture;
	}
	
	print_error("[SvgManager] Failed to load SVG image: " + path + " at scale " + String::num(scale));
	return Ref<ImageTexture>();
}
bool SvgManager::is_svg_animation(const String& anim_name) {
	return is_svg_animation_registry[anim_name];
}

void SvgManager::set_is_svg_animation(const String& anim_name, bool is_svg_animation) {
	is_svg_animation_registry[anim_name] = is_svg_animation;
}

Ref<SpriteFrames> SvgManager::create_svg_animation(const String& anim_name, int scale) {
	String key = make_animation_key(anim_name, scale);
	
	if (svg_animation_cache.has(key)) {
		return svg_animation_cache[key];
	}
	
	Ref<SpriteFrames> frames;
	
	// 检查是否为单图片路径 (以.svg结尾)
	if (anim_name.ends_with(".svg")) {
		frames = create_single_image_animation(anim_name, scale);
	} else {
		// 否则从资源管理器获取动画帧列表
		frames = create_multi_frame_animation(anim_name, scale);
	}
	
	if (frames.is_valid()) {
		svg_animation_cache[key] = frames;
	}
	
	return frames;
}

Ref<SpriteFrames> SvgManager::create_single_image_animation(const String& image_path, int scale) {
	// 加载单张SVG图片
	Ref<ImageTexture> texture = load_svg_image_at_scale(image_path, scale);
	
	if (!texture.is_valid()) {
		print_error("[SvgManager] Failed to load single SVG image: " + image_path);
		return Ref<SpriteFrames>();
	}
	
	// 创建包含单帧的动画
	Ref<SpriteFrames> frames;
	frames.instantiate();
	
	String anim_name = "default";
	frames->add_animation(anim_name);
	frames->add_frame(anim_name, texture);
	frames->set_animation_loop(anim_name, false);
	
	return frames;
}

Ref<SpriteFrames> SvgManager::create_multi_frame_animation(const String& anim_name, int scale) {
	// 从SpxResMgr获取动画定义
	auto res_mgr = SpxEngine::get_singleton()->get_res();
	if (!res_mgr) {
		print_error("[SvgManager] Cannot access SpxResMgr");
		return Ref<SpriteFrames>();
	}
	
	// 获取现有动画的帧列表
	auto existing_frames = res_mgr->get_anim_frames(anim_name);
	if (!existing_frames.is_valid()) {
		print_error("[SvgManager] Animation not found: " + anim_name);
		return Ref<SpriteFrames>();
	}
	
	// 检查动画是否包含SVG帧
	if (!existing_frames->has_animation(anim_name)) {
		print_error("[SvgManager] Animation key not found: " + anim_name);
		return Ref<SpriteFrames>();
	}
	
	// 创建新的SpriteFrames，替换其中的SVG纹理为缩放版本
	Ref<SpriteFrames> new_frames;
	new_frames.instantiate();
	new_frames->add_animation(anim_name);
	
	// 复制动画属性
	new_frames->set_animation_loop(anim_name, existing_frames->get_animation_loop(anim_name));
	new_frames->set_animation_speed(anim_name, existing_frames->get_animation_speed(anim_name));
	
	int frame_count = existing_frames->get_frame_count(anim_name);
	for (int i = 0; i < frame_count; i++) {
		auto original_texture = existing_frames->get_frame_texture(anim_name, i);
		float duration = existing_frames->get_frame_duration(anim_name, i);
		
		// 检查是否为SVG纹理
		String texture_path = original_texture->get_path();
		if (is_svg_file(texture_path)) {
			// 加载缩放版本的SVG
			Ref<ImageTexture> scaled_texture = load_svg_image_at_scale(texture_path, scale);
			if (scaled_texture.is_valid()) {
				new_frames->add_frame(anim_name, scaled_texture, duration);
			} else {
				// 如果SVG加载失败，使用原纹理
				new_frames->add_frame(anim_name, original_texture, duration);
			}
		} else {
			// 非SVG纹理直接使用原纹理
			new_frames->add_frame(anim_name, original_texture, duration);
		}
	}
	
	return new_frames;
}

void SvgManager::destroy() {
	svg_image_cache.clear();
	svg_animation_cache.clear();
	singleton = nullptr;
}

int SvgManager::calculate_optimal_scale_level(Vector2 required_scale) {
	float scale = MAX(required_scale.x, required_scale.y);
	// Use powers of 2: 1, 2, 4, 8, 16...
	if (scale <= 1.0f) return 1;
	if (scale <= 2.0f) return 2;
	if (scale <= 4.0f) return 4;
	if (scale <= 8.0f) return 8;
	return 16;  // Max scale level
}
