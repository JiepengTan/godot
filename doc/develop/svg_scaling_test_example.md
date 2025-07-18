# SVG 自动缩放测试示例

## 测试目的

验证新实现的 SVG 自动缩放功能是否正确工作，包括：
1. 单个 SVG 精灵的缩放
2. SVG 动画序列的缩放
3. 2的幂次缩放级别算法
4. 渐进式缩放阈值
5. 最大缩放级别限制

## 测试环境准备

### 1. 准备测试资源
- `test_single.svg` - 单个 SVG 图像文件
- `test_anim_frame1.svg`, `test_anim_frame2.svg`, `test_anim_frame3.svg` - 动画帧 SVG 文件

### 2. 配置测试场景
```cpp
// 创建测试场景
auto sprite_mgr = SpxEngine::get_singleton()->get_sprite();

// 设置全局配置
sprite_mgr->set_global_svg_scale_threshold(1.5f);
sprite_mgr->set_global_max_svg_scale_level(8);  // 最大8倍缩放
sprite_mgr->set_global_enable_svg_animation_scaling(true);
```

## 测试用例

### 测试用例 1: 单个 SVG 精灵缩放

```cpp
void test_single_svg_scaling() {
    // 创建精灵并设置 SVG 纹理
    auto sprite_id = sprite_mgr->create_sprite("");
    sprite_mgr->set_texture(sprite_id, "test_single.svg");
    
    // 初始缩放应该是 1.0
    auto sprite = sprite_mgr->get_sprite(sprite_id);
    assert(sprite->current_svg_scale == 1.0f);
    
    // 设置缩放到 1.4 (低于阈值 1.5)
    sprite_mgr->set_render_scale(sprite_id, GdVec2(1.4f, 1.4f));
    // SVG 缩放应该保持 1.0
    assert(sprite->current_svg_scale == 1.0f);
    
    // 设置缩放到 1.6 (超过阈值)
    sprite_mgr->set_render_scale(sprite_id, GdVec2(1.6f, 1.6f));
    // SVG 缩放应该升级到 2.0
    assert(sprite->current_svg_scale == 2.0f);
    
    // 设置缩放到 3.5
    sprite_mgr->set_render_scale(sprite_id, GdVec2(3.5f, 3.5f));
    // SVG 缩放应该升级到 4.0
    assert(sprite->current_svg_scale == 4.0f);
    
    // 设置缩放到 10.0 (超过最大级别 8)
    sprite_mgr->set_render_scale(sprite_id, GdVec2(10.0f, 10.0f));
    // SVG 缩放应该限制在 8.0
    assert(sprite->current_svg_scale == 8.0f);
    
    print_line("单个 SVG 精灵缩放测试通过");
}
```

### 测试用例 2: SVG 动画序列缩放

```cpp
void test_svg_animation_scaling() {
    // 创建动画
    auto sprite_id = sprite_mgr->create_sprite("");
    auto res_mgr = SpxEngine::get_singleton()->get_res();
    
    // 创建包含 SVG 帧的动画
    res_mgr->create_animation("TestSprite", "walk", 
        "test_anim_frame1.svg,test_anim_frame2.svg,test_anim_frame3.svg", 
        10, false);
    
    // 播放动画
    sprite_mgr->play_anim(sprite_id, "walk", 1.0f, true, false);
    
    auto sprite = sprite_mgr->get_sprite(sprite_id);
    
    // 检查动画缩放记录初始化
    assert(sprite->animation_svg_scales.get("walk", 1.0f) == 1.0f);
    
    // 设置缩放到 2.5
    sprite_mgr->set_render_scale(sprite_id, GdVec2(2.5f, 2.5f));
    // 动画缩放应该升级到 4.0
    assert(sprite->animation_svg_scales.get("walk", 1.0f) == 4.0f);
    
    print_line("SVG 动画序列缩放测试通过");
}
```

### 测试用例 3: 缩放级别算法测试

```cpp
void test_scale_level_algorithm() {
    auto sprite_id = sprite_mgr->create_sprite("");
    sprite_mgr->set_texture(sprite_id, "test_single.svg");
    auto sprite = sprite_mgr->get_sprite(sprite_id);
    
    // 测试 2 的幂次缩放级别
    assert(sprite->_calculate_optimal_scale_level(0.5f) == 1.0f);  // 不缩小
    assert(sprite->_calculate_optimal_scale_level(1.0f) == 1.0f);
    assert(sprite->_calculate_optimal_scale_level(1.5f) == 2.0f);
    assert(sprite->_calculate_optimal_scale_level(2.0f) == 2.0f);
    assert(sprite->_calculate_optimal_scale_level(2.5f) == 4.0f);
    assert(sprite->_calculate_optimal_scale_level(4.0f) == 4.0f);
    assert(sprite->_calculate_optimal_scale_level(5.0f) == 8.0f);
    
    // 测试最大级别限制
    sprite->set_max_svg_scale_level(4);
    assert(sprite->_calculate_optimal_scale_level(10.0f) == 4.0f);
    
    print_line("缩放级别算法测试通过");
}
```

### 测试用例 4: 阈值测试

```cpp
void test_scaling_threshold() {
    auto sprite_id = sprite_mgr->create_sprite("");
    sprite_mgr->set_texture(sprite_id, "test_single.svg");
    auto sprite = sprite_mgr->get_sprite(sprite_id);
    
    // 设置阈值为 2.0
    sprite->set_svg_scale_threshold(2.0f);
    
    // 测试阈值逻辑
    assert(sprite->_should_upgrade_scale(1.0f, 1.5f) == false);  // 1.5 < 1.0 * 2.0
    assert(sprite->_should_upgrade_scale(1.0f, 2.0f) == true);   // 2.0 >= 1.0 * 2.0
    assert(sprite->_should_upgrade_scale(2.0f, 3.5f) == false);  // 3.5 < 2.0 * 2.0
    assert(sprite->_should_upgrade_scale(2.0f, 4.0f) == true);   // 4.0 >= 2.0 * 2.0
    
    print_line("缩放阈值测试通过");
}
```

### 测试用例 5: Transform 变化触发测试

```cpp
void test_transform_change_trigger() {
    auto sprite_id = sprite_mgr->create_sprite("");
    sprite_mgr->set_texture(sprite_id, "test_single.svg");
    auto sprite = sprite_mgr->get_sprite(sprite_id);
    
    // 通过 set_scale 触发
    sprite_mgr->set_scale(sprite_id, GdVec2(2.0f, 2.0f));
    assert(sprite->current_svg_scale == 2.0f);
    
    // 通过直接设置渲染缩放触发
    sprite_mgr->set_render_scale(sprite_id, GdVec2(3.0f, 3.0f));
    assert(sprite->current_svg_scale == 4.0f);
    
    print_line("Transform 变化触发测试通过");
}
```

## 性能测试

### 内存使用测试
```cpp
void test_memory_usage() {
    // 创建多个不同缩放级别的 SVG 精灵
    Vector<GdObj> sprites;
    
    for (int i = 0; i < 100; i++) {
        auto sprite_id = sprite_mgr->create_sprite("");
        sprite_mgr->set_texture(sprite_id, "test_single.svg");
        
        // 设置不同的缩放级别
        float scale = 1.0f + (i % 8);
        sprite_mgr->set_render_scale(sprite_id, GdVec2(scale, scale));
        
        sprites.push_back(sprite_id);
    }
    
    // 检查缓存使用情况
    auto res_mgr = SpxEngine::get_singleton()->get_res();
    // 这里可以添加内存使用检查逻辑
    
    print_line("内存使用测试完成");
}
```

### 缩放响应时间测试
```cpp
void test_scaling_performance() {
    auto sprite_id = sprite_mgr->create_sprite("");
    sprite_mgr->set_texture(sprite_id, "test_single.svg");
    
    auto start_time = Time::get_singleton()->get_time_dict_from_system();
    
    // 执行多次缩放操作
    for (int i = 0; i < 1000; i++) {
        float scale = 1.0f + (i % 16);
        sprite_mgr->set_render_scale(sprite_id, GdVec2(scale, scale));
    }
    
    auto end_time = Time::get_singleton()->get_time_dict_from_system();
    
    print_line("缩放性能测试完成");
}
```

## 运行测试

```cpp
void run_all_svg_scaling_tests() {
    print_line("开始 SVG 自动缩放功能测试...");
    
    test_single_svg_scaling();
    test_svg_animation_scaling();
    test_scale_level_algorithm();
    test_scaling_threshold();
    test_transform_change_trigger();
    test_memory_usage();
    test_scaling_performance();
    
    print_line("所有 SVG 自动缩放功能测试完成!");
}
```

## 预期结果

1. **单个 SVG 精灵**: 应该根据缩放级别自动调整分辨率
2. **动画序列**: 所有帧应该统一缩放
3. **2的幂次级别**: 缩放级别应该是 1, 2, 4, 8, 16 等
4. **阈值控制**: 只有超过阈值时才升级缩放
5. **最大级别限制**: 不应该超过设定的最大缩放级别
6. **性能**: 缩放操作应该响应迅速，内存使用合理

## 故障排除

### 常见问题
1. **SVG 文件路径错误**: 确保 SVG 文件在正确的资源路径下
2. **缓存问题**: 如果缩放不生效，检查纹理缓存机制
3. **阈值设置**: 确保阈值设置合理，不要过高或过低
4. **动画配置**: 确保动画缩放功能已启用

### 调试信息
在代码中添加调试输出来跟踪缩放过程：
```cpp
print_line("SVG缩放: 当前级别=" + String::num(current_svg_scale) + 
          ", 目标缩放=" + String::num(max_scale) + 
          ", 新级别=" + String::num(new_scale));
``` 