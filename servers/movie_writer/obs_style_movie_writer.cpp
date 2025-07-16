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

ObsStyleMovieWriter::ObsStyleMovieWriter() :
    current_state(STATE_UNINITIALIZED),
    game_frame_sequence(0),
    recording_start_time(0),
    audio_driver_replaced(false),
    last_add_frame_time(0),
    frames_added_count(0) {
    
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
        ERR_PRINT("ObsStyleMovieWriter: Recorder state is incorrect");
        return ERR_INVALID_PARAMETER;
    }
    
    output_file_path = p_base_path;
    
    // 更新配置中的视频尺寸
    obs_config.video_width = p_movie_size.width;
    obs_config.video_height = p_movie_size.height;
    
    if (obs_config.enable_debug_output) {
        print_line("=== OBS-style Recording Started ===");
        print_line(String("Output file: ") + output_file_path);
        print_line(String("Video resolution: ") + String::num_int64(obs_config.video_width) + "x" + String::num_int64(obs_config.video_height));
        print_line(String("Target FPS: ") + String::num_int64(obs_config.video_fps));
        print_line(String("Audio config: ") + String::num_int64(obs_config.audio_sample_rate) + "Hz, " + String::num_int64(obs_config.audio_channels) + "ch");
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
        ERR_PRINT("ObsStyleMovieWriter: Failed to start video recording");
        cleanup_components();
        return video_start_error;
    }
    
    Error audio_start_error = audio_recorder->start_recording();
    if (audio_start_error != OK) {
        ERR_PRINT("ObsStyleMovieWriter: Failed to start audio recording");
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
        print_line("OBS-style recording initialization completed, starting recording...");
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
        print_line("ObsStyleMovieWriter: Stopping recording...");
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
    
    print_line("=== OBS-style Recording Completed ===");
}

Error ObsStyleMovieWriter::setup_components() {
    // 创建双缓冲区
    frame_buffer = new ThreadSafeFrameBuffer();
    if (!frame_buffer) {
        ERR_PRINT("ObsStyleMovieWriter: Failed to create frame buffer");
        return ERR_OUT_OF_MEMORY;
    }
    
    // 创建AVI写入器
    avi_writer = new EnhancedAviWriter();
    if (!avi_writer) {
        ERR_PRINT("ObsStyleMovieWriter: Failed to create AVI writer");
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
        ERR_PRINT("ObsStyleMovieWriter: AVI file initialization failed");
        return avi_error;
    }
    
    // 创建视频录制器
    video_recorder = new IndependentVideoRecorder();
    if (!video_recorder) {
        ERR_PRINT("ObsStyleMovieWriter: Failed to create video recorder");
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
    
    Error video_init_error = video_recorder->initialize(frame_buffer, output_file_path + "_video.avi", video_config);
    if (video_init_error != OK) {
        ERR_PRINT("ObsStyleMovieWriter: Video recorder initialization failed");
        return video_init_error;
    }
    
    // 创建音频录制器
    audio_recorder = new IndependentAudioRecorder();
    if (!audio_recorder) {
        ERR_PRINT("ObsStyleMovieWriter: Failed to create audio recorder");
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
    
    hybrid_audio_driver = nullptr; // 由MovieWriter管理，只清除引用
}

Error ObsStyleMovieWriter::setup_audio_capture() {
    // 重用MovieWriter的HybridAudioDriver，避免冲突
    hybrid_audio_driver = MovieWriter::get_hybrid_audio_driver();
    if (!hybrid_audio_driver) {
        ERR_PRINT("ObsStyleMovieWriter: MovieWriter's HybridAudioDriver not available");
        return ERR_UNCONFIGURED;
    }
    
    print_line("ObsStyleMovieWriter: Reusing MovieWriter's HybridAudioDriver");
    
    // 配置音频录制器
    IndependentAudioRecorder::AudioConfig audio_config;
    audio_config.sample_rate = obs_config.audio_sample_rate;
    audio_config.channels = obs_config.audio_channels;
    audio_config.chunk_size = (obs_config.audio_sample_rate * obs_config.audio_chunk_ms) / 1000;
    audio_config.buffer_size_seconds = obs_config.audio_buffer_seconds;
    audio_config.enable_audio_monitoring = obs_config.enable_audio_monitoring;
    
    Error audio_init_error = audio_recorder->initialize(hybrid_audio_driver, output_file_path + "_audio.avi", audio_config);
    if (audio_init_error != OK) {
        ERR_PRINT("ObsStyleMovieWriter: Audio recorder initialization failed");
        return audio_init_error;
    }
    
    // 向HybridAudioDriver注册音频录制器
    hybrid_audio_driver->register_audio_recorder(audio_recorder);
    
    // 启用录制模式
    hybrid_audio_driver->enable_recording(true);
    
    // HybridAudioDriver已经由MovieWriter启动和注册了，无需重复操作
    print_line("ObsStyleMovieWriter: Using existing HybridAudioDriver from MovieWriter");
    
    return OK;
}

void ObsStyleMovieWriter::restore_audio_driver() {
    if (hybrid_audio_driver && audio_recorder) {
        // 注销音频录制器（但不删除或停止HybridAudioDriver，它由MovieWriter管理）
        hybrid_audio_driver->unregister_audio_recorder(audio_recorder);
        print_line("ObsStyleMovieWriter: Audio recorder unregistered from HybridAudioDriver");
    }
    
    // 只清除引用，不删除对象（HybridAudioDriver由MovieWriter管理）
    hybrid_audio_driver = nullptr;
    original_audio_driver = nullptr;
}

void ObsStyleMovieWriter::update_recording_state(RecordingState new_state) {
    if (current_state != new_state) {
        current_state = new_state;
        
        if (obs_config.enable_debug_output) {
            print_line(String("ObsStyleMovieWriter: State changed to ") + get_state_name());
        }
    }
}

Error ObsStyleMovieWriter::validate_config() const {
    if (obs_config.video_width == 0 || obs_config.video_height == 0) {
        ERR_PRINT("ObsStyleMovieWriter: Invalid video resolution");
        return ERR_INVALID_PARAMETER;
    }
    
    if (obs_config.video_fps == 0 || obs_config.video_fps > 120) {
        ERR_PRINT("ObsStyleMovieWriter: Invalid video frame rate");
        return ERR_INVALID_PARAMETER;
    }
    
    if (obs_config.audio_sample_rate < 8000 || obs_config.audio_sample_rate > 192000) {
        ERR_PRINT("ObsStyleMovieWriter: Invalid audio sample rate");
        return ERR_INVALID_PARAMETER;
    }
    
    if (obs_config.audio_channels == 0 || obs_config.audio_channels > 8) {
        ERR_PRINT("ObsStyleMovieWriter: Invalid audio channel count");
        return ERR_INVALID_PARAMETER;
    }
    
    if (obs_config.jpeg_quality < 0.1f || obs_config.jpeg_quality > 1.0f) {
        ERR_PRINT("ObsStyleMovieWriter: Invalid JPEG quality");
        return ERR_INVALID_PARAMETER;
    }
    
    return OK;
}

String ObsStyleMovieWriter::get_state_name() const {
    switch (current_state) {
        case STATE_UNINITIALIZED: return "Uninitialized";
        case STATE_INITIALIZED: return "Initialized";
        case STATE_RECORDING: return "Recording";
        case STATE_STOPPING: return "Stopping";
        case STATE_ERROR: return "Error state";
        default: return "Unknown state";
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
    info += "=== ObsStyleMovieWriter Comprehensive Debug Info ===\n";
    info += String("Current state: ") + get_state_name() + "\n";
    info += String("Output file: ") + output_file_path + "\n";
    info += String("Game frame sequence: ") + String::num_int64(game_frame_sequence) + "\n";
    info += String("Game frames added: ") + String::num_int64(frames_added_count) + "\n";
    
    if (recording_start_time > 0) {
        uint64_t duration = OS::get_singleton()->get_ticks_usec() - recording_start_time;
        info += String("Recording duration: ") + String::num_real(duration / 1000000.0) + " seconds\n";
    }
    
    info += "\n--- Recording Configuration ---\n";
    info += String("Video FPS: ") + String::num_int64(obs_config.video_fps) + "\n";
    info += String("Video resolution: ") + String::num_int64(obs_config.video_width) + "x" + String::num_int64(obs_config.video_height) + "\n";
    info += String("JPEG quality: ") + String::num_real(obs_config.jpeg_quality) + "\n";
    info += String("Audio sample rate: ") + String::num_int64(obs_config.audio_sample_rate) + "Hz\n";
    info += String("Audio channels: ") + String::num_int64(obs_config.audio_channels) + "\n";
    
    if (video_recorder) {
        info += "\n--- Video Recorder Info ---\n";
        info += video_recorder->get_debug_info() + "\n";
    }
    
    if (audio_recorder) {
        info += "\n--- Audio Recorder Info ---\n";
        info += audio_recorder->get_debug_info() + "\n";
    }
    
    if (frame_buffer) {
        info += "\n--- Frame Buffer Info ---\n";
        info += String("Total updates: ") + String::num_int64(frame_buffer->get_total_updates()) + "\n";
        info += String("Buffer switches: ") + String::num_int64(frame_buffer->get_buffer_switches()) + "\n";
        info += String("Last sequence: ") + String::num_int64(frame_buffer->get_last_sequence()) + "\n";
    }
    
    info += "\n=========================================\n";
    
    return info;
}

void ObsStyleMovieWriter::print_recording_summary() const {
    CombinedStats stats = get_combined_statistics();
    
    print_line("=== OBS-style Recording Summary ===");
    print_line(String("Recording duration: ") + String::num_real(stats.total_recording_duration_us / 1000000.0) + " seconds");
    print_line(String("Game frames: ") + String::num_int64(stats.game_frames_added));
    print_line(String("Recorded video frames: ") + String::num_int64(stats.video_stats.total_recorded_frames));
    print_line(String("New frames: ") + String::num_int64(stats.video_stats.new_frames_count));
    print_line(String("Repeated frames: ") + String::num_int64(stats.video_stats.repeated_frames_count));
    print_line(String("Repeated frame ratio: ") + String::num_real(stats.overall_repeat_frame_ratio * 100.0f) + "%");
    print_line(String("Audio chunks: ") + String::num_int64(stats.audio_stats.total_chunks_recorded));
    print_line(String("Audio samples: ") + String::num_int64(stats.audio_stats.total_samples_recorded));
    print_line(String("Audio buffer overruns: ") + String::num_int64(stats.audio_stats.buffer_overruns));
    print_line(String("Audio buffer underruns: ") + String::num_int64(stats.audio_stats.buffer_underruns));
    
    if (avi_writer) {
        print_line(String("AVI file video frames: ") + String::num_int64(avi_writer->get_video_frame_count()));
        print_line(String("AVI file audio chunks: ") + String::num_int64(avi_writer->get_audio_chunk_count()));
    }
    
    print_line("==================");
}

void ObsStyleMovieWriter::set_recording_config(const ObsRecordingConfig &p_config) {
    if (current_state == STATE_RECORDING) {
        ERR_PRINT("ObsStyleMovieWriter: Cannot change configuration while recording");
        return;
    }
    
    obs_config = p_config;
    
    if (obs_config.enable_debug_output) {
        print_line("ObsStyleMovieWriter: Configuration updated");
    }
}

Error ObsStyleMovieWriter::pause_recording() {
    ERR_PRINT("ObsStyleMovieWriter: Pause functionality not yet implemented");
    return ERR_UNAVAILABLE;
}

Error ObsStyleMovieWriter::resume_recording() {
    ERR_PRINT("ObsStyleMovieWriter: Resume functionality not yet implemented");
    return ERR_UNAVAILABLE;
}

bool ObsStyleMovieWriter::is_paused() const {
    return false; // Pause functionality not yet implemented
} 