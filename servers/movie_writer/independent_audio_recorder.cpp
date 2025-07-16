/**************************************************************************/
/*  independent_audio_recorder.cpp                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "independent_audio_recorder.h"
#include "servers/audio/audio_driver_hybrid.h"
#include "core/string/print_string.h"
#include "core/os/os.h"

IndependentAudioRecorder::IndependentAudioRecorder() {
    recording_active.store(false);
    thread_started.store(false);
    buffer_read_pos.store(0);
    buffer_write_pos.store(0);
    buffer_size = 0;
    recording_start_time = 0;
}

IndependentAudioRecorder::~IndependentAudioRecorder() {
    if (is_recording()) {
        stop_recording();
    }
}

Error IndependentAudioRecorder::initialize(HybridAudioDriver *p_audio_driver, 
                                          EnhancedAviWriter *p_avi_writer,
                                          const AudioConfig &p_config) {
    if (!p_audio_driver || !p_avi_writer) {
        ERR_PRINT("IndependentAudioRecorder: 无效的audio_driver或avi_writer");
        return ERR_INVALID_PARAMETER;
    }
    
    audio_driver = p_audio_driver;
    avi_writer = p_avi_writer;
    config = p_config;
    
    // 计算缓冲区大小
    buffer_size = config.sample_rate * config.channels * config.buffer_size_seconds;
    
    // 初始化环形缓冲区
    audio_ring_buffer.clear();
    audio_ring_buffer.resize(buffer_size);
    
    // 初始化临时缓冲区
    temp_audio_buffer.resize(config.chunk_size * config.channels);
    chunk_buffer.resize(config.chunk_size * config.channels);
    
    // 重置统计信息
    reset_statistics();
    
    print_line("IndependentAudioRecorder 初始化完成");
    print_line(String("采样率: ") + String::num_int64(config.sample_rate) + "Hz");
    print_line(String("声道数: ") + String::num_int64(config.channels));
    print_line(String("块大小: ") + String::num_int64(config.chunk_size) + " 样本");
    print_line(String("缓冲区大小: ") + String::num_int64(buffer_size) + " 样本 (" + 
              String::num_real(config.buffer_size_seconds) + " 秒)");
    
    return OK;
}

Error IndependentAudioRecorder::start_recording() {
    if (recording_active.load()) {
        ERR_PRINT("IndependentAudioRecorder: 录制已经在进行中");
        return ERR_ALREADY_IN_USE;
    }
    
    if (!audio_driver || !avi_writer) {
        ERR_PRINT("IndependentAudioRecorder: 尚未初始化");
        return ERR_UNCONFIGURED;
    }
    
    // 清空缓冲区
    audio_ring_buffer.clear();
    buffer_read_pos.store(0);
    buffer_write_pos.store(0);
    
    // 设置录制状态
    recording_active.store(true);
    recording_start_time = OS::get_singleton()->get_ticks_usec();
    
    // 启动录制线程
    recording_thread.start(recording_thread_func, this);
    thread_started.store(true);
    
    print_line("IndependentAudioRecorder: 开始录制");
    
    return OK;
}

void IndependentAudioRecorder::stop_recording() {
    if (!recording_active.load()) {
        return;
    }
    
    print_line("IndependentAudioRecorder: 停止录制...");
    
    // 停止录制循环
    recording_active.store(false);
    
    // 等待线程结束
    if (thread_started.load()) {
        recording_thread.wait_to_finish();
        thread_started.store(false);
    }
    
    // 输出最终统计信息
    AudioStats final_stats = get_statistics();
    print_line(String("音频录制完成 - 总块数: ") + String::num_int64(final_stats.total_chunks_recorded));
    print_line(String("总样本数: ") + String::num_int64(final_stats.total_samples_recorded));
    print_line(String("录制时长: ") + String::num_real(final_stats.recording_duration_us / 1000000.0) + "秒");
    print_line(String("缓冲区溢出: ") + String::num_int64(final_stats.buffer_overruns));
    print_line(String("缓冲区下溢: ") + String::num_int64(final_stats.buffer_underruns));
}

void IndependentAudioRecorder::recording_thread_func(void *p_userdata) {
    IndependentAudioRecorder *recorder = static_cast<IndependentAudioRecorder *>(p_userdata);
    recorder->recording_loop();
}

void IndependentAudioRecorder::recording_loop() {
    print_line("IndependentAudioRecorder: 音频录制线程开始运行");
    
    uint64_t next_chunk_time = recording_start_time;
    uint64_t chunk_count = 0;
    
    while (recording_active.load()) {
        uint64_t current_time = OS::get_singleton()->get_ticks_usec();
        
        if (current_time >= next_chunk_time) {
            uint64_t chunk_process_start = OS::get_singleton()->get_ticks_usec();
            
            // 处理音频块
            bool chunk_processed = process_audio_chunk(next_chunk_time - recording_start_time);
            
            if (chunk_processed) {
                chunk_count++;
                update_statistics(chunk_process_start, config.chunk_size);
                
                // 每1000块输出一次调试信息
                if (chunk_count % 1000 == 0) {
                    AudioStats current_stats = get_statistics();
                    print_line(String("音频录制进度: ") + String::num_int64(chunk_count) + " 块, " +
                              String("缓冲区使用率: ") + String::num_int64(current_stats.current_buffer_level) + "%");
                }
            }
            
            // 计算下一块时间
            next_chunk_time += CHUNK_INTERVAL_USEC;
        }
        
        // 精确睡眠控制
        current_time = OS::get_singleton()->get_ticks_usec();
        if (next_chunk_time > current_time) {
            uint64_t sleep_time = next_chunk_time - current_time;
            if (sleep_time > 1000) { // 如果需要等待超过1ms
                OS::get_singleton()->delay_usec(sleep_time - 500); // 留500微秒的缓冲
            }
        }
    }
    
    print_line("IndependentAudioRecorder: 音频录制线程结束");
}

bool IndependentAudioRecorder::process_audio_chunk(uint64_t current_recording_time) {
    // 从环形缓冲区读取音频数据
    if (!read_audio_chunk(chunk_buffer, config.chunk_size * config.channels)) {
        handle_buffer_underrun();
        return false;
    }
    
    // 写入AVI文件
    Error write_result = avi_writer->write_audio_chunk(
        chunk_buffer.ptr(),
        config.chunk_size,
        recording_start_time + current_recording_time
    );
    
    if (write_result != OK) {
        ERR_PRINT("IndependentAudioRecorder: 写入音频块失败");
        return false;
    }
    
    // 更新统计信息
    {
        MutexLock lock(stats_mutex);
        stats.total_chunks_recorded++;
        stats.total_samples_recorded += config.chunk_size;
    }
    
    return true;
}

bool IndependentAudioRecorder::read_audio_chunk(Vector<int32_t> &output_buffer, uint32_t requested_samples) {
    if (get_available_samples() < requested_samples) {
        return false; // 数据不足
    }
    
    MutexLock lock(buffer_mutex);
    
    uint32_t read_pos = buffer_read_pos.load();
    
    for (uint32_t i = 0; i < requested_samples; i++) {
        output_buffer.write[i] = audio_ring_buffer[read_pos];
        read_pos = (read_pos + 1) % buffer_size;
    }
    
    buffer_read_pos.store(read_pos);
    
    return true;
}

void IndependentAudioRecorder::on_audio_output(const int32_t *p_buffer, int p_frame_count) {
    if (!recording_active.load() || !p_buffer || p_frame_count <= 0) {
        return;
    }
    
    MutexLock lock(buffer_mutex);
    
    uint32_t write_pos = buffer_write_pos.load();
    uint32_t samples_to_write = p_frame_count * config.channels;
    
    // 检查缓冲区空间
    uint32_t available_space = buffer_size - get_available_samples();
    if (samples_to_write > available_space) {
        handle_buffer_overrun();
        return;
    }
    
    // 写入数据
    for (uint32_t i = 0; i < samples_to_write; i++) {
        audio_ring_buffer[write_pos] = p_buffer[i];
        write_pos = (write_pos + 1) % buffer_size;
    }
    
    buffer_write_pos.store(write_pos);
}

void IndependentAudioRecorder::update_statistics(uint64_t chunk_process_start_time, uint32_t samples_processed) {
    uint64_t current_time = OS::get_singleton()->get_ticks_usec();
    uint64_t process_time = current_time - chunk_process_start_time;
    
    MutexLock lock(stats_mutex);
    
    // 更新录制时长
    stats.recording_duration_us = current_time - recording_start_time;
    
    // 更新平均处理时间（使用移动平均）
    if (stats.avg_chunk_process_time_us == 0) {
        stats.avg_chunk_process_time_us = process_time;
    } else {
        // 使用90%旧值 + 10%新值的移动平均
        stats.avg_chunk_process_time_us = (stats.avg_chunk_process_time_us * 9 + process_time) / 10;
    }
    
    // 更新缓冲区使用率
    update_buffer_level();
}

void IndependentAudioRecorder::update_buffer_level() {
    uint32_t available = get_available_samples();
    stats.current_buffer_level = (available * 100) / buffer_size;
}

void IndependentAudioRecorder::handle_buffer_underrun() {
    MutexLock lock(stats_mutex);
    stats.buffer_underruns++;
    
    // 填充静音数据
    chunk_buffer.fill(0);
}

void IndependentAudioRecorder::handle_buffer_overrun() {
    MutexLock lock(stats_mutex);
    stats.buffer_overruns++;
    
    // 跳过最老的数据，腾出空间
    uint32_t samples_to_skip = config.chunk_size * config.channels;
    uint32_t read_pos = buffer_read_pos.load();
    read_pos = (read_pos + samples_to_skip) % buffer_size;
    buffer_read_pos.store(read_pos);
}

uint32_t IndependentAudioRecorder::get_available_samples() const {
    uint32_t write_pos = buffer_write_pos.load();
    uint32_t read_pos = buffer_read_pos.load();
    
    if (write_pos >= read_pos) {
        return write_pos - read_pos;
    } else {
        return buffer_size - read_pos + write_pos;
    }
}

bool IndependentAudioRecorder::has_audio_data() const {
    return get_available_samples() >= (config.chunk_size * config.channels);
}

float IndependentAudioRecorder::get_buffer_usage_ratio() const {
    if (buffer_size == 0) {
        return 0.0f;
    }
    return (float)get_available_samples() / (float)buffer_size;
}

IndependentAudioRecorder::AudioStats IndependentAudioRecorder::get_statistics() const {
    MutexLock lock(stats_mutex);
    return stats;
}

void IndependentAudioRecorder::update_config(const AudioConfig &p_config) {
    if (is_recording()) {
        ERR_PRINT("IndependentAudioRecorder: 无法在录制时更新配置");
        return;
    }
    
    config = p_config;
    
    // 重新计算缓冲区大小
    buffer_size = config.sample_rate * config.channels * config.buffer_size_seconds;
    audio_ring_buffer.resize(buffer_size);
    
    temp_audio_buffer.resize(config.chunk_size * config.channels);
    chunk_buffer.resize(config.chunk_size * config.channels);
}

void IndependentAudioRecorder::reset_statistics() {
    MutexLock lock(stats_mutex);
    
    stats = AudioStats();
    buffer_read_pos.store(0);
    buffer_write_pos.store(0);
    recording_start_time = 0;
}

String IndependentAudioRecorder::get_debug_info() const {
    AudioStats current_stats = get_statistics();
    
    String info;
    info += "=== IndependentAudioRecorder Debug Info ===\n";
    info += String("录制状态: ") + (is_recording() ? "运行中" : "停止") + "\n";
    info += String("线程状态: ") + (is_thread_running() ? "运行中" : "停止") + "\n";
    info += String("总音频块数: ") + String::num_int64(current_stats.total_chunks_recorded) + "\n";
    info += String("总样本数: ") + String::num_int64(current_stats.total_samples_recorded) + "\n";
    info += String("录制时长: ") + String::num_real(current_stats.recording_duration_us / 1000000.0) + "秒\n";
    info += String("缓冲区使用率: ") + String::num_int64(current_stats.current_buffer_level) + "%\n";
    info += String("可用样本数: ") + String::num_int64(get_available_samples()) + "\n";
    info += String("缓冲区溢出: ") + String::num_int64(current_stats.buffer_overruns) + "\n";
    info += String("缓冲区下溢: ") + String::num_int64(current_stats.buffer_underruns) + "\n";
    info += String("平均块处理时间: ") + String::num_int64(current_stats.avg_chunk_process_time_us) + "微秒\n";
    info += String("配置采样率: ") + String::num_int64(config.sample_rate) + "Hz\n";
    info += String("配置声道数: ") + String::num_int64(config.channels) + "\n";
    info += String("配置块大小: ") + String::num_int64(config.chunk_size) + " 样本\n";
    info += "==========================================";
    
    return info;
} 