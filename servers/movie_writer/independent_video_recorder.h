/**************************************************************************/
/*  independent_video_recorder.h                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#ifndef INDEPENDENT_VIDEO_RECORDER_H
#define INDEPENDENT_VIDEO_RECORDER_H

#include "thread_safe_frame_buffer.h"
#include "simple_video_writer.h"
#include "core/os/thread.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include <atomic>
#include <chrono>

/**
 * 独立视频录制线程
 * 以固定30fps运行，与游戏主线程完全独立
 * 从双缓冲区读取画面数据，生成标准时间轴的录制视频
 */
class IndependentVideoRecorder { 
public:
    // 录制配置
    struct RecordingConfig {
        uint32_t target_fps;           // 目标录制帧率
        uint32_t video_width;        // 视频宽度
        uint32_t video_height;       // 视频高度
        float jpeg_quality;         // JPEG质量
        bool enable_timestamp_chunks; // 启用时间戳记录
        bool enable_repeat_frame_marking; // 启用重复帧标记
        
        RecordingConfig() :
            target_fps(30),
            video_width(1920),
            video_height(1080),
            jpeg_quality(0.85f),
            enable_timestamp_chunks(true),
            enable_repeat_frame_marking(true) {}
    };
    
    // 录制统计
    struct RecordingStats {
        uint32_t total_recorded_frames = 0;     // 总录制帧数
        uint32_t new_frames_count = 0;          // 新帧数量
        uint32_t repeated_frames_count = 0;     // 重复帧数量
        uint64_t recording_duration_us = 0;     // 录制时长（微秒）
        uint64_t avg_frame_process_time_us = 0; // 平均帧处理时间
        uint32_t last_game_frame_sequence = 0;  // 最后处理的游戏帧序号
    };

private:
    // 录制参数（固定）
    static const uint64_t FRAME_INTERVAL_USEC = 1000000 / 30; // 33333微秒 (30fps)
    
    // 线程控制
    Thread recording_thread;
    std::atomic<bool> recording_active{false};
    std::atomic<bool> thread_started{false};
    
    // 数据源和输出
    ThreadSafeFrameBuffer *frame_buffer = nullptr;
    Ref<SimpleVideoWriter> video_writer;
    
    // 录制配置
    RecordingConfig config;
    
    // 状态跟踪
    uint32_t last_game_frame_sequence = 0;
    ThreadSafeFrameBuffer::FrameData last_valid_frame;
    bool has_valid_frame = false;
    
    // 统计信息
    RecordingStats stats;
    uint64_t recording_start_time = 0;
    uint64_t last_stats_update_time = 0;
    
    // 同步保护
    mutable Mutex stats_mutex;
    
    // 线程主循环
    static void recording_thread_func(void *p_userdata);
    void recording_loop();
    
    // 内部处理方法
    bool process_frame(uint64_t current_recording_time);
    void update_statistics(uint64_t frame_process_start_time);
    uint8_t determine_frame_flags(const ThreadSafeFrameBuffer::FrameData &frame_data);

public:
    IndependentVideoRecorder();
    ~IndependentVideoRecorder();
    
    /**
     * 初始化录制器
     */
    Error initialize(ThreadSafeFrameBuffer *p_frame_buffer, 
                    const String &p_video_path,
                    const RecordingConfig &p_config = RecordingConfig());
    
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
     * 获取录制统计信息
     */
    RecordingStats get_statistics() const;
    
    /**
     * 获取重复帧比例
     */
    float get_repeat_frame_ratio() const;
    
    /**
     * 更新配置
     */
    void update_config(const RecordingConfig &p_config);
    
    /**
     * 重置统计信息
     */
    void reset_statistics();
    
    /**
     * 获取录制进度信息（用于调试）
     */
    String get_debug_info() const;
};

#endif // INDEPENDENT_VIDEO_RECORDER_H 