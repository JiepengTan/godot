/**************************************************************************/
/*  independent_video_recorder.cpp                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "independent_video_recorder.h"
#include "core/string/print_string.h"
#include "core/os/os.h"

IndependentVideoRecorder::IndependentVideoRecorder() {
    recording_active.store(false);
    thread_started.store(false);
    last_game_frame_sequence = 0;
    has_valid_frame = false;
    recording_start_time = 0;
    last_stats_update_time = 0;
}

IndependentVideoRecorder::~IndependentVideoRecorder() {
    if (is_recording()) {
        stop_recording();
    }
}

Error IndependentVideoRecorder::initialize(ThreadSafeFrameBuffer *p_frame_buffer, 
                                          EnhancedAviWriter *p_avi_writer,
                                          const RecordingConfig &p_config) {
    if (!p_frame_buffer || !p_avi_writer) {
        ERR_PRINT("IndependentVideoRecorder: 无效的frame_buffer或avi_writer");
        return ERR_INVALID_PARAMETER;
    }
    
    frame_buffer = p_frame_buffer;
    avi_writer = p_avi_writer;
    config = p_config;
    
    // 重置统计信息
    reset_statistics();
    
    print_line("IndependentVideoRecorder 初始化完成");
    print_line(String("目标FPS: ") + String::num_int64(config.target_fps));
    print_line(String("视频分辨率: ") + String::num_int64(config.video_width) + "x" + String::num_int64(config.video_height));
    print_line(String("JPEG质量: ") + String::num_real(config.jpeg_quality));
    
    return OK;
}

Error IndependentVideoRecorder::start_recording() {
    if (recording_active.load()) {
        ERR_PRINT("IndependentVideoRecorder: 录制已经在进行中");
        return ERR_ALREADY_IN_USE;
    }
    
    if (!frame_buffer || !avi_writer) {
        ERR_PRINT("IndependentVideoRecorder: 尚未初始化");
        return ERR_UNCONFIGURED;
    }
    
    // 设置录制状态
    recording_active.store(true);
    recording_start_time = OS::get_singleton()->get_ticks_usec();
    
    // 启动录制线程
    recording_thread.start(recording_thread_func, this);
    thread_started.store(true);
    
    print_line("IndependentVideoRecorder: 开始录制");
    
    return OK;
}

void IndependentVideoRecorder::stop_recording() {
    if (!recording_active.load()) {
        return;
    }
    
    print_line("IndependentVideoRecorder: 停止录制...");
    
    // 停止录制循环
    recording_active.store(false);
    
    // 等待线程结束
    if (thread_started.load()) {
        recording_thread.wait_to_finish();
        thread_started.store(false);
    }
    
    // 输出最终统计信息
    RecordingStats final_stats = get_statistics();
    print_line(String("录制完成 - 总帧数: ") + String::num_int64(final_stats.total_recorded_frames));
    print_line(String("新帧数: ") + String::num_int64(final_stats.new_frames_count));
    print_line(String("重复帧数: ") + String::num_int64(final_stats.repeated_frames_count));
    print_line(String("重复帧比例: ") + String::num_real(get_repeat_frame_ratio() * 100.0f) + "%");
    print_line(String("录制时长: ") + String::num_real(final_stats.recording_duration_us / 1000000.0) + "秒");
}

void IndependentVideoRecorder::recording_thread_func(void *p_userdata) {
    IndependentVideoRecorder *recorder = static_cast<IndependentVideoRecorder *>(p_userdata);
    recorder->recording_loop();
}

void IndependentVideoRecorder::recording_loop() {
    print_line("IndependentVideoRecorder: 录制线程开始运行");
    
    uint64_t next_record_time = recording_start_time;
    uint64_t frame_count = 0;
    
    while (recording_active.load()) {
        uint64_t current_time = OS::get_singleton()->get_ticks_usec();
        
        if (current_time >= next_record_time) {
            uint64_t frame_process_start = OS::get_singleton()->get_ticks_usec();
            
            // 处理帧
            bool frame_processed = process_frame(next_record_time - recording_start_time);
            
            if (frame_processed) {
                frame_count++;
                update_statistics(frame_process_start);
                
                // 每100帧输出一次调试信息
                if (frame_count % 100 == 0) {
                    RecordingStats current_stats = get_statistics();
                    print_line(String("录制进度: ") + String::num_int64(frame_count) + " 帧, " +
                              String("重复帧比例: ") + String::num_real(get_repeat_frame_ratio() * 100.0f) + "%");
                }
            }
            
            // 计算下一帧时间
            next_record_time += FRAME_INTERVAL_USEC;
        }
        
        // 精确睡眠控制
        current_time = OS::get_singleton()->get_ticks_usec();
        if (next_record_time > current_time) {
            uint64_t sleep_time = next_record_time - current_time;
            if (sleep_time > 1000) { // 如果需要等待超过1ms
                OS::get_singleton()->delay_usec(sleep_time - 500); // 留500微秒的缓冲
            }
        }
    }
    
    print_line("IndependentVideoRecorder: 录制线程结束");
}

bool IndependentVideoRecorder::process_frame(uint64_t current_recording_time) {
    // 获取当前帧数据
    ThreadSafeFrameBuffer::FrameData frame_data = frame_buffer->get_current_frame();
    
    bool is_repeated_frame = false;
    ThreadSafeFrameBuffer::FrameData frame_to_write;
    
    if (frame_data.frame_sequence == last_game_frame_sequence || frame_data.image.is_null()) {
        // 游戏没有新帧或无效帧，使用重复帧
        if (!has_valid_frame) {
            // 还没有有效帧，跳过
            return false;
        }
        
        is_repeated_frame = true;
        frame_to_write = last_valid_frame;
        
        // 更新重复帧的时间戳但保持游戏时间戳不变
        frame_to_write.is_new_frame = false;
        
        MutexLock lock(stats_mutex);
        stats.repeated_frames_count++;
    } else {
        // 有新帧，更新记录
        is_repeated_frame = false;
        frame_to_write = frame_data;
        frame_to_write.is_new_frame = true;
        
        last_valid_frame = frame_data;
        last_game_frame_sequence = frame_data.frame_sequence;
        has_valid_frame = true;
        
        MutexLock lock(stats_mutex);
        stats.new_frames_count++;
        stats.last_game_frame_sequence = frame_data.frame_sequence;
    }
    
    // 确定帧标志
    uint8_t flags = determine_frame_flags(frame_to_write);
    
    // 写入AVI文件
    Error write_result = avi_writer->write_video_frame(
        frame_to_write.image,
        recording_start_time + current_recording_time, // 录制时间戳
        frame_to_write.game_timestamp,                 // 游戏时间戳
        frame_to_write.frame_sequence,                 // 游戏帧序号
        flags                                          // 帧标志
    );
    
    if (write_result != OK) {
        ERR_PRINT("IndependentVideoRecorder: 写入视频帧失败");
        return false;
    }
    
    // 更新统计信息
    {
        MutexLock lock(stats_mutex);
        stats.total_recorded_frames++;
    }
    
    return true;
}

void IndependentVideoRecorder::update_statistics(uint64_t frame_process_start_time) {
    uint64_t current_time = OS::get_singleton()->get_ticks_usec();
    uint64_t process_time = current_time - frame_process_start_time;
    
    MutexLock lock(stats_mutex);
    
    // 更新录制时长
    stats.recording_duration_us = current_time - recording_start_time;
    
    // 更新平均处理时间（使用移动平均）
    if (stats.avg_frame_process_time_us == 0) {
        stats.avg_frame_process_time_us = process_time;
    } else {
        // 使用90%旧值 + 10%新值的移动平均
        stats.avg_frame_process_time_us = (stats.avg_frame_process_time_us * 9 + process_time) / 10;
    }
}

uint8_t IndependentVideoRecorder::determine_frame_flags(const ThreadSafeFrameBuffer::FrameData &frame_data) {
    uint8_t flags = 0;
    
    if (!config.enable_repeat_frame_marking) {
        return flags;
    }
    
    if (frame_data.is_new_frame) {
        flags |= EnhancedAviWriter::FRAME_FLAG_NEW;
    } else {
        flags |= EnhancedAviWriter::FRAME_FLAG_REPEATED;
    }
    
    return flags;
}

IndependentVideoRecorder::RecordingStats IndependentVideoRecorder::get_statistics() const {
    MutexLock lock(stats_mutex);
    return stats;
}

float IndependentVideoRecorder::get_repeat_frame_ratio() const {
    MutexLock lock(stats_mutex);
    
    if (stats.total_recorded_frames == 0) {
        return 0.0f;
    }
    
    return (float)stats.repeated_frames_count / (float)stats.total_recorded_frames;
}

void IndependentVideoRecorder::update_config(const RecordingConfig &p_config) {
    config = p_config;
    
    if (avi_writer) {
        avi_writer->set_jpeg_quality(config.jpeg_quality);
    }
}

void IndependentVideoRecorder::reset_statistics() {
    MutexLock lock(stats_mutex);
    
    stats = RecordingStats();
    last_game_frame_sequence = 0;
    has_valid_frame = false;
    recording_start_time = 0;
    last_stats_update_time = 0;
}

String IndependentVideoRecorder::get_debug_info() const {
    RecordingStats current_stats = get_statistics();
    
    String info;
    info += "=== IndependentVideoRecorder Debug Info ===\n";
    info += String("录制状态: ") + (is_recording() ? "运行中" : "停止") + "\n";
    info += String("线程状态: ") + (is_thread_running() ? "运行中" : "停止") + "\n";
    info += String("总录制帧数: ") + String::num_int64(current_stats.total_recorded_frames) + "\n";
    info += String("新帧数: ") + String::num_int64(current_stats.new_frames_count) + "\n";
    info += String("重复帧数: ") + String::num_int64(current_stats.repeated_frames_count) + "\n";
    info += String("重复帧比例: ") + String::num_real(get_repeat_frame_ratio() * 100.0f) + "%\n";
    info += String("录制时长: ") + String::num_real(current_stats.recording_duration_us / 1000000.0) + "秒\n";
    info += String("平均帧处理时间: ") + String::num_int64(current_stats.avg_frame_process_time_us) + "微秒\n";
    info += String("最后游戏帧序号: ") + String::num_int64(current_stats.last_game_frame_sequence) + "\n";
    info += String("配置FPS: ") + String::num_int64(config.target_fps) + "\n";
    info += String("配置分辨率: ") + String::num_int64(config.video_width) + "x" + String::num_int64(config.video_height) + "\n";
    info += "==========================================";
    
    return info;
} 