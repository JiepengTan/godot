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
    
    // 计算声道数
    switch (speaker_mode) {
        case AudioDriver::SPEAKER_MODE_STEREO:
            channels = 2;
            break;
        case AudioDriver::SPEAKER_SURROUND_31:
            channels = 4;
            break;
        case AudioDriver::SPEAKER_SURROUND_51:
            channels = 6;
            break;
        case AudioDriver::SPEAKER_SURROUND_71:
            channels = 8;
            break;
        default:
            channels = 2;
            break;
    }
    
    // 初始化录制驱动
    recording_driver->set_mix_rate(mix_rate);
    recording_driver->set_speaker_mode(speaker_mode);
    recording_driver->set_use_threads(false); // 关键：不使用线程模式
    
    Error err = recording_driver->init();
    if (err != OK) {
        ERR_PRINT("HybridAudioDriver: Failed to initialize recording driver");
        return err;
    }
    
    // 初始化捕获缓冲区（1秒的缓冲区大小）
    captured_buffer.resize(mix_rate * channels);
    
    initialized = true;
    print_line("HybridAudioDriver initialized successfully (non-invasive mode)");
    
    return OK;
}

void HybridAudioDriver::start() {
    if (!initialized) {
        return;
    }
    
    // 启动录制驱动
    recording_driver->start();
    data_ready.clear();
}

void HybridAudioDriver::finish() {
    if (!initialized) {
        return;
    }
    
    recording_enabled = false;
    
    // 停止录制驱动
    if (recording_driver) {
        recording_driver->finish();
    }
    
    initialized = false;
}

void HybridAudioDriver::enable_recording(bool p_enable) {
    recording_enabled = p_enable;
    
    if (p_enable) {
        print_line("HybridAudioDriver: Recording enabled");
    } else {
        print_line("HybridAudioDriver: Recording disabled");
    }
}

void HybridAudioDriver::capture_audio_data(const int32_t *p_buffer, int p_frames, int p_channels) {
    if (!recording_enabled || !initialized) {
        return;
    }
    
    // 线程安全地捕获音频数据
    data_mutex.lock();
    
    // 确保缓冲区大小足够
    int required_size = p_frames * p_channels;
    if (captured_buffer.size() < required_size) {
        captured_buffer.resize(required_size);
    }
    
    // 复制音频数据
    for (int i = 0; i < required_size; i++) {
        captured_buffer.ptrw()[i] = p_buffer[i];
    }
    
    data_ready.set();
    data_mutex.unlock();
}

int HybridAudioDriver::get_captured_audio_data(int32_t *p_output_buffer, int p_requested_frames) {
    if (!recording_enabled || !initialized || !data_ready.is_set()) {
        // 如果没有数据，填充静音
        int total_samples = p_requested_frames * channels;
        for (int i = 0; i < total_samples; i++) {
            p_output_buffer[i] = 0;
        }
        return p_requested_frames;
    }
    
    data_mutex.lock();
    
    int available_frames = captured_buffer.size() / channels;
    int frames_to_copy = MIN(p_requested_frames, available_frames);
    int total_samples = frames_to_copy * channels;
    
    // 复制数据
    for (int i = 0; i < total_samples; i++) {
        p_output_buffer[i] = captured_buffer[i];
    }
    
    // 如果请求的帧数超过可用数据，用静音填充
    if (p_requested_frames > frames_to_copy) {
        int remaining_samples = (p_requested_frames - frames_to_copy) * channels;
        for (int i = total_samples; i < total_samples + remaining_samples; i++) {
            p_output_buffer[i] = 0;
        }
    }
    
    data_ready.clear();
    data_mutex.unlock();
    
    return p_requested_frames;
} 