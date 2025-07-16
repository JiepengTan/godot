# Godot MovieWriter 视频录制原理分析

## 概述

MovieWriter 是 Godot 引擎的视频录制系统，支持非实时高质量视频录制。它可以录制游戏运行过程中的视频和音频，并输出为多种格式的文件。

## 1. 整体架构

### 1.1 类层次结构

MovieWriter 采用抽象工厂模式设计：

```cpp
// 抽象基类
class MovieWriter : public Object {
    // 核心接口方法
    virtual Error write_begin(const Size2i &p_movie_size, uint32_t p_fps, const String &p_base_path);
    virtual Error write_frame(const Ref<Image> &p_image, const int32_t *p_audio_data);
    virtual void write_end();
    virtual bool handles_file(const String &p_path) const;
};

// 具体实现类
class MovieWriterMJPEG : public MovieWriter;  // AVI 格式录制器
class MovieWriterPNGWAV : public MovieWriter;  // PNG 序列录制器
```

### 1.2 工厂注册机制

```cpp
// 在 servers/register_server_types.cpp 中注册
writer_mjpeg = memnew(MovieWriterMJPEG);
MovieWriter::add_writer(writer_mjpeg);

writer_pngwav = memnew(MovieWriterPNGWAV);
MovieWriter::add_writer(writer_pngwav);
```

### 1.3 录制器选择

通过文件扩展名自动选择合适的录制器：

```cpp
MovieWriter *MovieWriter::find_writer_for_file(const String &p_file) {
    for (int32_t i = writer_count - 1; i >= 0; i--) {
        if (writers[i]->handles_file(p_file)) {
            return writers[i];
        }
    }
    return nullptr;
}
```

## 2. 录制流程

### 2.1 初始化阶段

在 `main.cpp` 的 `Main::setup2()` 中检测录制参数：

```cpp
if (Engine::get_singleton()->get_write_movie_path() != String()) {
    movie_writer = MovieWriter::find_writer_for_file(Engine::get_singleton()->get_write_movie_path());
    if (movie_writer == nullptr) {
        ERR_PRINT("Can't find movie writer for file type, aborting: " + Engine::get_singleton()->get_write_movie_path());
        Engine::get_singleton()->set_write_movie_path(String());
    }
}
```

### 2.2 录制循环

在主循环 `Main::iteration()` 的每一帧末尾：

```cpp
if (movie_writer) {
    movie_writer->add_frame();
}
```

### 2.3 关键方法：add_frame()

```cpp
void MovieWriter::add_frame() {
    // 1. 获取当前帧图像
    RID main_vp_rid = RenderingServer::get_singleton()->viewport_find_from_screen_attachment(DisplayServer::MAIN_WINDOW_ID);
    RID main_vp_texture = RenderingServer::get_singleton()->viewport_get_texture(main_vp_rid);
    Ref<Image> vp_tex = RenderingServer::get_singleton()->texture_2d_get(main_vp_texture);
    
    // 2. HDR 格式转换
    if (RenderingServer::get_singleton()->viewport_is_using_hdr_2d(main_vp_rid)) {
        vp_tex->convert(Image::FORMAT_RGBA8);
        vp_tex->linear_to_srgb();
    }
    
    // 3. 收集性能数据
    RenderingServer::get_singleton()->viewport_set_measure_render_time(main_vp_rid, true);
    cpu_time += RenderingServer::get_singleton()->viewport_get_measured_render_time_cpu(main_vp_rid);
    gpu_time += RenderingServer::get_singleton()->viewport_get_measured_render_time_gpu(main_vp_rid);
    
    // 4. 获取音频数据
    AudioDriverDummy::get_dummy_singleton()->mix_audio(mix_rate / fps, audio_mix_buffer.ptr());
    
    // 5. 写入帧数据
    write_frame(vp_tex, audio_mix_buffer.ptr());
}
```

## 3. 数据采集机制

### 3.1 图像采集

**采集流程**：
1. 通过 `RenderingServer` 获取主窗口的 viewport
2. 获取 viewport 的纹理数据
3. 将纹理转换为 `Image` 对象
4. 处理 HDR 格式转换（如果需要）

**关键代码**：
```cpp
RID main_vp_rid = RenderingServer::get_singleton()->viewport_find_from_screen_attachment(DisplayServer::MAIN_WINDOW_ID);
RID main_vp_texture = RenderingServer::get_singleton()->viewport_get_texture(main_vp_rid);
Ref<Image> vp_tex = RenderingServer::get_singleton()->texture_2d_get(main_vp_texture);
```

### 3.2 音频采集

**AudioDriverDummy 的作用**：
- 替代真实的音频驱动，用于录制模式
- 不播放声音，只收集音频数据
- 支持多声道配置

**音频处理流程**：
```cpp
// 设置音频参数
mix_rate = get_audio_mix_rate();
AudioDriverDummy::get_dummy_singleton()->set_mix_rate(mix_rate);
AudioDriverDummy::get_dummy_singleton()->set_speaker_mode(AudioDriver::SpeakerMode(get_audio_speaker_mode()));

// 计算音频块大小
audio_channels = AudioDriverDummy::get_dummy_singleton()->get_channels();
audio_mix_buffer.resize(mix_rate * audio_channels / fps);

// 每帧获取音频数据
AudioDriverDummy::get_dummy_singleton()->mix_audio(mix_rate / fps, audio_mix_buffer.ptr());
```

## 4. 非实时录制特性

### 4.1 固定帧率机制

MovieWriter 的核心特色是**非实时录制**：

- 通过 `--fixed-fps` 参数强制设置固定帧率
- 每帧的 delta 时间保持一致，不受硬件性能影响
- 录制过程可能比实时播放慢，但输出视频质量稳定

### 4.2 时间控制

```cpp
// 在 Main::begin() 中设置
if (fixed_fps != -1) {
    Engine::get_singleton()->set_frame_delay(1.0 / fixed_fps * 1000000); // 微秒
}
```

### 4.3 音频同步

```cpp
// 确保音频采样率能被帧率整除
if ((mix_rate % fps) != 0) {
    WARN_PRINT("MovieWriter's audio mix rate (" + itos(mix_rate) + ") can not be divided by the recording FPS (" + itos(fps) + "). Audio may go out of sync over time.");
}
```

## 5. 具体实现详解

### 5.1 MJPEG 录制器 (MovieWriterMJPEG)

**特点**：
- 输出 AVI 格式文件
- 使用 MJPEG 压缩视频
- 音频为未压缩的 PCM 格式
- 文件大小中等，编码速度快

**实现细节**：
```cpp
Error MovieWriterMJPEG::write_frame(const Ref<Image> &p_image, const int32_t *p_audio_data) {
    // 将图像压缩为 JPEG
    Vector<uint8_t> jpg_buffer = p_image->save_jpg_to_buffer(quality);
    
    // 写入 AVI 文件结构
    f->store_buffer((const uint8_t *)"00db", 4); // 视频流标识
    f->store_32(jpg_buffer.size());
    f->store_buffer(jpg_buffer.ptr(), jpg_buffer.size());
    
    // 处理字节对齐
    if (jpg_buffer.size() & 1) {
        f->store_8(0);
    }
    
    // 写入音频数据
    f->store_buffer((const uint8_t *)"01wb", 4); // 音频流标识
    f->store_32(audio_block_size);
    f->store_buffer((const uint8_t *)p_audio_data, audio_block_size);
}
```

**AVI 文件结构**：
- RIFF 头
- AVI 头信息
- 视频流信息
- 音频流信息
- 数据块（交替存储视频和音频）
- 索引表

### 5.2 PNG+WAV 录制器 (MovieWriterPNGWAV)

**特点**：
- 输出 PNG 图像序列 + WAV 音频文件
- 无损压缩，质量最高
- 文件大小较大，编码速度慢
- 适合后期处理

**实现细节**：
```cpp
Error MovieWriterPNGWAV::write_frame(const Ref<Image> &p_image, const int32_t *p_audio_data) {
    // 保存 PNG 图像文件
    Vector<uint8_t> png_buffer = p_image->save_png_to_buffer();
    Ref<FileAccess> fi = FileAccess::open(base_path + zeros_str(frame_count) + ".png", FileAccess::WRITE);
    fi->store_buffer(png_buffer.ptr(), png_buffer.size());
    
    // 将音频数据写入 WAV 文件
    f_wav->store_buffer((const uint8_t *)p_audio_data, audio_block_size);
    
    frame_count++;
}
```

**文件命名规则**：
- PNG 文件：`output_00000.png`, `output_00001.png`, ...
- WAV 文件：`output.wav`

## 6. 使用方式

### 6.1 命令行参数

```bash
# 录制为 AVI 格式（MJPEG）
godot --write-movie output.avi --fixed-fps 60

# 录制为 PNG 序列
godot --write-movie output.png --fixed-fps 30

# 结合其他参数
godot --write-movie video.avi --fixed-fps 30 --headless
```

### 6.2 项目设置

在 `project.godot` 中可以配置：

```ini
[editor/movie_writer]
mix_rate = 48000            # 音频采样率 (Hz)
speaker_mode = 0            # 音频声道模式 (0=立体声)
mjpeg_quality = 0.75        # MJPEG 压缩质量 (0.01-1.0)
movie_file = ""             # 默认输出文件
disable_vsync = false       # 是否禁用垂直同步
fps = 60                    # 录制帧率
```

### 6.3 音频声道配置

```cpp
enum SpeakerMode {
    SPEAKER_MODE_STEREO = 0,        // 2 声道
    SPEAKER_SURROUND_31 = 1,        // 4 声道
    SPEAKER_SURROUND_51 = 2,        // 6 声道
    SPEAKER_SURROUND_71 = 3,        // 8 声道
};
```

## 7. 性能监控

### 7.1 录制统计

MovieWriter 会收集并报告详细的性能统计：

```cpp
void MovieWriter::end() {
    // 输出录制统计信息
    print_line(vformat("%d frames at %d FPS (movie length: %s), recorded in %s (%d%% of real-time speed).", 
        Engine::get_singleton()->get_frames_drawn(), fps, movie_time, real_time, 
        (float(movie_time_seconds) / real_time_seconds) * 100));
    
    print_line(vformat("CPU time: %.2f seconds (average: %.2f ms/frame)", 
        cpu_time / 1000, cpu_time / Engine::get_singleton()->get_frames_drawn()));
    
    print_line(vformat("GPU time: %.2f seconds (average: %.2f ms/frame)", 
        gpu_time / 1000, gpu_time / Engine::get_singleton()->get_frames_drawn()));
}
```

### 7.2 窗口标题显示

录制过程中会实时更新窗口标题：

```cpp
DisplayServer::get_singleton()->window_set_title(
    vformat("MovieWriter: Frame %d (time: %s) - %s", 
        Engine::get_singleton()->get_frames_drawn(), movie_time, project_name));
```

## 8. 扩展性

### 8.1 自定义录制器

可以通过继承 `MovieWriter` 类来实现自定义录制格式：

```cpp
class CustomMovieWriter : public MovieWriter {
    GDCLASS(CustomMovieWriter, MovieWriter)
    
protected:
    virtual bool handles_file(const String &p_path) const override;
    virtual Error write_begin(const Size2i &p_movie_size, uint32_t p_fps, const String &p_base_path) override;
    virtual Error write_frame(const Ref<Image> &p_image, const int32_t *p_audio_data) override;
    virtual void write_end() override;
};
```

### 8.2 GDExtension 支持

文档提到可以通过 GDExtension 实现自定义录制器，以获得更好的性能。

## 9. 技术优势

### 9.1 稳定的输出质量
- 非实时录制确保每帧都有足够时间渲染
- 固定帧率保证输出视频的时间一致性
- 不受硬件性能波动影响

### 9.2 灵活的格式支持
- 支持有损压缩（MJPEG）和无损压缩（PNG）
- 音频格式可配置（立体声到7.1环绕声）
- 可扩展的架构支持添加新格式

### 9.3 完整的音视频同步
- 音频和视频采用相同的时间基准
- 确保音视频完美同步
- 支持多声道音频录制

### 9.4 便于后期处理
- PNG 序列格式便于视频编辑
- 详细的性能统计数据
- 灵活的参数配置

## 10. 注意事项

### 10.1 磁盘空间
- 录制前会检查可用磁盘空间
- PNG 序列文件可能非常大
- 建议预留足够的存储空间

### 10.2 性能影响
- 录制过程会显著影响运行速度
- 建议在专门的录制环境中使用
- 可能需要调整项目设置以优化录制效果

### 10.3 格式选择
- AVI 格式适合直接播放和分享
- PNG 序列适合后期编辑和处理
- 根据用途选择合适的格式

## 11. 总结

Godot 的 MovieWriter 系统是一个设计精良的视频录制解决方案，通过非实时录制机制保证了高质量的输出。其模块化的架构使得系统具有良好的扩展性，同时提供了丰富的配置选项和详细的性能监控。

这个系统特别适合：
- 制作游戏宣传视频
- 录制教程和演示
- 游戏开发调试
- 自动化测试录制

通过深入理解其工作原理，开发者可以更好地利用这个工具来创建高质量的游戏视频内容。

## 12. AudioDriverDummy 技术分析 - 为什么不能同时录制和播放

### 12.1 核心技术问题

#### 单例模式限制

**架构设计问题**：
```cpp
// AudioDriver 使用单例模式，全局只能有一个活跃驱动
AudioDriver *AudioDriver::singleton = nullptr;

// 录制时强制替换为 dummy 驱动
if (Engine::get_singleton()->get_write_movie_path() != String()) {
    audio_driver_idx = AudioDriverManager::get_driver_count() - 1;  // Dummy driver
    AudioDriverDummy::get_dummy_singleton()->set_use_threads(false);
}
```

这意味着：
- 同时只能有一个音频驱动处于活跃状态
- 录制时会完全替换掉真实的音频驱动
- 无法同时使用真实驱动播放和 dummy 驱动录制

#### AudioDriverDummy 的设计本质

**Dummy 驱动 vs 真实驱动的差异**：

```cpp
// AudioDriverDummy - 只处理数据，不输出声音
void AudioDriverDummy::thread_func(void *p_udata) {
    while (!ad->exit_thread.is_set()) {
        if (ad->active.is_set()) {
            ad->audio_server_process(ad->buffer_frames, ad->samples_in);
            // 注意：处理完数据后什么都不做，不输出到音频设备
        }
        OS::get_singleton()->delay_usec(usdelay);
    }
}
```

对比真实音频驱动（以 ALSA 为例）：
```cpp
// AudioDriverALSA - 处理数据并输出到扬声器
void AudioDriverALSA::thread_func(void *p_udata) {
    while (!ad->exit_thread.is_set()) {
        ad->audio_server_process(ad->period_size, ad->samples_in.ptrw());
        
        // 关键：将音频数据写入实际音频设备
        int wrote = snd_pcm_writei(ad->pcm_handle, 
                     (void *)(src + (total * ad->channels)), todo);
    }
}
```

#### 音频数据流向分析

**正常播放流程**：
```
AudioServer → 真实音频驱动 → 音频设备 → 扬声器输出声音
```

**录制模式流程**：
```
AudioServer → AudioDriverDummy → 音频数据收集 → 录制文件
                               ↘ 无输出到音频设备
```

**核心问题**：AudioDriverDummy 只收集音频数据用于录制，**故意不输出**到音频设备。

### 12.2 为什么这样设计？

**技术考虑**：

1. **时间同步精确性**：
```cpp
// 非实时录制需要精确的时间控制
if (fixed_fps != -1) {
    Engine::get_singleton()->set_frame_delay(1.0 / fixed_fps * 1000000);
}
```

2. **避免音频冲突**：
- 防止实际音频播放的延迟影响录制同步
- 确保录制的音频数据完全可控
- 避免硬件音频设备的不确定性

3. **性能优化**：
- Dummy 驱动无需与音频硬件交互，性能更高
- 减少系统资源占用

### 12.3 解决方案

#### 方案一：混合音频驱动架构（短期方案）

```cpp
class HybridAudioDriver : public AudioDriver {
private:
    AudioDriver* real_driver;      // 实际播放驱动
    AudioDriverDummy* dummy_driver; // 录制驱动
    bool recording_mode = false;
    
public:
    virtual void audio_server_process(int p_frames, int32_t *p_buffer, 
                                      bool p_update_mix_time = true) override {
        // 处理音频数据
        if (AudioServer::get_singleton()) {
            AudioServer::get_singleton()->_driver_process(p_frames, p_buffer);
        }
        
        // 同时发送给真实驱动和录制驱动
        if (real_driver) {
            real_driver->output_to_device(p_buffer, p_frames);  // 播放声音
        }
        
        if (recording_mode && dummy_driver) {
            dummy_driver->collect_for_recording(p_buffer, p_frames);  // 收集录制
        }
    }
};
```

#### 方案二：音频分流机制

```cpp
class AudioSplitter {
private:
    std::vector<AudioDriver*> outputs;
    
public:
    void add_output(AudioDriver* driver) {
        outputs.push_back(driver);
    }
    
    void process_audio(int32_t* buffer, int frames) {
        for (auto* driver : outputs) {
            driver->process_audio_data(buffer, frames);
        }
    }
};
```

#### 方案三：AudioServer 层面的多路输出

```cpp
// 在 AudioServer 中实现
void AudioServer::_driver_process(int p_frames, int32_t *p_buffer) {
    // 现有的音频处理逻辑
    process_audio_mixing(p_frames, p_buffer);
    
    // 分发到多个输出
    if (playback_driver) {
        playback_driver->output_audio(p_buffer, p_frames);  // 播放
    }
    
    if (recording_enabled && recording_driver) {
        recording_driver->record_audio(p_buffer, p_frames);  // 录制
    }
}
```

### 12.4 短期解决方案实施计划

#### 阶段一：混合驱动基础架构（1-2 周）

1. **创建 RealtimeMovieWriter 类**：
```cpp
class RealtimeMovieWriter : public MovieWriter {
private:
    AudioDriver* backup_audio_driver = nullptr;
    bool realtime_mode = false;
    
public:
    void enable_realtime_mode(bool enable) {
        realtime_mode = enable;
        if (enable) {
            setup_hybrid_audio_driver();
        } else {
            restore_original_audio_driver();
        }
    }
    
private:
    void setup_hybrid_audio_driver();
    void restore_original_audio_driver();
};
```

2. **修改音频驱动选择逻辑**：
```cpp
// 在 main.cpp 中
if (Engine::get_singleton()->get_write_movie_path() != String()) {
    if (realtime_recording_enabled) {
        // 使用混合驱动，保持音频播放
        setup_realtime_recording();
    } else {
        // 原有逻辑：使用 dummy 驱动
        audio_driver_idx = AudioDriverManager::get_driver_count() - 1;
        AudioDriverDummy::get_dummy_singleton()->set_use_threads(false);
    }
}
```

#### 阶段二：录制模式切换（1 周）

1. **添加录制模式配置**：
```cpp
// project.godot 配置
GLOBAL_DEF("movie_writer/realtime_mode", false);
GLOBAL_DEF("movie_writer/enable_audio_playback", true);
```

2. **用户界面改进**：
```cpp
// 录制控制面板
class RecordingControlPanel : public Control {
    CheckBox* realtime_mode_checkbox;
    CheckBox* audio_playback_checkbox;
    Button* start_recording_button;
    Label* status_label;
};
```

#### 阶段三：测试和优化（1 周）

1. **性能测试**：
- 对比离线录制和实时录制的性能差异
- 测试音频同步精度
- 验证音频质量

2. **兼容性测试**：
- 不同平台的音频驱动兼容性
- 各种音频格式支持
- 硬件配置适应性

### 12.5 配置选项

```cpp
// 新增项目设置
GLOBAL_DEF("movie_writer/mode", "offline");  // "offline" 或 "realtime"
GLOBAL_DEF("movie_writer/realtime/enable_audio_playback", true);
GLOBAL_DEF("movie_writer/realtime/audio_buffer_size", 1024);
GLOBAL_DEF("movie_writer/realtime/sync_tolerance_ms", 5);
```

### 12.6 使用示例

```bash
# 实时录制（可听到声音）
godot --write-movie output.avi --realtime-recording

# 传统离线录制（无声音，高质量）
godot --write-movie output.avi --fixed-fps 60
```

### 12.7 性能影响评估

**实时录制模式**：
- CPU 使用率：+15-25%（需要同时处理播放和录制）
- 内存使用：+50-100MB（音频缓冲区）
- 录制质量：可能有轻微帧丢失

**离线录制模式**：
- CPU 使用率：保持当前水平
- 内存使用：保持当前水平  
- 录制质量：最高质量，无帧丢失

### 12.8 总结

AudioDriverDummy 不能同时录制和播放声音的根本原因是：

1. **架构限制**：单例音频驱动设计，无法同时运行多个驱动
2. **设计目的**：Dummy 驱动专门设计为"不播放声音"，只收集数据
3. **同步需求**：非实时录制需要完全控制音频时序，避免硬件干扰

**短期解决方案**通过混合音频驱动架构，在保持现有高质量离线录制功能的基础上，添加实时录制模式，让用户可以在录制时听到声音，满足实时预览和调试的需求。

## 13. 深度技术分析 - 常见问题解答

### 13.1 为什么选择 AVI 格式而不是 MP4？

从代码分析可以看出，Godot 选择 AVI 格式的原因：

#### 技术实现角度

**AVI 格式优势**：
```cpp
// 代码中的注释显示这是快速实现
// Quick & Dirty MJPEG Code based on - https://docs.microsoft.com/en-us/windows/win32/directshow/avi-riff-file-reference
```

- **实现简单**：AVI 使用 RIFF 容器格式，结构相对简单
- **MJPEG 标准容器**：AVI 是 MJPEG 编码的标准容器格式
- **开发效率**：快速实现，代码量少，维护成本低
- **兼容性好**：大多数媒体播放器都支持 AVI 格式

#### MP4 格式的复杂性

**MP4 实现挑战**：
- **复杂的原子结构**：需要处理 ftyp、moov、mdat 等复杂的原子结构
- **时间戳管理**：需要精确的时间戳计算和 DTS/PTS 处理
- **索引表复杂**：需要构建复杂的索引表（stbl、stco、stsc 等）
- **内存管理**：需要更复杂的内存管理和缓冲机制

```cpp
// AVI 的简单结构示例
f->store_buffer((const uint8_t *)"RIFF", 4);
f->store_32(total_size);
f->store_buffer((const uint8_t *)"AVI ", 4);
// 相比之下，MP4 需要处理复杂的原子嵌套结构
```

#### 使用场景考虑

- **非实时录制**：对文件大小不敏感，更注重实现简单性
- **后期处理**：通常需要用 FFmpeg 等工具进行二次处理
- **开发资源**：游戏引擎的重点在渲染和游戏逻辑，视频录制是辅助功能

### 13.2 为什么音频不进行压缩？

#### 代码证据

```cpp
// 音频格式设置为标准 PCM
f->store_16(1); // compression code, standard PCM
f->store_16(channels);
f->store_32(mix_rate);

// 音频数据直接写入，无压缩处理
f->store_buffer((const uint8_t *)p_audio_data, audio_block_size);
```

#### 技术原因分析

**1. 简化实现**：
- PCM 是最简单的音频格式，无需额外编码库
- 避免引入 AAC、MP3 等编码器的复杂性
- 减少第三方依赖和许可证问题

**2. 确保音视频同步**：
- 未压缩音频消除了编码延迟
- 固定的音频块大小便于同步计算
- 避免了可变码率音频的时间戳问题

**3. 质量考虑**：
- 保证最佳音频质量，适合专业后期处理
- 避免有损压缩带来的质量损失
- 48kHz/32bit 的高质量音频输出

**4. 实时性考虑**：
```cpp
// 每帧的音频数据大小是固定的
audio_block_size = (mix_rate / fps) * blockalign;
```

### 13.3 OBS 录屏文件小的技术原理

#### OBS 的先进技术

**1. 现代视频编码**：
- **H.264/H.265**：比 MJPEG 压缩效率高 10-50 倍
- **帧间压缩**：利用连续帧之间的相关性
- **运动估计**：只编码变化的区域

**2. 硬件加速**：
- **NVENC**：NVIDIA GPU 硬件编码器
- **QuickSync**：Intel 集成显卡硬件编码
- **AMF**：AMD GPU 硬件编码器

**3. 智能编码策略**：
- **变码率编码**：根据画面复杂度动态调整码率
- **自适应量化**：重要区域使用更高质量
- **B帧和P帧**：减少冗余数据

#### Godot vs OBS 对比

| 特性 | Godot MovieWriter | OBS Studio |
|------|-------------------|------------|
| 视频编码 | MJPEG (帧内压缩) | H.264/H.265 (帧间压缩) |
| 音频编码 | PCM (无压缩) | AAC/MP3 (高效压缩) |
| 硬件加速 | 无 | 支持多种硬件编码器 |
| 实时性 | 非实时 | 实时录制 |
| 文件大小 | 较大 | 较小 |
| 质量控制 | 固定质量 | 智能码率控制 |

#### 压缩效率示例

```
场景：1080p 60fps 游戏录制 10 分钟

Godot (MJPEG):
- 视频: ~2-5 GB
- 音频: ~500 MB
- 总计: ~3-6 GB

OBS (H.264):
- 视频: ~200-500 MB
- 音频: ~10-20 MB
- 总计: ~300-600 MB
```

### 13.4 Web 平台视频录制支持

#### 当前限制

从代码分析可以看出，Web 平台存在以下限制：

**1. 文件系统限制**：
```cpp
// Web 平台的文件访问受限
Error OS_Web::execute(const String &p_path, const List<String> &p_arguments, String *r_pipe, int *r_exitcode, bool read_stderr, Mutex *p_pipe_mutex, bool p_open_console) {
    return create_process(p_path, p_arguments);
}
```

**2. 内存限制**：
- 浏览器内存限制可能导致大文件录制失败
- 无法像桌面应用一样直接写入文件系统

**3. API 限制**：
- 缺少原生文件写入 API
- 需要通过特殊的 Web API 处理

#### 解决方案

**1. 使用现代 Web API**：

```javascript
// MediaRecorder API 实现
class WebMovieWriter {
    constructor(canvas, options = {}) {
        this.canvas = canvas;
        this.stream = canvas.captureStream(options.fps || 30);
        this.recorder = new MediaRecorder(this.stream, {
            mimeType: 'video/webm;codecs=vp8,opus',
            videoBitsPerSecond: options.videoBitrate || 2500000
        });
        this.chunks = [];
    }
    
    start() {
        this.recorder.ondataavailable = (event) => {
            this.chunks.push(event.data);
        };
        this.recorder.start();
    }
    
    stop() {
        return new Promise((resolve) => {
            this.recorder.onstop = () => {
                const blob = new Blob(this.chunks, { type: 'video/webm' });
                resolve(blob);
            };
            this.recorder.stop();
        });
    }
}
```

**2. 分块存储策略**：

```javascript
// 使用 IndexedDB 存储大文件
class IndexedDBMovieWriter {
    async storeChunk(chunkData, chunkIndex) {
        const db = await this.openDB();
        const transaction = db.transaction(['chunks'], 'readwrite');
        const store = transaction.objectStore('chunks');
        await store.put({
            index: chunkIndex,
            data: chunkData,
            timestamp: Date.now()
        });
    }
    
    async exportMovie() {
        const chunks = await this.getAllChunks();
        const blob = new Blob(chunks.map(c => c.data), { type: 'video/webm' });
        this.downloadBlob(blob, 'recording.webm');
    }
}
```

**3. File System Access API**：

```javascript
// 现代浏览器的文件系统访问
class FileSystemMovieWriter {
    async selectOutputFile() {
        const fileHandle = await window.showSaveFilePicker({
            suggestedName: 'recording.webm',
            types: [{
                description: 'WebM videos',
                accept: { 'video/webm': ['.webm'] }
            }]
        });
        return fileHandle;
    }
    
    async writeChunk(fileHandle, chunk) {
        const writable = await fileHandle.createWritable();
        await writable.write(chunk);
        await writable.close();
    }
}
```

#### 实现建议

**1. 渐进式实现**：
- 首先实现基于 MediaRecorder 的录制
- 逐步添加更高级的功能
- 兼容不同浏览器的特性

**2. 格式选择**：
- 优先使用 WebM 格式（浏览器原生支持）
- 备选方案使用 MP4 格式
- 避免使用 AVI 格式（浏览器支持有限）

**3. 性能优化**：
- 使用 Web Workers 进行后台处理
- 实现流式写入避免内存溢出
- 提供录制质量和文件大小的平衡选项

#### 技术路线图

**短期目标**：
1. 实现基于 MediaRecorder 的基础录制功能
2. 支持 WebM 格式输出
3. 提供基本的质量控制选项

**中期目标**：
1. 添加 File System Access API 支持
2. 实现分块存储和流式写入
3. 支持更多视频格式

**长期目标**：
1. 实现完整的 MovieWriter 接口兼容
2. 支持自定义编码器
3. 提供与桌面版本一致的 API

### 13.5 性能优化建议

#### 针对 Godot MovieWriter 的优化

**1. 硬件加速支持**：
```cpp
// 可以考虑添加硬件编码器接口
class HardwareMovieWriter : public MovieWriter {
    // 使用平台特定的硬件编码器
    // Windows: Media Foundation
    // Linux: VA-API
    // macOS: VideoToolbox
};
```

**2. 现代编码格式**：
- 添加 H.264 支持
- 实现 VP8/VP9 编码器
- 支持 AV1 编码（未来）

**3. 音频压缩**：
- 添加 AAC 音频编码
- 支持 Opus 音频编码
- 提供音频质量选项

这些分析展示了 Godot MovieWriter 系统的设计权衡和改进空间，同时也为 Web 平台的支持提供了可行的技术路线。 

## 14. 实时录制技术方案

### 14.1 当前系统的实时录制限制

#### 核心问题分析

**1. 固定帧率机制的限制**：

```cpp
// 在 main_timer_sync.cpp 中
MainFrameTime MainTimerSync::advance_checked(double p_physics_step, int p_physics_ticks_per_second, double p_process_step) {
    if (fixed_fps != -1) {
        p_process_step = 1.0 / fixed_fps;  // 强制固定时间步长
    }
    // ...
}
```

这种设计完全忽略了实际的帧时间，导致：
- 无法与显示器刷新率同步
- 无法适应性能波动
- 录制速度固定，无法实时预览

**2. AudioDriverDummy 的限制**：

```cpp
// 在录制模式下强制使用 Dummy 音频驱动
if (Engine::get_singleton()->get_write_movie_path() != String()) {
    audio_driver_idx = AudioDriverManager::get_driver_count() - 1;  // Dummy driver
    AudioDriverDummy::get_dummy_singleton()->set_use_threads(false);
}
```

这导致：
- 用户无法听到声音
- 无法进行实时录制反馈
- 音频与视频分离处理

**3. 同步I/O性能瓶颈**：

```cpp
void MovieWriter::add_frame() {
    // 同步获取图像数据
    RID main_vp_texture = RenderingServer::get_singleton()->viewport_get_texture(main_vp_rid);
    Ref<Image> vp_tex = RenderingServer::get_singleton()->texture_2d_get(main_vp_texture);
    
    // 同步音频处理
    AudioDriverDummy::get_dummy_singleton()->mix_audio(mix_rate / fps, audio_mix_buffer.ptr());
    
    // 同步文件写入
    write_frame(vp_tex, audio_mix_buffer.ptr());
}
```

### 14.2 实时录制解决方案

#### 1. 双模式录制系统

**设计思路**：
- 保留现有的非实时录制模式
- 新增实时录制模式
- 通过配置参数切换

**实现方案**：

```cpp
enum RecordingMode {
    RECORDING_MODE_OFFLINE,    // 当前的非实时模式
    RECORDING_MODE_REALTIME    // 新的实时模式
};

class RealtimeMovieWriter : public MovieWriter {
private:
    RecordingMode recording_mode;
    std::thread encoding_thread;
    std::queue<FrameData> frame_queue;
    std::mutex queue_mutex;
    
public:
    void set_recording_mode(RecordingMode mode) {
        recording_mode = mode;
    }
    
    virtual void add_frame() override {
        if (recording_mode == RECORDING_MODE_REALTIME) {
            add_frame_realtime();
        } else {
            add_frame_offline();
        }
    }
    
private:
    void add_frame_realtime() {
        // 异步获取帧数据
        FrameData frame;
        capture_frame_async(frame);
        
        // 加入编码队列
        enqueue_frame(frame);
    }
    
    void add_frame_offline() {
        // 原有的同步实现
        MovieWriter::add_frame();
    }
};
```

#### 2. 异步编码管道

**多线程架构**：

```cpp
class AsyncEncoder {
private:
    // 三个独立的线程
    std::thread capture_thread;    // 图像捕获线程
    std::thread encode_thread;     // 编码线程
    std::thread write_thread;      // 文件写入线程
    
    // 线程间通信队列
    ThreadSafeQueue<RawFrame> capture_queue;
    ThreadSafeQueue<EncodedFrame> encode_queue;
    
public:
    void start_realtime_recording() {
        capture_thread = std::thread(&AsyncEncoder::capture_loop, this);
        encode_thread = std::thread(&AsyncEncoder::encode_loop, this);
        write_thread = std::thread(&AsyncEncoder::write_loop, this);
    }
    
private:
    void capture_loop() {
        while (is_recording) {
            RawFrame frame;
            capture_frame(frame);
            capture_queue.push(frame);
        }
    }
    
    void encode_loop() {
        while (is_recording) {
            RawFrame raw_frame;
            if (capture_queue.try_pop(raw_frame)) {
                EncodedFrame encoded_frame;
                encode_frame(raw_frame, encoded_frame);
                encode_queue.push(encoded_frame);
            }
        }
    }
    
    void write_loop() {
        while (is_recording) {
            EncodedFrame encoded_frame;
            if (encode_queue.try_pop(encoded_frame)) {
                write_frame_to_file(encoded_frame);
            }
        }
    }
};
```

#### 3. 智能帧率适配

**自适应帧率系统**：

```cpp
class AdaptiveFrameRate {
private:
    double target_fps;
    double current_fps;
    double performance_factor;
    
public:
    void update_performance_metrics() {
        // 监控编码性能
        double encode_time = measure_encode_time();
        double available_time = 1.0 / target_fps;
        
        performance_factor = available_time / encode_time;
        
        // 动态调整策略
        if (performance_factor < 0.8) {
            // 性能不足，降低质量或跳帧
            reduce_quality();
        } else if (performance_factor > 1.2) {
            // 性能充足，提高质量
            increase_quality();
        }
    }
    
    bool should_skip_frame() {
        return performance_factor < 0.5;
    }
};
```

#### 4. 混合音频驱动

**实时音频处理**：

```cpp
class HybridAudioDriver : public AudioDriver {
private:
    AudioDriver* real_driver;      // 实际音频驱动
    AudioDriver* dummy_driver;     // 录制用驱动
    bool recording_enabled;
    
public:
    void start_recording() {
        recording_enabled = true;
        dummy_driver->start();
    }
    
    void stop_recording() {
        recording_enabled = false;
        dummy_driver->stop();
    }
    
    virtual void audio_server_process(int p_frames, int32_t *p_buffer, bool p_update_mix_time = true) override {
        // 正常音频播放
        real_driver->audio_server_process(p_frames, p_buffer, p_update_mix_time);
        
        // 如果正在录制，同时发送到录制驱动
        if (recording_enabled) {
            dummy_driver->audio_server_process(p_frames, p_buffer, false);
        }
    }
};
```

#### 5. 硬件加速编码

**GPU 编码集成**：

```cpp
class HardwareEncoder {
private:
    enum EncoderType {
        ENCODER_SOFTWARE,
        ENCODER_NVENC,
        ENCODER_QUICKSYNC,
        ENCODER_AMF
    };
    
    EncoderType encoder_type;
    
public:
    void initialize_hardware_encoder() {
        // 检测可用的硬件编码器
        if (is_nvenc_available()) {
            encoder_type = ENCODER_NVENC;
            init_nvenc();
        } else if (is_quicksync_available()) {
            encoder_type = ENCODER_QUICKSYNC;
            init_quicksync();
        } else if (is_amf_available()) {
            encoder_type = ENCODER_AMF;
            init_amf();
        } else {
            encoder_type = ENCODER_SOFTWARE;
            init_software_encoder();
        }
    }
    
    void encode_frame_gpu(const Ref<Image>& frame, EncodedFrame& output) {
        switch (encoder_type) {
            case ENCODER_NVENC:
                encode_nvenc(frame, output);
                break;
            case ENCODER_QUICKSYNC:
                encode_quicksync(frame, output);
                break;
            // ... 其他编码器
        }
    }
};
```

### 14.3 用户界面改进

#### 实时录制控制面板

```cpp
class RealtimeRecordingPanel : public Control {
private:
    Button* start_button;
    Button* pause_button;
    Button* stop_button;
    ProgressBar* performance_bar;
    Label* status_label;
    
public:
    void _ready() override {
        // 创建用户界面
        create_recording_controls();
        
        // 连接信号
        start_button->connect("pressed", callable_mp(this, &RealtimeRecordingPanel::start_recording));
        pause_button->connect("pressed", callable_mp(this, &RealtimeRecordingPanel::pause_recording));
        stop_button->connect("pressed", callable_mp(this, &RealtimeRecordingPanel::stop_recording));
    }
    
    void update_performance_indicator(double performance_factor) {
        performance_bar->set_value(performance_factor * 100);
        
        if (performance_factor < 0.8) {
            status_label->set_text("Performance Warning: Consider lowering quality");
            status_label->set_modulate(Color::YELLOW);
        } else {
            status_label->set_text("Recording: Good Performance");
            status_label->set_modulate(Color::GREEN);
        }
    }
};
```

### 14.4 配置选项

#### 实时录制配置

```cpp
// 项目设置
GLOBAL_DEF("movie_writer/realtime/enabled", false);
GLOBAL_DEF("movie_writer/realtime/target_fps", 60);
GLOBAL_DEF("movie_writer/realtime/adaptive_quality", true);
GLOBAL_DEF("movie_writer/realtime/hardware_acceleration", true);
GLOBAL_DEF("movie_writer/realtime/audio_preview", true);
GLOBAL_DEF("movie_writer/realtime/buffer_size", 10);

// 编码质量设置
GLOBAL_DEF("movie_writer/encoding/video_codec", "h264");
GLOBAL_DEF("movie_writer/encoding/audio_codec", "aac");
GLOBAL_DEF("movie_writer/encoding/bitrate", 8000);
GLOBAL_DEF("movie_writer/encoding/quality_preset", "medium");
```

### 14.5 实现优先级

#### 短期目标（3-6个月）

1. **双模式架构**：
   - 实现 RealtimeMovieWriter 基类
   - 添加录制模式切换
   - 保持向后兼容性

2. **异步编码管道**：
   - 实现多线程编码框架
   - 添加帧缓冲队列
   - 优化内存管理

#### 中期目标（6-12个月）

1. **硬件加速支持**：
   - 集成 NVENC 编码器
   - 添加 QuickSync 支持
   - 实现 AMF 编码器

2. **现代编码格式**：
   - 支持 H.264/H.265
   - 添加 VP8/VP9 支持
   - 实现音频压缩

#### 长期目标（12个月以上）

1. **Web 平台支持**：
   - 使用 MediaRecorder API
   - 实现 IndexedDB 存储
   - 添加流媒体推送

2. **云录制服务**：
   - 实现录制服务器
   - 添加远程存储
   - 支持协作录制

### 14.6 性能基准测试

#### 测试场景

```cpp
class RecordingBenchmark {
public:
    struct BenchmarkResult {
        double average_fps;
        double min_fps;
        double max_fps;
        double cpu_usage;
        double memory_usage;
        double encode_time;
        double file_size;
    };
    
    BenchmarkResult run_benchmark(RecordingMode mode, int duration_seconds) {
        BenchmarkResult result;
        
        // 设置测试环境
        setup_test_scene();
        
        // 开始录制
        start_recording(mode);
        
        // 运行测试
        auto start_time = std::chrono::steady_clock::now();
        while (get_elapsed_time(start_time) < duration_seconds) {
            // 收集性能数据
            collect_performance_metrics(result);
            
            // 更新场景
            update_test_scene();
        }
        
        // 停止录制
        stop_recording();
        
        return result;
    }
};
```

这个实时录制方案提供了：
- **兼容性**：保持现有非实时录制功能
- **性能**：通过异步处理和硬件加速提高效率
- **用户体验**：实时预览和性能反馈
- **扩展性**：支持多种编码格式和平台

通过这种渐进式的实现方法，可以在不破坏现有功能的基础上，为 Godot 添加强大的实时录制能力。 