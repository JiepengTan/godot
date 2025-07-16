# OBS风格录制器 - 合并录制功能

## 概述

`ObsStyleMovieWriter` 现在支持两种录制模式：

1. **分离录制模式**（原有功能）：将视频和音频分别录制到两个独立的AVI文件中
2. **合并录制模式**（新功能）：将视频和音频合并录制到同一个AVI文件中，方便音视频同步检查

## 新功能特性

### 合并录制模式
- ✅ 视频和音频写入同一个AVI文件
- ✅ 实时音视频同步
- ✅ 标准AVI格式，兼容大多数播放器
- ✅ 自动音视频时长检查和同步验证
- ✅ 16位PCM音频格式，提高兼容性
- ✅ 独立录制线程，不影响游戏性能

### 改进的EnhancedAviWriter
- ✅ 修复了索引条目偏移计算错误
- ✅ 正确的文件头更新机制
- ✅ 改用16位PCM音频格式替代32位
- ✅ 简化的时间戳记录
- ✅ 完整的音视频同步统计

## 使用方法

### 启用合并录制模式

在项目设置中添加以下配置：

```gdscript
# project.godot 或通过项目设置UI
[movie_writer]
obs_enable_combined_recording=true
obs_video_fps=30
obs_video_quality=0.85
obs_audio_sample_rate=48000
obs_audio_channels=2
obs_enable_debug_output=true
```

### 代码中使用

```gdscript
# 获取OBS风格录制器
var obs_recorder = ObsStyleMovieWriter.new()

# 配置合并录制
var config = ObsStyleMovieWriter.ObsRecordingConfig()
config.enable_combined_recording = true
config.video_fps = 30
config.jpeg_quality = 0.85
config.audio_sample_rate = 48000
config.audio_channels = 2

obs_recorder.set_recording_config(config)

# 开始录制
obs_recorder.begin(Vector2i(1920, 1080), 30, "output_path")
```

## 输出文件对比

### 分离录制模式 (enable_combined_recording = false)
```
output_path_video.avi  # 仅包含视频
output_path_audio.avi  # 仅包含音频
```

### 合并录制模式 (enable_combined_recording = true)
```
output_path_combined.avi  # 包含视频+音频
```

## 技术实现

### 合并录制架构
1. **主游戏线程**：更新视频帧到ThreadSafeFrameBuffer，音频数据到音频缓冲区
2. **合并录制线程**：以固定30fps读取视频帧和音频数据，写入AVI文件
3. **EnhancedAviWriter**：标准AVI格式写入，支持视频MJPEG + 音频16位PCM

### 音视频同步机制
- 视频按固定帧率录制（30fps）
- 音频按视频帧同步写入（每帧写入 sample_rate/fps 个样本）
- 自动音频缓冲区管理，避免溢出和下溢
- 录制完成后自动检查音视频时长同步

### 性能优化
- 独立录制线程，不阻塞游戏主线程
- 双缓冲区机制，避免帧数据竞争
- 线程安全的音频缓冲区
- 智能睡眠控制，降低CPU占用

## 故障排除

### 常见问题

1. **音视频不同步**
   - 检查音频采样率设置是否正确
   - 确保游戏帧率稳定
   - 查看录制完成后的同步验证信息

2. **录制文件无法播放**
   - 确保使用支持MJPEG+PCM的播放器
   - 推荐使用VLC、ffplay等标准播放器

3. **录制性能问题**
   - 降低JPEG质量设置
   - 减少录制分辨率
   - 检查磁盘写入速度

### 调试输出

启用 `obs_enable_debug_output=true` 可以看到详细的录制信息：

```
=== OBS-style Recording Started ===
Output file: test_combined.avi
Video resolution: 1920x1080
Target FPS: 30
Audio config: 48000Hz, 2ch
合并录制模式：跳过HybridAudioDriver设置，使用直接音频传递
合并录制文件: test_combined.avi
双缓冲区和AVI写入器初始化成功
合并录制线程已启动
合并录制线程开始运行
合并录制进度: 300 帧, 实际FPS: 30.0
...
=== AVI合并录制完成 ===
输出文件: test_combined.avi
视频帧数: 900
音频块数: 900
总音频样本: 1440000
索引条目: 1800
视频时长: 30.00 秒
音频时长: 30.00 秒
✓ 音视频同步正常
```

## 配置参考

### 高质量配置
```gdscript
var config = ObsStyleMovieWriter.get_high_quality_config()
config.enable_combined_recording = true
```

### 标准配置
```gdscript
var config = ObsStyleMovieWriter.get_standard_config()
config.enable_combined_recording = true
```

### 性能优化配置
```gdscript
var config = ObsStyleMovieWriter.get_performance_config()
config.enable_combined_recording = true
```

## 版本历史

### v1.1.0 - 合并录制功能
- ✅ 新增合并录制模式
- ✅ 修复EnhancedAviWriter的多个问题
- ✅ 改进音视频同步机制
- ✅ 增强错误处理和调试输出

### v1.0.0 - 基础功能
- ✅ OBS风格独立线程录制
- ✅ 分离视频和音频录制
- ✅ ThreadSafeFrameBuffer双缓冲机制

---

**现在您可以轻松录制包含音视频同步的完整AVI文件，方便检查游戏的音视频表现！** 🎬🎵 