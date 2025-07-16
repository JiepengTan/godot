/**************************************************************************/
/*  audio_driver_hybrid.cpp                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "audio_driver_hybrid.h"
#include "servers/movie_writer/independent_audio_recorder.h"
#include "core/os/os.h"

HybridAudioDriver::HybridAudioDriver() {
    recording_driver = memnew(AudioDriverDummy);
}

HybridAudioDriver::~HybridAudioDriver() {
    finish();
    if (recording_driver) {
        memdelete(recording_driver);
        recording_driver = nullptr;
    }
}

Error HybridAudioDriver::init(int p_mix_rate, AudioDriver::SpeakerMode p_speaker_mode) {
    mix_rate = p_mix_rate;
    speaker_mode = p_speaker_mode;
    
    // 计算实际音频通道数（与AudioServer保持一致）
    // AudioServer的数据格式是交错立体声，所以总是2个通道
    channels = 2;
    
    // 初始化录制驱动
    recording_driver->set_mix_rate(mix_rate);
    recording_driver->set_speaker_mode(speaker_mode);
    recording_driver->set_use_threads(false); // 关键：不使用线程模式
    
    Error err = recording_driver->init();
    if (err != OK) {
        ERR_PRINT("HybridAudioDriver: Failed to initialize recording driver");
        return err;
    }
    
    // 初始化双缓冲区（缓冲区大小基于采样率和缓冲时长）
    int buffer_frame_count = int(mix_rate * buffer_length_seconds);
    
    // RingBuffer构造函数需要幂次，不是直接大小
    // 计算能容纳buffer_frame_count的最小幂次
    int power = 0;
    while ((1 << power) < buffer_frame_count) {
        power++;
    }
    int actual_buffer_size = 1 << power;  // 实际缓冲区大小
    
    original_buffer_size = actual_buffer_size;  // 保存实际大小
    write_buffer = RingBuffer<AudioFrame>(power);
    read_buffer = RingBuffer<AudioFrame>(power);
    buffer_initialized.set();
    swap_pending.clear();
    
    initialized = true;
    print_line(vformat("HybridAudioDriver initialized: %d Hz, %d channels, %.1f sec buffer (%d frames)", 
               mix_rate, channels, buffer_length_seconds, actual_buffer_size));
    
    return OK;
}

void HybridAudioDriver::start() {
    if (!initialized) {
        return;
    }
    
    // 启动录制驱动
    recording_driver->start();
}

void HybridAudioDriver::finish() {
    if (!initialized) {
        return;
    }
    
    recording_enabled = false;
    buffer_initialized.clear();
    
    // 停止录制驱动
    if (recording_driver) {
        recording_driver->finish();
    }
    
    initialized = false;
}

void HybridAudioDriver::enable_recording(bool p_enable) {
    recording_enabled = p_enable;
    
    if (p_enable) {
        // 清空缓冲区，开始新的录制
        data_mutex.lock();
        if (buffer_initialized.is_set()) {
            // 计算正确的幂次
            int power = 0;
            while ((1 << power) < original_buffer_size) {
                power++;
            }
            write_buffer = RingBuffer<AudioFrame>(power);
            read_buffer = RingBuffer<AudioFrame>(power);
            swap_pending.clear();
        }
        data_mutex.unlock();
        print_line("HybridAudioDriver: Recording enabled");
    } else {
        print_line("HybridAudioDriver: Recording disabled");
    }
}

void HybridAudioDriver::capture_audio_data(const int32_t *p_buffer, int p_frames, int p_channels) {
    if (!recording_enabled || !initialized || !buffer_initialized.is_set()) {
        return;
    }
    
    // 记录捕获时间戳
    last_capture_time = OS::get_singleton()->get_ticks_usec();
    capture_count++;
    
    // AudioServer中的p_channels是"声道对"数量，实际音频通道数是p_channels * 2
    // 对于立体声：p_channels=1, 实际通道数=2 (left, right)
    int actual_channels = p_channels * 2;
    
    // 将int32_t音频数据转换为AudioFrame并写入write_buffer
    for (int i = 0; i < p_frames; i++) {
        AudioFrame frame;
        
        if (actual_channels >= 2) {
            // 立体声或多声道 - 取前两个声道作为AudioFrame
            frame.left = float(p_buffer[i * actual_channels]) / float(1 << 31);
            frame.right = float(p_buffer[i * actual_channels + 1]) / float(1 << 31);
        } else {
            // 单声道情况
            float sample = float(p_buffer[i]) / float(1 << 31);
            frame.left = sample;
            frame.right = sample;
        }
        
        // 如果缓冲区满了，丢弃最旧的数据
        if (write_buffer.space_left() == 0) {
            AudioFrame dummy;
            write_buffer.read(&dummy, 1);
        }
        
        write_buffer.write(&frame, 1);
    }
    
    // 检查是否应该触发缓冲区交换
    int current_data = write_buffer.data_left();
    int threshold = write_buffer.size() / 4;  // 25%满时触发交换
    
    if (current_data >= threshold && !swap_pending.is_set()) {
        swap_pending.set();
    }
    
    // 分发音频数据给所有注册的录制器
    {
        MutexLock lock(recorders_mutex);
        for (IndependentAudioRecorder* recorder : registered_recorders) {
            if (recorder) {
                recorder->on_audio_output(p_buffer, p_frames);
            }
        }
    }
}

int HybridAudioDriver::get_captured_audio_data(int32_t *p_output_buffer, int p_requested_frames) {
    if (!recording_enabled || !initialized || !buffer_initialized.is_set()) {
        // 如果没有数据，填充静音（立体声格式）
        int total_samples = p_requested_frames * 2;  // 2个通道：左+右
        for (int i = 0; i < total_samples; i++) {
            p_output_buffer[i] = 0;
        }
        return p_requested_frames;
    }
    
    // 检查是否需要交换缓冲区（快速操作，最小锁时间）
    if (swap_pending.is_set()) {
        data_mutex.lock();
        
        // 交换缓冲区：write_buffer变成read_buffer
        RingBuffer<AudioFrame> temp = read_buffer;
        read_buffer = write_buffer;
        write_buffer = temp;
        
        // 清空新的write_buffer以便继续写入
        int power = 0;
        while ((1 << power) < original_buffer_size) {
            power++;
        }
        write_buffer = RingBuffer<AudioFrame>(power);
        
        swap_pending.clear();
        data_mutex.unlock();
    }
    
    int available_frames = read_buffer.data_left();
    int frames_to_read = MIN(p_requested_frames, available_frames);
    
    if (frames_to_read > 0) {
        // 读取可用的音频数据
        Vector<AudioFrame> temp_buffer;
        temp_buffer.resize(frames_to_read);
        read_buffer.read(temp_buffer.ptrw(), frames_to_read);
        
        // 转换AudioFrame到int32_t格式 (立体声交错格式)
        for (int i = 0; i < frames_to_read; i++) {
            AudioFrame frame = temp_buffer[i];
            
            // 输出立体声交错格式：[left, right, left, right...]
            p_output_buffer[i * 2] = int32_t(CLAMP(frame.left, -1.0, 1.0) * float(1 << 31));
            p_output_buffer[i * 2 + 1] = int32_t(CLAMP(frame.right, -1.0, 1.0) * float(1 << 31));
        }
    }
    
    // 如果请求的帧数超过可用数据，用静音填充剩余部分
    if (p_requested_frames > frames_to_read) {
        int remaining_samples = (p_requested_frames - frames_to_read) * 2;  // 2个通道：左+右
        int start_index = frames_to_read * 2;
        for (int i = 0; i < remaining_samples; i++) {
            p_output_buffer[start_index + i] = 0;
        }
    }
    
    return p_requested_frames;
}

int HybridAudioDriver::get_available_frames() const {
    if (!buffer_initialized.is_set()) {
        return 0;
    }
    return read_buffer.data_left();
}

bool HybridAudioDriver::has_audio_data() const {
    return buffer_initialized.is_set() && read_buffer.data_left() > 0;
}

void HybridAudioDriver::register_audio_recorder(IndependentAudioRecorder* recorder) {
    if (!recorder) {
        return;
    }
    
    MutexLock lock(recorders_mutex);
    
    // 检查是否已经注册
    for (const IndependentAudioRecorder* existing : registered_recorders) {
        if (existing == recorder) {
            return; // 已经注册了
        }
    }
    
    registered_recorders.push_back(recorder);
    print_line(vformat("HybridAudioDriver: 注册音频录制器，当前总数: %d", registered_recorders.size()));
}

void HybridAudioDriver::unregister_audio_recorder(IndependentAudioRecorder* recorder) {
    if (!recorder) {
        return;
    }
    
    MutexLock lock(recorders_mutex);
    
    for (int i = 0; i < registered_recorders.size(); i++) {
        if (registered_recorders[i] == recorder) {
            registered_recorders.remove_at(i);
            print_line(vformat("HybridAudioDriver: 注销音频录制器，当前总数: %d", registered_recorders.size()));
            return;
        }
    }
}

int HybridAudioDriver::get_registered_recorder_count() const {
    MutexLock lock(recorders_mutex);
    return registered_recorders.size();
} 

