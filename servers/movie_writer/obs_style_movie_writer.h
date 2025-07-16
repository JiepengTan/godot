/**************************************************************************/
/*  obs_style_movie_writer.h                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#ifndef OBS_STYLE_MOVIE_WRITER_H
#define OBS_STYLE_MOVIE_WRITER_H

#include "movie_writer.h"
#include "thread_safe_frame_buffer.h"
#include "enhanced_avi_writer.h"
#include "independent_video_recorder.h"
#include "independent_audio_recorder.h"
#include "servers/audio/audio_driver_hybrid.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/os/mutex.h"
#include <atomic>

/**
 * OBS式独立线程录制器
 * 实现与OBS类似的录制效果：
 * - 固定时间轴录制（30fps）
 * - 游戏卡顿在视频中真实体现
 * - 音频连续录制，不受游戏帧率影响
 * - 完整的时序信息保存
 */
class ObsStyleMovieWriter : public MovieWriter {
    GDCLASS(ObsStyleMovieWriter, MovieWriter);

public:
    // OBS录制配置
    struct ObsRecordingConfig {
        // 视频参数
        uint32_t video_fps = 30;            // 固定录制帧率
        uint32_t video_width = 1920;        // 视频宽度
        uint32_t video_height = 1080;       // 视频高度
        float jpeg_quality = 0.85f;         // JPEG质量
        
        // 音频参数
        uint32_t audio_sample_rate = 48000; // 音频采样率
        uint32_t audio_channels = 2;        // 音频声道数
        uint32_t audio_chunk_ms = 10;       // 音频块时长（毫秒）
        uint32_t audio_buffer_seconds = 2;  // 音频缓冲区时长（秒）
        
        // 功能开关
        bool enable_timestamp_chunks = true;      // 启用时间戳记录
        bool enable_repeat_frame_marking = true;  // 启用重复帧标记
        bool enable_audio_monitoring = false;     // 启用音频监控
        bool enable_debug_output = true;          // 启用调试输出
        bool enable_combined_recording = true;    // 启用合并录制（video+audio到同一文件）
        
        // 性能参数
        uint32_t max_frame_buffer_size = 4;       // 最大帧缓冲区大小
    };
    
    // 录制状态
    enum RecordingState {
        STATE_UNINITIALIZED,    // 未初始化
        STATE_INITIALIZED,      // 已初始化
        STATE_RECORDING,        // 录制中
        STATE_STOPPING,         // 停止中
        STATE_ERROR            // 错误状态
    };

private:
    // 录制组件
    ThreadSafeFrameBuffer *frame_buffer = nullptr;
    EnhancedAviWriter *avi_writer = nullptr;
    IndependentVideoRecorder *video_recorder = nullptr;
    IndependentAudioRecorder *audio_recorder = nullptr;
    HybridAudioDriver *hybrid_audio_driver = nullptr;
    
    // 录制配置
    ObsRecordingConfig obs_config;
    
    // 录制状态
    RecordingState current_state = STATE_UNINITIALIZED;
    String output_file_path;
    uint32_t game_frame_sequence = 0;
    uint64_t recording_start_time = 0;
    
    // 音频驱动管理
    AudioDriver *original_audio_driver = nullptr;
    bool audio_driver_replaced = false;
    
    // 性能监控
    uint64_t last_add_frame_time = 0;
    uint32_t frames_added_count = 0;
    
    // 合并录制支持
    Thread *combined_recording_thread = nullptr;
    std::atomic<bool> combined_recording_active{false};
    Mutex audio_buffer_mutex;
    Vector<int32_t> pending_audio_samples;
    uint64_t last_video_frame_time = 0;
    uint64_t last_audio_chunk_time = 0;
    
    // 内部方法
    Error setup_components();
    void cleanup_components();
    Error setup_audio_capture();
    void restore_audio_driver();
    void update_recording_state(RecordingState new_state);
    
    // 合并录制方法
    Error setup_combined_recording();
    void cleanup_combined_recording();
    static void combined_recording_thread_function(void *p_userdata);
    void combined_recording_loop();
    Error write_combined_frame_and_audio();
    
    // 配置验证
    Error validate_config() const;
    void apply_config_to_components();

protected:
    // MovieWriter接口实现
    virtual uint32_t get_audio_mix_rate() const override;
    virtual AudioServer::SpeakerMode get_audio_speaker_mode() const override;
    virtual Error write_begin(const Size2i &p_movie_size, uint32_t p_fps, const String &p_base_path) override;
    virtual Error write_frame(const Ref<Image> &p_image, const int32_t *p_audio_data) override;
    virtual void write_end() override;

public:
    ObsStyleMovieWriter();
    ~ObsStyleMovieWriter();
    
    // MovieWriter接口
    virtual bool handles_file(const String &p_path) const override;
    virtual void get_supported_extensions(List<String> *r_extensions) const override;
    
    /**
     * 配置管理
     */
    void set_recording_config(const ObsRecordingConfig &p_config);
    const ObsRecordingConfig &get_recording_config() const { return obs_config; }
    
    /**
     * 状态查询
     */
    RecordingState get_recording_state() const { return current_state; }
    String get_state_name() const;
    bool is_recording_active() const { return current_state == STATE_RECORDING; }
    
    /**
     * 统计信息
     */
    struct CombinedStats {
        IndependentVideoRecorder::RecordingStats video_stats;
        IndependentAudioRecorder::AudioStats audio_stats;
        uint32_t game_frames_added = 0;
        uint64_t total_recording_duration_us = 0;
        float overall_repeat_frame_ratio = 0.0f;
    };
    
    CombinedStats get_combined_statistics() const;
    
    /**
     * 调试功能
     */
    String get_comprehensive_debug_info() const;
    void print_recording_summary() const;
    
    /**
     * 高级控制
     */
    Error pause_recording();
    Error resume_recording();
    bool is_paused() const;
    
    /**
     * 配置预设
     */
    static ObsRecordingConfig get_high_quality_config();
    static ObsRecordingConfig get_standard_config();
    static ObsRecordingConfig get_performance_config();
    
    /**
     * 文件格式支持
     */
    static bool is_supported_format(const String &p_extension);
};

#endif // OBS_STYLE_MOVIE_WRITER_H 