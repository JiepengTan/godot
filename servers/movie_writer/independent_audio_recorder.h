/**************************************************************************/
/*  independent_audio_recorder.h                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#ifndef INDEPENDENT_AUDIO_RECORDER_H
#define INDEPENDENT_AUDIO_RECORDER_H

#include "simple_audio_writer.h"
#include "core/os/thread.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/templates/ring_buffer.h"
#include <atomic>

// 前向声明
class HybridAudioDriver;

/**
 * 独立音频录制线程
 * 以固定采样率连续运行，与游戏主线程完全独立
 * 从音频驱动获取输出数据，生成连续的音频流
 */
class IndependentAudioRecorder {
public:
    // 音频录制配置
    struct AudioConfig {
        uint32_t sample_rate;       // 采样率
        uint32_t channels;              // 声道数
        uint32_t chunk_size;          // 每次处理的样本数 (10ms at 48kHz)
        uint32_t buffer_size_seconds;   // 环形缓冲区大小（秒）
        bool enable_audio_monitoring; // 启用音频监控
        
        AudioConfig() :
            sample_rate(48000),
            channels(2),
            chunk_size(480),
            buffer_size_seconds(2),
            enable_audio_monitoring(false) {}
    };
    
    // 音频统计信息
    struct AudioStats {
        uint64_t total_chunks_recorded = 0;    // 总录制的音频块数
        uint64_t total_samples_recorded = 0;   // 总录制的样本数
        uint64_t buffer_overruns = 0;          // 缓冲区溢出次数
        uint64_t buffer_underruns = 0;         // 缓冲区下溢次数
        uint64_t recording_duration_us = 0;    // 录制时长（微秒）
        uint32_t current_buffer_level = 0;     // 当前缓冲区使用率(0-100)
        uint64_t avg_chunk_process_time_us = 0; // 平均块处理时间
    };

private:
    // 录制参数（固定）
    static const uint64_t CHUNK_INTERVAL_USEC = 10000; // 10ms间隔
    
    // 线程控制
    Thread recording_thread;
    std::atomic<bool> recording_active{false};
    std::atomic<bool> thread_started{false};
    
    // 音频配置
    AudioConfig config;
    
    // 数据源和输出
    HybridAudioDriver *audio_driver = nullptr;
    Ref<SimpleAudioWriter> audio_writer;
    
    // 音频环形缓冲区
    RingBuffer<int32_t> audio_ring_buffer;
    mutable Mutex buffer_mutex;
    std::atomic<uint32_t> buffer_read_pos{0};
    std::atomic<uint32_t> buffer_write_pos{0};
    uint32_t buffer_size = 0;
    
    // 临时缓冲区
    Vector<int32_t> temp_audio_buffer;
    Vector<int32_t> chunk_buffer;
    
    // 统计信息
    AudioStats stats;
    uint64_t recording_start_time = 0;
    mutable Mutex stats_mutex;
    
    // 线程主循环
    static void recording_thread_func(void *p_userdata);
    void recording_loop();
    
    // 内部处理方法
    bool process_audio_chunk(uint64_t current_recording_time);
    void update_statistics(uint64_t chunk_process_start_time, uint32_t samples_processed);
    void update_buffer_level();
    
    // 缓冲区管理
    bool read_audio_chunk(Vector<int32_t> &output_buffer, uint32_t requested_samples);
    void handle_buffer_underrun();
    void handle_buffer_overrun();

public:
    IndependentAudioRecorder();
    ~IndependentAudioRecorder();
    
    /**
     * 初始化录制器
     */
    Error initialize(HybridAudioDriver *p_audio_driver, 
                    const String &p_audio_path,
                    const AudioConfig &p_config = AudioConfig());
    
    /**
     * 开始录制
     */
    Error start_recording();
    
    /**
     * 停止录制
     */
    void stop_recording();
    
    /**
     * 检查录制状态
     */
    bool is_recording() const { return recording_active.load(); }
    bool is_thread_running() const { return thread_started.load(); }
    
    /**
     * 音频数据输入接口（由HybridAudioDriver调用）
     */
    void on_audio_output(const int32_t *p_buffer, int p_frame_count);
    
    /**
     * 获取音频统计信息
     */
    AudioStats get_statistics() const;
    
    /**
     * 获取缓冲区状态
     */
    uint32_t get_available_samples() const;
    bool has_audio_data() const;
    float get_buffer_usage_ratio() const;
    
    /**
     * 更新配置
     */
    void update_config(const AudioConfig &p_config);
    
    /**
     * 重置统计信息
     */
    void reset_statistics();
    
    /**
     * 获取调试信息
     */
    String get_debug_info() const;
    
    /**
     * 获取音频配置
     */
    const AudioConfig &get_config() const { return config; }
};

#endif // INDEPENDENT_AUDIO_RECORDER_H 