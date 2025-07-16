# Godot OBS式独立线程录制方案

## 总体架构

### 1. 核心设计原则
- **时间轴驱动**：录制以固定时间轴为准，不受游戏帧率影响
- **独立线程**：视频和音频录制线程完全独立于游戏主线程
- **双缓冲保障**：确保画面捕获的稳定性和线程安全
- **完整信息保存**：记录真实时间戳和重复帧信息用于后期分析

### 2. 线程架构图
```
游戏主线程          视频录制线程           音频录制线程
    |                    |                    |
[渲染画面]          [30fps定时器]        [48kHz定时器]
    |                    |                    |
[更新双缓冲] -----> [读取缓冲区]            [捕获音频输出]
    |                    |                    |
[游戏逻辑]          [写入AVI视频]        [写入AVI音频]
    |                    |                    |
[可能卡顿]          [固定节拍运行]        [连续运行]
```

### 3. 录制效果对比
```
OBS录制效果：
游戏画面：  [帧1] ----卡顿2秒---- [帧2] [帧3] [帧4]
游戏音频：  ♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪
OBS录制：   [帧1] [重复帧1多次]     [帧2] [帧3] [帧4]
           ♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪

本方案效果：
游戏状态：      [正常60fps] [卡顿1秒] [恢复60fps]
录制视频帧：    [新][新][新] [重][重][重][...] [新][新][新]
录制音频：      [连续48kHz音频流，不受游戏卡顿影响]
```

## 详细技术方案

### 1. 双缓冲画面捕获系统

#### A. 缓冲区管理
```cpp
class ThreadSafeFrameBuffer {
private:
    struct FrameData {
        Ref<Image> image;
        uint64_t game_timestamp;    // 游戏时间戳
        uint32_t frame_sequence;    // 游戏帧序号
        bool is_new_frame;          // 是否为新帧
    };
    
    FrameData buffer_a, buffer_b;
    bool writing_to_a = true;       // 游戏线程写入标志
    std::atomic<bool> has_new_data{false};
    mutable std::mutex buffer_mutex;
    
public:
    // 游戏线程调用：更新画面
    void update_frame(Ref<Image> new_frame, uint64_t timestamp, uint32_t sequence);
    
    // 录制线程调用：获取稳定画面
    FrameData get_current_frame() const;
    
    // 检查是否有新数据
    bool has_new_frame() const { return has_new_data.load(); }
};
```

#### B. 游戏主线程集成
```cpp
// 在现有的 MovieWriter::add_frame() 中添加
void MovieWriter::add_frame() {
    // 1. 正常获取当前帧
    RID main_vp_rid = RenderingServer::get_singleton()->viewport_find_from_screen_attachment(DisplayServer::MAIN_WINDOW_ID);
    RID main_vp_texture = RenderingServer::get_singleton()->viewport_get_texture(main_vp_rid);
    Ref<Image> vp_tex = RenderingServer::get_singleton()->texture_2d_get(main_vp_texture);
    
    // 2. 更新双缓冲区（非阻塞）
    uint64_t game_time = OS::get_singleton()->get_ticks_usec();
    frame_buffer->update_frame(vp_tex, game_time, game_frame_sequence++);
    
    // 3. 不再直接写入文件，由独立线程处理
}
```

### 2. 独立视频录制线程

#### A. 固定帧率录制逻辑
```cpp
class IndependentVideoRecorder {
private:
    // 录制参数（固定）
    static const uint32_t TARGET_FPS = 30;
    static const uint64_t FRAME_INTERVAL_USEC = 1000000 / TARGET_FPS; // 33333微秒
    
    // 状态跟踪
    uint32_t recorded_frame_count = 0;
    uint32_t last_game_frame_sequence = 0;
    FrameBuffer::FrameData last_valid_frame;
    
public:
    void recording_loop() {
        uint64_t recording_start_time = OS::get_singleton()->get_ticks_usec();
        uint64_t next_record_time = recording_start_time;
        
        while (recording_active) {
            uint64_t current_time = OS::get_singleton()->get_ticks_usec();
            
            if (current_time >= next_record_time) {
                // 尝试获取新帧
                auto frame_data = frame_buffer->get_current_frame();
                
                bool is_repeated_frame = false;
                if (frame_data.frame_sequence == last_game_frame_sequence) {
                    // 游戏没有新帧，使用重复帧
                    is_repeated_frame = true;
                    frame_data = last_valid_frame; // 复用上一帧
                } else {
                    // 有新帧，更新记录
                    last_valid_frame = frame_data;
                    last_game_frame_sequence = frame_data.frame_sequence;
                }
                
                // 写入AVI文件
                write_video_frame_to_avi(frame_data, next_record_time, is_repeated_frame);
                
                recorded_frame_count++;
                next_record_time += FRAME_INTERVAL_USEC;
            }
            
            // 精确睡眠（避免忙等待）
            auto sleep_until = std::chrono::high_resolution_clock::now() + 
                              std::chrono::microseconds(next_record_time - current_time);
            std::this_thread::sleep_until(sleep_until);
        }
    }
};
```

### 3. 独立音频录制线程

#### A. 连续音频捕获
```cpp
class IndependentAudioRecorder {
private:
    // 录制参数（固定）
    static const uint32_t SAMPLE_RATE = 48000;
    static const uint32_t CHANNELS = 2;
    static const uint32_t CHUNK_SIZE = 480;  // 10ms音频块
    static const uint64_t CHUNK_INTERVAL_USEC = CHUNK_SIZE * 1000000 / SAMPLE_RATE;
    
    // 音频环形缓冲区
    std::vector<float> audio_ring_buffer;
    std::atomic<uint32_t> write_pos{0};
    std::atomic<uint32_t> read_pos{0};
    
public:
    void recording_loop() {
        uint64_t recording_start_time = OS::get_singleton()->get_ticks_usec();
        uint64_t next_chunk_time = recording_start_time;
        
        while (recording_active) {
            uint64_t current_time = OS::get_singleton()->get_ticks_usec();
            
            if (current_time >= next_chunk_time) {
                // 从环形缓冲区读取音频数据
                std::vector<float> audio_chunk = read_audio_chunk(CHUNK_SIZE);
                
                // 写入AVI文件
                write_audio_chunk_to_avi(audio_chunk, next_chunk_time);
                
                next_chunk_time += CHUNK_INTERVAL_USEC;
            }
            
            std::this_thread::sleep_until(
                std::chrono::high_resolution_clock::now() + 
                std::chrono::microseconds(next_chunk_time - current_time)
            );
        }
    }
    
    // 由HybridAudioDriver调用，将音频输出复制到环形缓冲区
    void on_audio_output(const int32_t* buffer, int frame_count);
};
```

### 4. 扩展AVI格式

#### A. 自定义Chunk定义
```cpp
// 时间戳信息chunk
struct TimestampChunk {
    char fourcc[4] = {'0', '0', 't', 's'};  // "00ts"
    uint32_t size = 24;                     // 数据大小
    uint64_t recording_timestamp;           // 录制时间戳（微秒）
    uint64_t game_timestamp;               // 游戏时间戳（微秒）
    uint32_t frame_sequence;               // 游戏帧序号
    uint8_t flags;                         // 标志位
    uint8_t reserved[3];                   // 保留字段
};

// 标志位定义
enum FrameFlags : uint8_t {
    FRAME_FLAG_NEW = 0x01,        // 新帧
    FRAME_FLAG_REPEATED = 0x02,   // 重复帧
    FRAME_FLAG_DROPPED = 0x04,    // 丢帧（预留）
};
```

#### B. AVI写入顺序
```
每个录制帧的AVI数据结构：
├── 时间戳chunk (00ts)
│   ├── 录制时间戳: 33333μs
│   ├── 游戏时间戳: 16667μs
│   ├── 游戏帧序号: 1
│   └── 标志: FRAME_FLAG_NEW
├── 视频数据chunk (00db)
│   └── JPEG图像数据
└── 音频数据chunk (01wb)
    └── PCM音频数据 (10ms)
```

#### C. AVI文件时间轴示例
```
时间轴：  0ms    33ms   66ms   100ms  133ms  166ms
视频：   [帧1]  [帧1]  [帧1]  [帧2]  [帧3]  [帧4]   ← 固定30fps
音频：   [♪♪]   [♪♪]   [♪♪]   [♪♪]   [♪♪]   [♪♪]    ← 连续音频块
标志：   [新]   [重]   [重]   [新]   [新]   [新]

AVI文件：
├── 00ts + 时间戳数据  (录制时间:0ms, 游戏时间:0ms, 标志:新帧)
├── 00db + 帧1数据    
├── 01wb + 音频块1    
├── 00ts + 时间戳数据  (录制时间:33ms, 游戏时间:0ms, 标志:重复帧)
├── 00db + 帧1数据    ← 重复帧
├── 01wb + 音频块2    
├── 00ts + 时间戳数据  (录制时间:66ms, 游戏时间:0ms, 标志:重复帧)
├── 00db + 帧1数据    ← 重复帧
├── 01wb + 音频块3    
└── ...
```

### 5. 音频系统集成

#### A. HybridAudioDriver增强
```cpp
class HybridAudioDriver : public AudioDriver {
private:
    std::vector<IndependentAudioRecorder*> registered_recorders;
    
public:
    virtual void audio_server_process(int p_frames, int32_t *p_buffer) override {
        // 1. 执行正常音频处理
        AudioDriver::audio_server_process(p_frames, p_buffer);
        
        // 2. 复制音频数据给所有注册的录制器
        for (auto recorder : registered_recorders) {
            recorder->on_audio_output(p_buffer, p_frames);
        }
    }
    
    // 录制器注册接口
    void register_audio_recorder(IndependentAudioRecorder* recorder) {
        registered_recorders.push_back(recorder);
    }
    
    void unregister_audio_recorder(IndependentAudioRecorder* recorder) {
        auto it = std::find(registered_recorders.begin(), registered_recorders.end(), recorder);
        if (it != registered_recorders.end()) {
            registered_recorders.erase(it);
        }
    }
};
```

#### B. 音频捕获原理
```
音频处理流程：
AudioServer::update() 
    ↓
HybridAudioDriver::audio_server_process()
    ↓ (复制音频数据)
IndependentAudioRecorder::on_audio_output()
    ↓ (写入环形缓冲区)
audio_ring_buffer[write_pos++] = audio_data
    ↓ (录制线程读取)
IndependentAudioRecorder::read_audio_chunk()
    ↓ (写入AVI文件)
write_audio_chunk_to_avi()
```

### 6. 配置参数

#### A. 固定录制参数
```cpp
struct RecordingConfig {
    // 视频参数
    static const uint32_t VIDEO_FPS = 30;
    static const uint32_t VIDEO_WIDTH = 1920;   // 可配置
    static const uint32_t VIDEO_HEIGHT = 1080;  // 可配置
    static const float JPEG_QUALITY = 0.85f;
    
    // 音频参数
    static const uint32_t AUDIO_SAMPLE_RATE = 48000;
    static const uint32_t AUDIO_CHANNELS = 2;
    static const uint32_t AUDIO_CHUNK_MS = 10;
    
    // 缓冲区参数
    static const uint32_t AUDIO_BUFFER_SIZE = AUDIO_SAMPLE_RATE * 2; // 2秒缓冲
    static const uint32_t MAX_FRAME_BUFFER_SIZE = 4; // 最多缓存4帧
};
```

#### B. 项目配置文件
```ini
[movie_writer]
obs_mode = true                     # 启用OBS式录制
video_fps = 30                     # 固定录制帧率
video_quality = 0.85               # JPEG质量
audio_sample_rate = 48000          # 音频采样率
audio_buffer_seconds = 2.0         # 音频缓冲时长
enable_timestamp_chunks = true     # 启用时间戳记录
enable_repeat_frame_marking = true # 启用重复帧标记
```

## 实现架构

### 1. 类层次结构
```
MovieWriter (基类)
├── MovieWriterMJPEG (现有实现)
└── ObsStyleMovieWriter (新实现)
    ├── ThreadSafeFrameBuffer
    ├── IndependentVideoRecorder
    ├── IndependentAudioRecorder
    └── EnhancedAviWriter
```

### 2. 主要文件修改
```
servers/movie_writer/
├── movie_writer_obs.h              # 新增：OBS式录制器
├── movie_writer_obs.cpp            # 新增：OBS式录制器实现
├── thread_safe_frame_buffer.h      # 新增：双缓冲管理
├── thread_safe_frame_buffer.cpp    # 新增：双缓冲实现
└── enhanced_avi_writer.h           # 新增：增强AVI写入器

servers/audio/
├── audio_driver_hybrid.h           # 修改：添加录制器注册
└── audio_driver_hybrid.cpp         # 修改：音频数据复制

main/
└── main.cpp                        # 修改：集成OBS式录制器
```

### 3. 生命周期管理
```cpp
// 录制开始
void ObsStyleMovieWriter::begin() {
    // 1. 初始化双缓冲区
    frame_buffer = new ThreadSafeFrameBuffer();
    
    // 2. 创建并启动视频录制线程
    video_recorder = new IndependentVideoRecorder();
    video_thread = std::thread(&IndependentVideoRecorder::recording_loop, video_recorder);
    
    // 3. 创建并启动音频录制线程
    audio_recorder = new IndependentAudioRecorder();
    audio_thread = std::thread(&IndependentAudioRecorder::recording_loop, audio_recorder);
    
    // 4. 注册音频录制器到HybridAudioDriver
    hybrid_audio_driver->register_audio_recorder(audio_recorder);
}

// 录制结束
void ObsStyleMovieWriter::end() {
    // 1. 停止录制线程
    recording_active = false;
    video_thread.join();
    audio_thread.join();
    
    // 2. 注销音频录制器
    hybrid_audio_driver->unregister_audio_recorder(audio_recorder);
    
    // 3. 清理资源
    delete video_recorder;
    delete audio_recorder;
    delete frame_buffer;
}
```

## 技术优势与特性

### 1. 核心优势
- **真实还原**：完全模拟OBS录制效果，游戏卡顿在视频中真实体现
- **音频连续**：音频录制独立于游戏帧率，始终保持连续
- **数据完整**：保存完整的时序信息，支持后期分析和处理
- **标准兼容**：生成的AVI文件可在任何标准播放器中播放
- **性能隔离**：录制线程独立运行，不影响游戏主线程性能

### 2. 分析能力
通过扩展的AVI格式，可以进行以下分析：

#### A. 性能分析
```python
# 分析重复帧比例
def analyze_performance(avi_file):
    total_frames = 0
    repeated_frames = 0
    
    for chunk in read_timestamp_chunks(avi_file):
        total_frames += 1
        if chunk.flags & FRAME_FLAG_REPEATED:
            repeated_frames += 1
    
    repeat_ratio = repeated_frames / total_frames
    print(f"重复帧比例: {repeat_ratio:.2%}")
    return repeat_ratio
```

#### B. 帧率波动分析
```python
# 分析游戏真实帧率
def analyze_game_fps(avi_file):
    game_timestamps = []
    
    for chunk in read_timestamp_chunks(avi_file):
        if chunk.flags & FRAME_FLAG_NEW:
            game_timestamps.append(chunk.game_timestamp)
    
    # 计算帧间隔
    frame_intervals = []
    for i in range(1, len(game_timestamps)):
        interval = (game_timestamps[i] - game_timestamps[i-1]) / 1000000.0
        frame_intervals.append(1.0 / interval)  # 转换为FPS
    
    avg_fps = sum(frame_intervals) / len(frame_intervals)
    min_fps = min(frame_intervals)
    max_fps = max(frame_intervals)
    
    print(f"平均FPS: {avg_fps:.1f}")
    print(f"最低FPS: {min_fps:.1f}")
    print(f"最高FPS: {max_fps:.1f}")
```

### 3. 后期处理能力
基于时间戳信息，可以实现各种后期处理：

- **变速播放**：根据游戏真实帧率调整播放速度
- **卡顿优化**：跳过或快进卡顿部分
- **帧率提升**：通过插值算法生成中间帧
- **音视频重同步**：根据时间戳重新对齐音视频

## 使用方法

### 1. 编译配置
```bash
# 编译时启用OBS式录制支持
scons platform=macos target=editor obs_recording=yes
```

### 2. 运行命令
```bash
# 启用OBS式录制
./godot --write-movie output.avi --obs-mode --path project/

# 传统录制模式
./godot --write-movie output.avi --fixed-fps 60 --path project/
```

### 3. 配置文件示例
```ini
# project.godot
[movie_writer]
obs_mode = true
video_fps = 30
video_quality = 0.85
audio_sample_rate = 48000
enable_timestamp_analysis = true
```

## 未来扩展

### 1. 可能的增强功能
- **硬件加速编码**：支持GPU编码以提高性能
- **多格式输出**：支持MP4、WebM等现代格式
- **实时预览**：录制过程中显示实时预览窗口
- **智能质量调节**：根据系统性能自动调整录制质量

### 2. 工具链支持
- **分析工具**：专用的性能分析和可视化工具
- **转换工具**：AVI到其他格式的转换工具
- **编辑器集成**：Godot编辑器内置的录制控制面板

## 总结

这个OBS式独立线程录制方案提供了一个完整、稳定、高性能的视频录制解决方案。通过双缓冲机制和独立线程架构，实现了与OBS类似的录制效果，同时保持了与Godot引擎的深度集成。扩展的AVI格式不仅保证了标准兼容性，还提供了丰富的分析能力，为游戏开发和性能优化提供了有力的工具支持。
