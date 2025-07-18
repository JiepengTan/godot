# SVG系统重构计划 (修订版)

## 🎯 **重构目标 (基于新需求)**

1. **统一动画模式**：单张图片和多帧动画都视为动画，简化处理逻辑
2. **简化数据结构**：只维护两个核心映射表
3. **按需加载策略**：只加载不卸载，减少内存管理复杂度
4. **整数缩放**：使用int类型简化scale计算

## 🏗️ **新架构设计**

### **核心数据结构**
```cpp
class SvgManager {
private:
    // 1. SVG图片缓存: "scale@图片路径" -> ImageTexture
    HashMap<String, Ref<ImageTexture>> svg_image_cache;
    
    // 2. SVG动画缓存: "scale@动画名" -> SpriteFrames  
    HashMap<String, Ref<SpriteFrames>> svg_animation_cache;
    
public:
    // 核心接口
    Ref<SpriteFrames> get_svg_animation(const String& anim_name, int scale);
    Ref<ImageTexture> get_svg_image(const String& image_path, int scale);
};
```

### **关键简化**
- **移除观察者模式**：不需要复杂的通知机制
- **移除双重引用管理**：只有一种数据获取方式
- **统一接口**：单张图片和动画用同一套API处理

## 📋 **分阶段实现计划**

### **阶段1：架构设计确认** (预计1-2天)

#### 1.1 核心接口设计确认
```cpp
class SvgManager {
public:
    // 主要接口 - 获取SVG动画（包括单图片动画）
    Ref<SpriteFrames> get_svg_animation(const String& anim_name, int scale);
    
    // 辅助接口 - 直接获取单张SVG图片
    Ref<ImageTexture> get_svg_image(const String& image_path, int scale);
    
    // 工具方法
    String make_image_key(const String& path, int scale);     // "scale@path"
    String make_animation_key(const String& name, int scale); // "scale@name"
    
private:
    HashMap<String, Ref<ImageTexture>> svg_image_cache;
    HashMap<String, Ref<SpriteFrames>> svg_animation_cache;
};
```

#### 1.2 SpxSprite接口简化
```cpp
class SpxSprite {
public:
    // 统一的设置接口
    void set_animation(const String& anim_name, int scale = 1);
    void set_single_image(const String& image_path, int scale = 1);
    
private:
    // 简化的状态跟踪
    String current_animation_name;
    int current_scale = 1;
    
    // 内部方法
    void update_svg_animation_if_needed();
};
```

#### 1.3 数据流程确认
1. **单张图片流程**：
   ```
   set_single_image(path, scale) 
   → get_svg_animation(path, scale)  // 把单图片当作特殊动画
   → 如果缓存未命中，创建单帧SpriteFrames
   → 设置到AnimatedSprite2D
   ```

2. **多帧动画流程**：
   ```
   set_animation(name, scale)
   → get_svg_animation(name, scale)
   → 如果缓存未命中，加载所有帧的SVG图片
   → 创建多帧SpriteFrames
   → 设置到AnimatedSprite2D
   ```

### **阶段2：移除现有复杂系统** (预计2-3天)

#### 2.1 删除观察者相关代码
- [ ] 删除 `svg_observer.h`
- [ ] 删除 `svg_resource.h/cpp`
- [ ] 移除 `SpxSprite` 中的 `SvgObserver` 继承
- [ ] 删除所有 `on_svg_*` 回调方法

#### 2.2 清理SvgManager中的复杂逻辑
- [ ] 移除 `SvgResource` 类
- [ ] 删除观察者管理方法 (`add_observer`, `remove_observer`, etc.)
- [ ] 删除批量操作方法 (`batch_*`)
- [ ] 移除兼容性接口

#### 2.3 简化SpxSprite的SVG集成
- [ ] 删除复杂的依赖管理系统
  ```cpp
  // 删除这些
  // ❌ Vector<SvgDependency> current_dependencies;
  // ❌ SpriteDisplayMode current_display_mode;
  // ❌ String current_single_texture_svg_path;
  // ❌ bool dependencies_dirty;
  ```

- [ ] 移除复杂的刷新机制
  ```cpp
  // 删除这些方法
  // ❌ refresh_svg_dependencies()
  // ❌ clear_svg_dependencies()
  // ❌ on_svg_changed()
  // ❌ _register_svg_references()
  ```

### **阶段3：实现新的简化架构** (预计3-4天)

#### 3.1 实现新的SvgManager
```cpp
class SvgManager {
private:
    HashMap<String, Ref<ImageTexture>> svg_image_cache;
    HashMap<String, Ref<SpriteFrames>> svg_animation_cache;
    
public:
    Ref<SpriteFrames> get_svg_animation(const String& anim_name, int scale) {
        String key = make_animation_key(anim_name, scale);
        
        if (svg_animation_cache.has(key)) {
            return svg_animation_cache[key];
        }
        
        // 按需创建动画
        return create_svg_animation(anim_name, scale);
    }
    
private:
    Ref<SpriteFrames> create_svg_animation(const String& anim_name, int scale);
    Ref<ImageTexture> load_svg_image_at_scale(const String& path, int scale);
    String make_image_key(const String& path, int scale);
    String make_animation_key(const String& name, int scale);
};
```

#### 3.2 实现按需加载逻辑
- [ ] 实现 `load_svg_image_at_scale()` 方法
  ```cpp
  Ref<ImageTexture> load_svg_image_at_scale(const String& path, int scale) {
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
          svg_image_cache[key] = texture;
          return texture;
      }
      
      return Ref<ImageTexture>();
  }
  ```

#### 3.3 实现动画创建逻辑
- [ ] 区分单图片和多帧动画
  ```cpp
  Ref<SpriteFrames> create_svg_animation(const String& anim_name, int scale) {
      // 1. 检查是否为单图片路径 (以.svg结尾)
      if (anim_name.ends_with(".svg")) {
          return create_single_image_animation(anim_name, scale);
      }
      
      // 2. 否则从资源管理器获取动画帧列表
      return create_multi_frame_animation(anim_name, scale);
  }
  ```

### **阶段4：简化SpxSprite集成** (预计2-3天)

#### 4.1 实现统一的动画设置接口
```cpp
class SpxSprite {
public:
    void set_animation(const String& anim_name, int scale = 1) {
        if (current_animation_name == anim_name && current_scale == scale) {
            return; // 无需更新
        }
        
        current_animation_name = anim_name;
        current_scale = scale;
        
        // 从SVG管理器获取动画数据
        if (is_svg_animation(anim_name)) {
            auto svg_frames = svgMgr->get_svg_animation(anim_name, scale);
            if (svg_frames.is_valid()) {
                anim2d->set_sprite_frames(svg_frames);
                anim2d->set_animation(anim_name);
            }
        } else {
            // 普通动画处理
            // ... 现有逻辑
        }
    }
    
    void set_single_image(const String& image_path, int scale = 1) {
        // 将单图片视为特殊动画
        set_animation(image_path, scale);
    }
};
```

#### 4.2 简化缩放响应
```cpp
void SpxSprite::set_render_scale(GdVec2 new_scale) {
    _render_scale = new_scale;
    
    // 计算所需的SVG缩放级别
    int required_scale = calculate_required_svg_scale(new_scale);
    
    // 如果缩放级别变化，更新SVG动画
    if (required_scale != current_scale && is_svg_animation(current_animation_name)) {
        set_animation(current_animation_name, required_scale);
    }
    
    anim2d->set_scale(new_scale);
}

private:
int calculate_required_svg_scale(GdVec2 render_scale) {
    float max_scale = MAX(render_scale.x, render_scale.y);
    if (max_scale <= 1.0f) return 1;
    if (max_scale <= 2.0f) return 2;
    if (max_scale <= 4.0f) return 4;
    return 8; // 最大支持8倍
}
```

### **阶段5：清理和优化** (预计2天)

#### 5.1 清理SpxResMgr中的SVG动画管理
- [ ] 移除复杂的 `AnimationInfo` 结构
- [ ] 简化 `create_animation()` 方法，移除SVG特殊处理
- [ ] 删除缩放动画相关方法 (`create_scaled_animation_if_needed`, etc.)

#### 5.2 更新资源加载流程
- [ ] 修改 `load_texture()` 方法，简化SVG处理
  ```cpp
  Ref<Texture2D> SpxResMgr::load_texture(String path, GdBool direct) {
      if (path.to_lower().ends_with(".svg")) {
          // 单SVG图片通过SVG管理器处理
          return svgMgr->get_svg_image(path, 1); // 默认1倍缩放
      }
      
      // 非SVG图片用原有逻辑
      // ...
  }
  ```

#### 5.3 清理全局定义和宏
- [ ] 移除不需要的头文件包含
- [ ] 清理未使用的宏定义
- [ ] 整理include依赖关系

## 📊 **预期简化效果**

### **代码量减少**
- 删除整个文件: `svg_observer.h`, `svg_resource.h/cpp` (~400行)
- `svg_mgr.h/cpp`: ~283行 → ~120行 (-58%)
- `spx_sprite.h/cpp`: SVG相关代码 ~500行 → ~100行 (-80%)
- `spx_res_mgr.cpp`: SVG相关代码 ~200行 → ~50行 (-75%)

### **架构简化**
- **核心类**: 4个 → 1个 (只保留SvgManager)
- **主要概念**: 观察者、依赖管理、批量操作、多模式 → 两个HashMap
- **API方法**: ~25个 → ~6个

### **数据结构简化**
```cpp
// 现在: 复杂的多层结构
SvgManager → SvgResource → observers + scale_textures
SpxSprite → SvgDependency[] + display_mode + paths

// 重构后: 简单的两个映射表
SvgManager → image_cache["scale@path"] + animation_cache["scale@name"]  
SpxSprite → current_animation_name + current_scale
```

## ⚠️ **关键实现细节**

### **Key格式约定**
- 图片Key: `"2@res://sprites/player.svg"`
- 动画Key: `"4@walk"` 或 `"2@res://sprites/bullet.svg"`

### **缩放级别策略**
- 支持缩放: 1, 2, 4, 8 (int类型)
- 选择策略: 向上取最近的可用级别

### **动画判断逻辑**
- 如果anim_name以`.svg`结尾 → 单图片动画
- 否则 → 从SpxResMgr查询多帧动画定义

---

**总预计时间**: 10-14个工作日  
**代码减少**: ~60-70%  
**维护复杂度**: 显著降低 