/**************************************************************************/
/*  obs_style_movie_writer.cpp                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "obs_style_movie_writer.h"
#include "core/string/print_string.h"
#include "core/config/project_settings.h"
#include "servers/audio_server.h"
#include "servers/rendering_server.h"
#include "servers/display_server.h"

ObsStyleMovieWriter::ObsStyleMovieWriter() {
    current_state = STATE_UNINITIALIZED;
    game_frame_sequence = 0;
    recording_start_time = 0;
    frames_added_count = 0;
    last_add_frame_time = 0;
    audio_driver_replaced = false;
    
    // 从项目设置加载配置
    obs_config = get_standard_config();
    
    // 应用项目设置
    if (ProjectSettings::get_singleton()->has_setting("movie_writer/obs_video_fps")) {
        obs_config.video_fps = GLOBAL_GET("movie_writer/obs_video_fps");
    }
    if (ProjectSettings::get_singleton()->has_setting("movie_writer/obs_video_quality")) {
        obs_config.jpeg_quality = GLOBAL_GET("movie_writer/obs_video_quality");
    }
    if (ProjectSettings::get_singleton()->has_setting("movie_writer/obs_audio_sample_rate")) {
        obs_config.audio_sample_rate = GLOBAL_GET("movie_writer/obs_audio_sample_rate");
    }
    if (ProjectSettings::get_singleton()->has_setting("movie_writer/obs_audio_channels")) {
        obs_config.audio_channels = GLOBAL_GET("movie_writer/obs_audio_channels");
    }
    if (ProjectSettings::get_singleton()->has_setting("movie_writer/obs_enable_timestamp_chunks")) {
        obs_config.enable_timestamp_chunks = GLOBAL_GET("movie_writer/obs_enable_timestamp_chunks");
    }
    if (ProjectSettings::get_singleton()->has_setting("movie_writer/obs_enable_repeat_frame_marking")) {
        obs_config.enable_repeat_frame_marking = GLOBAL_GET("movie_writer/obs_enable_repeat_frame_marking");
    }
    if (ProjectSettings::get_singleton()->has_setting("movie_writer/obs_enable_debug_output")) {
        obs_config.enable_debug_output = GLOBAL_GET("movie_writer/obs_enable_debug_output");
    }
}

ObsStyleMovieWriter::~ObsStyleMovieWriter() {
    if (current_state == STATE_RECORDING) {
        write_end();
    }
    cleanup_components();
}

bool ObsStyleMovieWriter::handles_file(const String &p_path) const {
    return p_path.get_extension().to_lower() == "avi";
}

void ObsStyleMovieWriter::get_supported_extensions(List<String> *r_extensions) const {
    r_extensions->push_back("avi");
}

bool ObsStyleMovieWriter::is_supported_format(const String &p_extension) {
    return p_extension.to_lower() == "avi";
}

uint32_t ObsStyleMovieWriter::get_audio_mix_rate() const {
    return obs_config.audio_sample_rate;
}

AudioServer::SpeakerMode ObsStyleMovieWriter::get_audio_speaker_mode() const {
    return obs_config.audio_channels == 2 ? AudioServer::SPEAKER_MODE_STEREO : AudioServer::SPEAKER_SURROUND_31;
}

Error ObsStyleMovieWriter::write_begin(const Size2i &p_movie_size, uint32_t p_fps, const String &p_base_path) {
    if (current_state != STATE_UNINITIALIZED) {
        ERR_PRINT("ObsStyleMovieWriter: 录制器状态不正确");
        return ERR_INVALID_PARAMETER;
    }
    
    output_file_path = p_base_path;
    
    // 更新配置中的视频尺寸
    obs_config.video_width = p_movie_size.width;
    obs_config.video_height = p_movie_size.height;
    
    if (obs_config.enable_debug_output) {
        print_line("=== OBS式录制开始 ===");
        print_line(String("输出文件: ") + output_file_path);
        print_line(String("视频分辨率: ") + String::num_int64(obs_config.video_width) + "x" + String::num_int64(obs_config.video_height));
        print_line(String("目标FPS: ") + String::num_int64(obs_config.video_fps));
        print_line(String("音频配置: ") + String::num_int64(obs_config.audio_sample_rate) + "Hz, " + String::num_int64(obs_config.audio_channels) + "ch");
    }
    
    // 验证配置
    Error config_error = validate_config();
    if (config_error != OK) {
        return config_error;
    }
    
    // 设置录制组件
    Error setup_error = setup_components();
    if (setup_error != OK) {
        cleanup_components();
        return setup_error;
    }
    
    // 设置音频捕获
    Error audio_error = setup_audio_capture();
    if (audio_error != OK) {
        cleanup_components();
        return audio_error;
    }
    
    // 启动录制线程
    Error video_start_error = video_recorder->start_recording();
    if (video_start_error != OK) {
        ERR_PRINT("ObsStyleMovieWriter: 启动视频录制失败");
        cleanup_components();
        return video_start_error;
    }
    
    Error audio_start_error = audio_recorder->start_recording();
    if (audio_start_error != OK) {
        ERR_PRINT("ObsStyleMovieWriter: 启动音频录制失败");
        video_recorder->stop_recording();
        cleanup_components();
        return audio_start_error;
    }
    
    // 更新状态
    update_recording_state(STATE_RECORDING);
    recording_start_time = OS::get_singleton()->get_ticks_usec();
    game_frame_sequence = 0;
    frames_added_count = 0;
    
    if (obs_config.enable_debug_output) {
        print_line("OBS式录制初始化完成，开始录制...");
    }
    
    return OK;
}

Error ObsStyleMovieWriter::write_frame(const Ref<Image> &p_image, const int32_t *p_audio_data) {
    if (current_state != STATE_RECORDING) {
        return ERR_UNCONFIGURED;
    }
    
    uint64_t current_time = OS::get_singleton()->get_ticks_usec();
    
    // 更新游戏帧数据到双缓冲区
    if (p_image.is_valid()) {
        frame_buffer->update_frame(p_image, current_time, game_frame_sequence);
        game_frame_sequence++;
        frames_added_count++;
    }
    
    last_add_frame_time = current_time;
    
    // 注意：我们不直接处理音频数据，因为音频是通过HybridAudioDriver捕获的
    // p_audio_data参数在OBS式录制中不使用
    
    return OK;
}

void ObsStyleMovieWriter::write_end() {
    if (current_state != STATE_RECORDING) {
        return;
    }
    
    if (obs_config.enable_debug_output) {
        print_line("ObsStyleMovieWriter: 停止录制...");
    }
    
    update_recording_state(STATE_STOPPING);
    
    // 停止录制线程
    if (video_recorder) {
        video_recorder->stop_recording();
    }
    
    if (audio_recorder) {
        audio_recorder->stop_recording();
    }
    
    // 关闭AVI文件
    if (avi_writer) {
        avi_writer->close();
    }
    
    // 恢复音频驱动
    restore_audio_driver();
    
    // 打印录制摘要
    if (obs_config.enable_debug_output) {
        print_recording_summary();
    }
    
    // 清理组件
    cleanup_components();
    
    update_recording_state(STATE_UNINITIALIZED);
    
    print_line("=== OBS式录制完成 ===");
}

Error ObsStyleMovieWriter::setup_components() {
    // 创建双缓冲区
    frame_buffer = new ThreadSafeFrameBuffer();
    if (!frame_buffer) {
        ERR_PRINT("ObsStyleMovieWriter: 无法创建帧缓冲区");
        return ERR_OUT_OF_MEMORY;
    }
    
    // 创建AVI写入器
    avi_writer = new EnhancedAviWriter();
    if (!avi_writer) {
        ERR_PRINT("ObsStyleMovieWriter: 无法创建AVI写入器");
        return ERR_OUT_OF_MEMORY;
    }
    
    // 初始化AVI写入器
    Error avi_error = avi_writer->open(
        output_file_path,
        obs_config.video_width,
        obs_config.video_height,
        obs_config.video_fps,
        obs_config.audio_sample_rate,
        obs_config.audio_channels,
        obs_config.jpeg_quality
    );
    
    if (avi_error != OK) {
        ERR_PRINT("ObsStyleMovieWriter: AVI文件初始化失败");
        return avi_error;
    }
    
    // 创建视频录制器
    video_recorder = new IndependentVideoRecorder();
    if (!video_recorder) {
        ERR_PRINT("ObsStyleMovieWriter: 无法创建视频录制器");
        return ERR_OUT_OF_MEMORY;
    }
    
    // 配置视频录制器
    IndependentVideoRecorder::RecordingConfig video_config;
    video_config.target_fps = obs_config.video_fps;
    video_config.video_width = obs_config.video_width;
    video_config.video_height = obs_config.video_height;
    video_config.jpeg_quality = obs_config.jpeg_quality;
    video_config.enable_timestamp_chunks = obs_config.enable_timestamp_chunks;
    video_config.enable_repeat_frame_marking = obs_config.enable_repeat_frame_marking;
    
    Error video_init_error = video_recorder->initialize(frame_buffer, avi_writer, video_config);
    if (video_init_error != OK) {
        ERR_PRINT("ObsStyleMovieWriter: 视频录制器初始化失败");
        return video_init_error;
    }
    
    // 创建音频录制器
    audio_recorder = new IndependentAudioRecorder();
    if (!audio_recorder) {
        ERR_PRINT("ObsStyleMovieWriter: 无法创建音频录制器");
        return ERR_OUT_OF_MEMORY;
    }
    
    // 配置音频录制器
    IndependentAudioRecorder::AudioConfig audio_config;
    audio_config.sample_rate = obs_config.audio_sample_rate;
    audio_config.channels = obs_config.audio_channels;
    audio_config.chunk_size = (obs_config.audio_sample_rate * obs_config.audio_chunk_ms) / 1000;
    audio_config.buffer_size_seconds = obs_config.audio_buffer_seconds;
    audio_config.enable_audio_monitoring = obs_config.enable_audio_monitoring;
    
    // 注意：audio_recorder需要在setup_audio_capture中与HybridAudioDriver关联
    
    update_recording_state(STATE_INITIALIZED);
    
    return OK;
}

void ObsStyleMovieWriter::cleanup_components() {
    if (video_recorder) {
        delete video_recorder;
        video_recorder = nullptr;
    }
    
    if (audio_recorder) {
        delete audio_recorder;
        audio_recorder = nullptr;
    }
    
    if (avi_writer) {
        delete avi_writer;
        avi_writer = nullptr;
    }
    
    if (frame_buffer) {
        delete frame_buffer;
        frame_buffer = nullptr;
    }
    
    hybrid_audio_driver = nullptr; // 不删除，只是清除引用
}

Error ObsStyleMovieWriter::setup_audio_capture() {
    // 获取当前音频驱动
    original_audio_driver = AudioServer::get_singleton()->get_audio_driver();
    
    // 创建或获取HybridAudioDriver
    hybrid_audio_driver = new HybridAudioDriver();
    if (!hybrid_audio_driver) {
        ERR_PRINT("ObsStyleMovieWriter: 无法创建HybridAudioDriver");
        return ERR_OUT_OF_MEMORY;
    }
    
    // 初始化HybridAudioDriver
    Error init_error = hybrid_audio_driver->init(
        obs_config.audio_sample_rate,
        obs_config.audio_channels == 2 ? AudioDriver::SPEAKER_MODE_STEREO : AudioDriver::SPEAKER_SURROUND_31
    );
    
    if (init_error != OK) {
        ERR_PRINT("ObsStyleMovieWriter: HybridAudioDriver初始化失败");
        delete hybrid_audio_driver;
        hybrid_audio_driver = nullptr;
        return init_error;
    }
    
    // 配置音频录制器
    IndependentAudioRecorder::AudioConfig audio_config;
    audio_config.sample_rate = obs_config.audio_sample_rate;
    audio_config.channels = obs_config.audio_channels;
    audio_config.chunk_size = (obs_config.audio_sample_rate * obs_config.audio_chunk_ms) / 1000;
    audio_config.buffer_size_seconds = obs_config.audio_buffer_seconds;
    audio_config.enable_audio_monitoring = obs_config.enable_audio_monitoring;
    
    Error audio_init_error = audio_recorder->initialize(hybrid_audio_driver, avi_writer, audio_config);
    if (audio_init_error != OK) {
        ERR_PRINT("ObsStyleMovieWriter: 音频录制器初始化失败");
        return audio_init_error;
    }
    
    // 向HybridAudioDriver注册音频录制器
    hybrid_audio_driver->register_audio_recorder(audio_recorder);
    
    // 启用录制模式
    hybrid_audio_driver->enable_recording(true);
    
    // 启动HybridAudioDriver
    hybrid_audio_driver->start();
    
    // 替换音频驱动（注意：这是一个敏感操作）
    // 在实际实现中，可能需要更复杂的音频驱动替换逻辑
    // AudioServer::get_singleton()->set_audio_driver(hybrid_audio_driver);
    // audio_driver_replaced = true;
    
    return OK;
}

void ObsStyleMovieWriter::restore_audio_driver() {
    if (hybrid_audio_driver) {
        // 注销音频录制器
        if (audio_recorder) {
            hybrid_audio_driver->unregister_audio_recorder(audio_recorder);
        }
        
        hybrid_audio_driver->enable_recording(false);
        hybrid_audio_driver->finish();
    }
    
    if (audio_driver_replaced && original_audio_driver) {
        // AudioServer::get_singleton()->set_audio_driver(original_audio_driver);
        audio_driver_replaced = false;
    }
    
    if (hybrid_audio_driver) {
        delete hybrid_audio_driver;
        hybrid_audio_driver = nullptr;
    }
    
    original_audio_driver = nullptr;
}

void ObsStyleMovieWriter::update_recording_state(RecordingState new_state) {
    if (current_state != new_state) {
        current_state = new_state;
        
        if (obs_config.enable_debug_output) {
            print_line(String("ObsStyleMovieWriter: 状态变更为 ") + get_state_name());
        }
    }
}

Error ObsStyleMovieWriter::validate_config() const {
    if (obs_config.video_width == 0 || obs_config.video_height == 0) {
        ERR_PRINT("ObsStyleMovieWriter: 无效的视频分辨率");
        return ERR_INVALID_PARAMETER;
    }
    
    if (obs_config.video_fps == 0 || obs_config.video_fps > 120) {
        ERR_PRINT("ObsStyleMovieWriter: 无效的视频帧率");
        return ERR_INVALID_PARAMETER;
    }
    
    if (obs_config.audio_sample_rate < 8000 || obs_config.audio_sample_rate > 192000) {
        ERR_PRINT("ObsStyleMovieWriter: 无效的音频采样率");
        return ERR_INVALID_PARAMETER;
    }
    
    if (obs_config.audio_channels == 0 || obs_config.audio_channels > 8) {
        ERR_PRINT("ObsStyleMovieWriter: 无效的音频声道数");
        return ERR_INVALID_PARAMETER;
    }
    
    if (obs_config.jpeg_quality < 0.1f || obs_config.jpeg_quality > 1.0f) {
        ERR_PRINT("ObsStyleMovieWriter: 无效的JPEG质量");
        return ERR_INVALID_PARAMETER;
    }
    
    return OK;
}

String ObsStyleMovieWriter::get_state_name() const {
    switch (current_state) {
        case STATE_UNINITIALIZED: return "未初始化";
        case STATE_INITIALIZED: return "已初始化";
        case STATE_RECORDING: return "录制中";
        case STATE_STOPPING: return "停止中";
        case STATE_ERROR: return "错误状态";
        default: return "未知状态";
    }
}

// 配置预设
ObsStyleMovieWriter::ObsRecordingConfig ObsStyleMovieWriter::get_high_quality_config() {
    ObsRecordingConfig config;
    config.video_fps = 30;
    config.jpeg_quality = 0.95f;
    config.audio_sample_rate = 48000;
    config.audio_channels = 2;
    config.audio_buffer_seconds = 3;
    config.enable_timestamp_chunks = true;
    config.enable_repeat_frame_marking = true;
    config.enable_debug_output = true;
    return config;
}

ObsStyleMovieWriter::ObsRecordingConfig ObsStyleMovieWriter::get_standard_config() {
    ObsRecordingConfig config;
    config.video_fps = 30;
    config.jpeg_quality = 0.85f;
    config.audio_sample_rate = 48000;
    config.audio_channels = 2;
    config.audio_buffer_seconds = 2;
    config.enable_timestamp_chunks = true;
    config.enable_repeat_frame_marking = true;
    config.enable_debug_output = true;
    return config;
}

ObsStyleMovieWriter::ObsRecordingConfig ObsStyleMovieWriter::get_performance_config() {
    ObsRecordingConfig config;
    config.video_fps = 30;
    config.jpeg_quality = 0.75f;
    config.audio_sample_rate = 44100;
    config.audio_channels = 2;
    config.audio_buffer_seconds = 1;
    config.enable_timestamp_chunks = false;
    config.enable_repeat_frame_marking = true;
    config.enable_debug_output = false;
    return config;
}

// 统计信息和调试功能
ObsStyleMovieWriter::CombinedStats ObsStyleMovieWriter::get_combined_statistics() const {
    CombinedStats stats;
    
    if (video_recorder) {
        stats.video_stats = video_recorder->get_statistics();
    }
    
    if (audio_recorder) {
        stats.audio_stats = audio_recorder->get_statistics();
    }
    
    stats.game_frames_added = frames_added_count;
    stats.total_recording_duration_us = OS::get_singleton()->get_ticks_usec() - recording_start_time;
    
    if (video_recorder) {
        stats.overall_repeat_frame_ratio = video_recorder->get_repeat_frame_ratio();
    }
    
    return stats;
}

String ObsStyleMovieWriter::get_comprehensive_debug_info() const {
    String info;
    info += "=== ObsStyleMovieWriter 综合调试信息 ===\n";
    info += String("当前状态: ") + get_state_name() + "\n";
    info += String("输出文件: ") + output_file_path + "\n";
    info += String("游戏帧序号: ") + String::num_int64(game_frame_sequence) + "\n";
    info += String("游戏帧添加数: ") + String::num_int64(frames_added_count) + "\n";
    
    if (recording_start_time > 0) {
        uint64_t duration = OS::get_singleton()->get_ticks_usec() - recording_start_time;
        info += String("录制时长: ") + String::num_real(duration / 1000000.0) + "秒\n";
    }
    
    info += "\n--- 录制配置 ---\n";
    info += String("视频FPS: ") + String::num_int64(obs_config.video_fps) + "\n";
    info += String("视频分辨率: ") + String::num_int64(obs_config.video_width) + "x" + String::num_int64(obs_config.video_height) + "\n";
    info += String("JPEG质量: ") + String::num_real(obs_config.jpeg_quality) + "\n";
    info += String("音频采样率: ") + String::num_int64(obs_config.audio_sample_rate) + "Hz\n";
    info += String("音频声道: ") + String::num_int64(obs_config.audio_channels) + "\n";
    
    if (video_recorder) {
        info += "\n--- 视频录制器信息 ---\n";
        info += video_recorder->get_debug_info() + "\n";
    }
    
    if (audio_recorder) {
        info += "\n--- 音频录制器信息 ---\n";
        info += audio_recorder->get_debug_info() + "\n";
    }
    
    if (frame_buffer) {
        info += "\n--- 帧缓冲区信息 ---\n";
        info += String("总更新次数: ") + String::num_int64(frame_buffer->get_total_updates()) + "\n";
        info += String("缓冲区切换次数: ") + String::num_int64(frame_buffer->get_buffer_switches()) + "\n";
        info += String("最后序列号: ") + String::num_int64(frame_buffer->get_last_sequence()) + "\n";
    }
    
    info += "\n=========================================\n";
    
    return info;
}

void ObsStyleMovieWriter::print_recording_summary() const {
    CombinedStats stats = get_combined_statistics();
    
    print_line("=== OBS式录制摘要 ===");
    print_line(String("录制时长: ") + String::num_real(stats.total_recording_duration_us / 1000000.0) + "秒");
    print_line(String("游戏帧数: ") + String::num_int64(stats.game_frames_added));
    print_line(String("录制视频帧数: ") + String::num_int64(stats.video_stats.total_recorded_frames));
    print_line(String("新帧数: ") + String::num_int64(stats.video_stats.new_frames_count));
    print_line(String("重复帧数: ") + String::num_int64(stats.video_stats.repeated_frames_count));
    print_line(String("重复帧比例: ") + String::num_real(stats.overall_repeat_frame_ratio * 100.0f) + "%");
    print_line(String("音频块数: ") + String::num_int64(stats.audio_stats.total_chunks_recorded));
    print_line(String("音频样本数: ") + String::num_int64(stats.audio_stats.total_samples_recorded));
    print_line(String("音频缓冲区溢出: ") + String::num_int64(stats.audio_stats.buffer_overruns));
    print_line(String("音频缓冲区下溢: ") + String::num_int64(stats.audio_stats.buffer_underruns));
    
    if (avi_writer) {
        print_line(String("AVI文件视频帧数: ") + String::num_int64(avi_writer->get_video_frame_count()));
        print_line(String("AVI文件音频块数: ") + String::num_int64(avi_writer->get_audio_chunk_count()));
    }
    
    print_line("==================");
}

void ObsStyleMovieWriter::set_recording_config(const ObsRecordingConfig &p_config) {
    if (current_state == STATE_RECORDING) {
        ERR_PRINT("ObsStyleMovieWriter: 无法在录制时更改配置");
        return;
    }
    
    obs_config = p_config;
    
    if (obs_config.enable_debug_output) {
        print_line("ObsStyleMovieWriter: 配置已更新");
    }
}

Error ObsStyleMovieWriter::pause_recording() {
    ERR_PRINT("ObsStyleMovieWriter: 暂停功能暂未实现");
    return ERR_UNAVAILABLE;
}

Error ObsStyleMovieWriter::resume_recording() {
    ERR_PRINT("ObsStyleMovieWriter: 恢复功能暂未实现");
    return ERR_UNAVAILABLE;
}

bool ObsStyleMovieWriter::is_paused() const {
    return false; // 暂停功能暂未实现
} 