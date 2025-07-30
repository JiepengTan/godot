# Web端音频录制使用说明

## ❓ 关于MediaRecorder只录制音频的说明

**是的，MediaRecorder完全可以只录制音频！**

我们的实现使用了以下技术确保只录制音频：

### 🎵 纯音频录制原理

1. **MediaStreamDestination**: 
   - `AudioContext.createMediaStreamDestination()` 只创建音频流
   - 天然不包含任何视频轨道
   - 专门用于Web Audio API的音频输出

2. **音频验证机制**:
   ```javascript
   // 流验证
   const audioTracks = stream.getAudioTracks().length; // 1
   const videoTracks = stream.getVideoTracks().length; // 0
   
   // MIME类型验证
   const recorder = new MediaRecorder(stream, {
       mimeType: 'audio/webm;codecs=opus'  // 明确指定音频格式
   });
   
   // 录制数据验证
   blob.type.startsWith('audio/')  // true
   ```

3. **支持的纯音频格式**:
   - `audio/webm;codecs=opus` ✅ 最佳选择（高质量低延迟）
   - `audio/webm` ✅ 通用WebM音频
   - `audio/mp4` ✅ MP4音频容器
   - `audio/wav` ✅ 未压缩WAV

### 🔍 实时验证日志

运行时会看到以下验证信息：
```
GodotAudioRecorder stream verification:
  Audio tracks: 1
  Video tracks: 0
GodotAudioRecorder: Selected audio format: audio/webm;codecs=opus
GodotAudioRecorder: AUDIO-ONLY recording started
GodotAudioRecorder: Audio chunk recorded: 1234 bytes (audio/webm;codecs=opus)
```

### 🧪 独立验证测试

如果您想验证MediaRecorder确实可以只录制音频，可以使用提供的测试页面：

```bash
# 在浏览器中打开
open simple_audio_test.html
```

这个测试页面会：
1. 创建一个纯音频流（440Hz正弦波）
2. 使用MediaRecorder录制3秒
3. 验证录制的数据确实是音频格式
4. 提供下载录制的音频文件

测试结果会显示详细的验证信息，证明MediaRecorder完全可以只录制音频。

## 📝 概述

本实现基于**方案1：MediaRecorder API**，为Godot Engine的Web平台提供了完整的音频录制功能，解决了原有Web端音频录制失效的问题。

## 🔧 实现内容

### 1. JavaScript层 (library_godot_audio.js)
- **GodotAudioRecorder**: 完整的MediaRecorder API集成
- **音频数据捕获**: 使用MediaStreamDestination捕获Web Audio API输出
- **多格式支持**: 支持WebM/Opus、WebM、MP4、WAV等格式
- **C++接口**: 提供完整的C++调用接口

### 2. C++接口层 (godot_audio.h)
- `godot_audio_recorder_init()`: 初始化录制器
- `godot_audio_recorder_start()`: 开始录制
- `godot_audio_recorder_stop()`: 停止录制
- `godot_audio_recorder_is_recording()`: 检查录制状态
- `godot_audio_recorder_get_data_size()`: 获取录制数据大小
- `godot_audio_recorder_download_data()`: 下载录制数据（测试用）

### 3. MovieWriter集成 (movie_writer.cpp/h)
- **Web平台检测**: 自动识别Web平台并使用MediaRecorder
- **统一接口**: 与PC端HybridAudioDriver保持相同的使用方式
- **错误处理**: 完善的初始化和清理流程

## 🚀 使用方法

### 基本录制流程

```gdscript
# 1. 启用实时录制模式
MovieWriter.get_singleton().set_realtime_mode(true)

# 2. 设置录制路径
Engine.set_write_movie_path("user://my_recording.avi")

# 3. 开始游戏内容（音频会自动录制）
# ... 你的游戏逻辑 ...

# 4. 停止录制
MovieWriter.get_singleton().set_realtime_mode(false)
Engine.set_write_movie_path("")
```

### 测试示例

使用提供的测试脚本 `test_web_audio_recording.gd`：

```gdscript
# 添加到场景中并运行
var test_scene = preload("res://test_web_audio_recording.gd")
add_child(test_scene.new())
```

## 🛠️ 编译说明

### 1. Web平台编译

```bash
# 标准Web编译
scons platform=web tools=yes target=template_debug

# 或者用于发布
scons platform=web tools=no target=template_release
```

### 2. 编译要求

- **Emscripten**: 最新版本
- **Python**: 3.6+
- **SCons**: 4.0+

### 3. 验证编译

编译完成后，检查生成的文件中是否包含：
- `godot.js`: 包含GodotAudioRecorder函数
- `godot.wasm`: 包含Web音频录制C++代码

## 🔍 调试和验证

### 1. 浏览器控制台日志

正常工作时应该看到：
```
GodotAudioRecorder: Connected master bus to recording destination
GodotAudioRecorder initialized successfully
GodotAudioRecorder: Recording started with audio/webm;codecs=opus
MovieWriter: Web realtime recording mode - MediaRecorder started
```

### 2. 功能验证

- **音频捕获**: 检查是否有"Audio chunk recorded"日志
- **数据大小**: 使用`godot_audio_recorder_get_data_size()`检查
- **MIME类型**: 确认浏览器支持的音频格式

### 3. 常见问题排查

| 问题 | 可能原因 | 解决方案 |
|------|----------|----------|
| 录制器初始化失败 | AudioContext未准备好 | 确保用户交互后初始化 |
| 无音频数据 | 主音频总线未连接 | 检查GodotAudio.buses[0] |
| MediaRecorder错误 | 浏览器不支持格式 | 检查getSupportedMimeType() |
| 录制数据为空 | 音频源无输出 | 确保游戏有音频播放 |

## 📊 性能特点

### 优势
- **高质量录制**: 直接捕获Web Audio API输出
- **多格式支持**: 自动选择最佳支持格式
- **无额外延迟**: 不影响游戏音频播放
- **内存效率**: 使用浏览器原生MediaRecorder

### 注意事项
- **格式限制**: 录制格式由浏览器决定（通常为WebM/Opus）
- **数据处理**: 需要额外处理来转换为PCM格式（用于视频文件）
- **浏览器兼容**: 需要支持MediaRecorder API的现代浏览器

## 🌐 浏览器兼容性

| 浏览器 | 支持状态 | 推荐格式 |
|--------|----------|----------|
| Chrome 47+ | ✅ 完全支持 | WebM/Opus |
| Firefox 25+ | ✅ 完全支持 | WebM/Opus |
| Safari 14+ | ✅ 有限支持 | MP4 |
| Edge 79+ | ✅ 完全支持 | WebM/Opus |

## 🔮 未来扩展

### 短期改进
1. **格式转换**: 添加WebM到PCM的实时转换
2. **质量选项**: 可配置的音频质量设置
3. **缓冲优化**: 更高效的音频数据管理

### 长期计划
1. **实时流**: 支持音频流式传输
2. **效果处理**: 录制时应用音频效果
3. **多轨录制**: 分离录制不同音频通道

## 📞 技术支持

如有问题，请检查：
1. 浏览器控制台的详细错误信息
2. Godot编辑器的输出日志
3. 网络开发者工具中的音频相关API调用

---

**注意**: 此实现是对原有Godot Engine Web音频录制功能的增强，与PC端录制功能保持兼容。 