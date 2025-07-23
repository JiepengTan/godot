# SVG 自动缩放策略文档

## 概述

本文档描述了 SVG 纹理自动分辨率缩放系统的改进策略，旨在解决当精灵放大时 SVG 图像模糊的问题。

### 核心设计原则
1. **只支持放大**：不支持缩小分辨率，确保图像质量
2. **全局共享**：同一个 SVG 文件只对应一个 Image 对象，所有精灵共享同一个高分辨率版本
3. **最大分辨率策略**：使用当前所有引用该 SVG 的精灵中最大的分辨率需求
4. **数据更新而非对象替换**：Image 引用不变，更新的是其内部图片数据
5. **SvgScaleLevel 管理**：使用 2 的幂次缩放级别，避免频繁分辨率抖动

## 当前实现分析

### 已有功能
- `SpxSprite::_check_and_update_svg_scale()` - 检查并更新 SVG 缩放
- 基础缓存机制 - 按路径缓存纹理
- 全局和单个精灵的缩放阈值配置

### 需要重新设计的部分
1. **缓存策略不合适** - 当前使用 `path@scale` 缓存多个版本，需要改为单一版本全局共享
2. **缺乏全局引用跟踪** - 无法知道哪些精灵在使用同一个 SVG
3. **触发时机不完整** - 只在 `set_render_scale()` 时触发
4. **缺乏最大分辨率需求计算** - 无法确定所有引用中的最大缩放需求
5. **纹理替换而非数据更新** - 当前创建新纹理对象而非更新现有数据

## 改进策略

### 阶段一：建立全局 SVG 管理系统

#### 目标
建立全局的 SVG 引用跟踪和分辨率管理系统，确保同一个 SVG 文件的所有引用共享相同的高分辨率版本。

#### 实现要点
1. **全局 SVG 引用跟踪器**：
   ```cpp
   class SvgManager {
   private:
       struct SvgInfo {
           String path;
           Ref<ImageTexture> texture;  // 全局共享的纹理对象
           float current_scale_level;  // 当前分辨率级别
           HashSet<SpxSprite*> references;  // 所有引用这个SVG的精灵
       };
       HashMap<String, SvgInfo> svg_registry;  // path -> SvgInfo
   };
   ```

2. **引用注册和注销**：
   - 精灵设置 SVG 纹理时注册引用
   - 精灵销毁时自动注销引用
   - 支持动画帧的批量引用管理

3. **触发点扩展**：
   - `set_scale()` / `set_render_scale()` 时检查
   - `_notification(NOTIFICATION_TRANSFORM_CHANGED)` 时检查
   - 精灵创建/销毁时更新引用

### 阶段二：最大分辨率需求计算和数据更新

#### 目标
实现基于所有引用精灵的最大分辨率需求计算，并通过更新纹理数据（而非替换对象）来升级分辨率。

#### 实现要点
1. **最大分辨率需求计算**：
   ```cpp
   float calculate_max_required_scale(const SvgInfo& svg_info) {
       float max_scale = 1.0f;
       for (SpxSprite* sprite : svg_info.references) {
           Vector2 actual_scale = sprite->get_actual_render_scale();
           float sprite_max = MAX(actual_scale.x, actual_scale.y);
           max_scale = MAX(max_scale, sprite_max);
       }
       return max_scale;
   }
   ```

2. **SvgScaleLevel 管理**：
   ```cpp
   float calculate_optimal_scale_level(float required_scale) {
       if (required_scale <= 1.0f) return 1.0f;
       
       float level = 1.0f;
       while (level < required_scale && level < max_svg_scale_level) {
           level *= 2.0f;
       }
       return level;
   }
   ```

3. **纹理数据更新策略**：
   - 保持 ImageTexture 对象引用不变
   - 重新加载 SVG 为新分辨率的 Image
   - 调用 `texture->set_image(new_image)` 更新数据
   - 所有引用此纹理的精灵自动获得高分辨率版本

### 阶段四：扩展对动画序列的支持

#### 目标
将动画序列中的每个 SVG 帧都纳入全局管理系统，确保动画播放时也能享受到自动分辨率缩放。

#### 实现要点
1. **动画帧 SVG 注册**：
   ```cpp
   void register_animation_svg_frames(const String& anim_name, SpxSprite* sprite) {
       auto frames = sprite->get_sprite_frames();
       int frame_count = frames->get_frame_count(anim_name);
       
       for (int i = 0; i < frame_count; i++) {
           auto texture = frames->get_frame_texture(anim_name, i);
           String svg_path = extract_svg_path(texture);
           if (!svg_path.is_empty()) {
               svg_mgr->register_reference(svg_path, sprite);
           }
       }
   }
   ```

2. **简化的批量更新**：
   - 由于全局共享机制，SVG 数据更新会自动影响所有动画帧
   - 不需要单独更新每个动画帧，只需更新对应的 SVG 纹理数据
   - 动画播放无需特殊处理，自动使用最新的高分辨率数据

3. **动画引用生命周期管理**：
   - 播放动画时批量注册所有 SVG 帧的引用
   - 停止动画或切换动画时注销旧的引用
   - 确保引用计数的准确性

## 技术实现细节

### 全局 SVG 管理系统

```cpp
class SvgManager {
private:
    struct SvgInfo {
        String path;
        Ref<ImageTexture> texture;      // 全局共享的纹理对象
        float current_scale_level;      // 当前分辨率级别 (1, 2, 4, 8, 16...)
        HashSet<SpxSprite*> references; // 所有引用这个SVG的精灵
        
        // 计算所有引用中的最大缩放需求
        float get_max_required_scale() const {
            float max_scale = 1.0f;
            for (SpxSprite* sprite : references) {
                Vector2 actual_scale = sprite->get_actual_render_scale();
                max_scale = MAX(max_scale, MAX(actual_scale.x, actual_scale.y));
            }
            return max_scale;
        }
    };
    
    HashMap<String, SvgInfo> svg_registry;  // path -> SvgInfo
    float scale_threshold = 1.5f;           // 升级阈值
    int max_scale_level = 16;               // 最大缩放级别
    
public:
    void register_reference(const String& svg_path, SpxSprite* sprite);
    void unregister_reference(const String& svg_path, SpxSprite* sprite);
    void check_and_update_svg_scale(const String& svg_path);
    void on_sprite_scale_changed(SpxSprite* sprite);
    
private:
    float calculate_optimal_scale_level(float required_scale);
    void update_svg_texture_data(SvgInfo& svg_info, float new_scale);
};
```

### 性能优势和考虑

1. **内存效率显著提升**：
   - 每个 SVG 文件只保存一个最高分辨率版本，大幅减少内存占用
   - 不再需要为不同缩放级别缓存多个纹理副本
   - 自动引用计数，未使用的 SVG 可以及时释放

2. **更新性能优化**：
   - 纹理数据更新比创建新纹理对象更高效
   - 批量更新：一次 SVG 数据更新影响所有引用精灵
   - 避免了纹理对象替换带来的引用更新开销

3. **检查频率控制**：
   - 基于阈值的升级策略，避免频繁更新
   - 增量检查：只在精灵缩放变化时触发
   - 全局管理避免了重复计算

## 配置选项

### 全局配置（简化设计）
```cpp
// 在 SvgManager 中
float global_svg_scale_threshold = 1.5f;   // 升级阈值
int global_max_svg_scale_level = 16;       // 最大缩放级别 (2^4)
bool enable_svg_auto_scaling = true;       // 总开关

// 支持的缩放级别
static const float SCALE_LEVELS[] = {1.0f, 2.0f, 4.0f, 8.0f, 16.0f};
```

### 精灵级别控制（可选）
```cpp
// 在 SpxSprite 中 - 用于特殊需求
bool participate_in_svg_scaling = true;    // 是否参与 SVG 缩放计算
```

### 运行时调整
```cpp
// 提供运行时调整接口
svg_mgr->set_scale_threshold(2.0f);
svg_mgr->set_max_scale_level(8);
svg_mgr->enable_auto_scaling(false);  // 暂时禁用
```

## 实施计划

### 阶段一实施步骤：全局管理系统
1. **创建 SvgManager 类**
   - 实现 SvgInfo 结构体和引用跟踪
   - 添加 register/unregister_reference 方法
   - 集成到 SpxResMgr 中

2. **修改纹理加载流程**
   - 更新 `load_texture()` 使用全局管理器
   - 移除旧的 `path@scale` 缓存机制
   - 确保 SVG 纹理唯一性

3. **添加引用生命周期管理**
   - 在 `set_texture()` 时注册引用
   - 在精灵销毁时自动注销引用
   - 实现引用计数和清理逻辑

### 阶段二实施步骤：分辨率计算和更新
1. **实现最大分辨率需求计算**
   - 添加 `get_actual_render_scale()` 方法
   - 实现 `get_max_required_scale()` 逻辑
   - 考虑全局变换和相机缩放

2. **实现 SvgScaleLevel 管理**
   - 2 的幂次缩放级别算法
   - 阈值检查避免频繁更新
   - 最大缩放级别限制

3. **实现纹理数据更新**
   - 使用 `texture->set_image()` 更新数据
   - 确保所有引用自动获得新数据
   - 添加错误处理和回退机制

### 阶段四实施步骤：动画支持
1. **扩展引用注册到动画帧**
   - 检测动画中的 SVG 帧
   - 批量注册所有帧的引用
   - 处理动画切换时的引用更新

2. **简化动画更新流程**
   - 移除单独的动画帧更新逻辑
   - 依赖全局共享机制自动更新
   - 确保动画播放的平滑性

## 测试策略

### 功能测试
1. 单个 SVG 精灵缩放测试
2. 动画序列缩放测试
3. 多精灵同时缩放测试
4. 极限缩放测试（大倍数放大）

### 性能测试
1. 内存使用监控
2. 缩放响应时间测试
3. 大量精灵场景测试
4. 长时间运行稳定性测试

## 未来扩展

### 阶段三：实时监测（未来实现）
- 基于可见性的优先级检测
- 定期检查机制
- 性能自适应调整

### 高级功能（未来考虑）
- SVG 质量预设（性能/质量平衡）
- 用户自定义缩放策略
- GPU 加速的 SVG 渲染
- 预测性缓存机制

## 注意事项

1. **只支持放大策略**：系统不会缩小 SVG 分辨率，确保图像质量不会因为缩小而降低
2. **全局共享机制**：同一个 SVG 文件的所有引用共享相同的纹理对象，修改会影响所有使用者
3. **内存使用特点**：
   - 优点：每个 SVG 只保存一个版本，大幅减少内存占用
   - 注意：会使用所有引用中的最大分辨率，可能比某些精灵实际需求更高
4. **引用生命周期**：必须正确管理精灵的引用注册和注销，避免内存泄漏或悬空引用
5. **兼容性保证**：确保改进不影响现有的非 SVG 纹理功能和动画系统
6. **性能监控重点**：
   - 监控全局管理器的引用跟踪开销
   - 观察纹理数据更新的频率和性能影响
   - 跟踪内存使用情况，确保达到预期的节省效果 