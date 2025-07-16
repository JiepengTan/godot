/**************************************************************************/
/*  audio_driver_hybrid.h                                                */
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

#ifndef AUDIO_DRIVER_HYBRID_H
#define AUDIO_DRIVER_HYBRID_H

#include "servers/audio_server.h"
#include "servers/audio/audio_driver_dummy.h"
#include "core/os/mutex.h"
#include "core/templates/safe_refcount.h"
#include "core/templates/ring_buffer.h"
#include "core/math/audio_frame.h"

// 音频数据捕获接口
class AudioCaptureInterface {
public:
    virtual ~AudioCaptureInterface() {}
    virtual void capture_audio_data(const int32_t *p_buffer, int p_frames, int p_channels) = 0;
};

// 重新设计的HybridAudioDriver - 不替换单例，而是作为音频捕获器
class HybridAudioDriver : public AudioCaptureInterface {
private:
    AudioDriverDummy *recording_driver = nullptr;
    bool recording_enabled = false;
    bool initialized = false;
    
    // 音频参数
    int mix_rate = 44100;
    AudioDriver::SpeakerMode speaker_mode = AudioDriver::SPEAKER_MODE_STEREO;
    int channels = 2;
    
    // 双缓冲区架构 - 避免读写锁竞争
    Mutex data_mutex;
    RingBuffer<AudioFrame> write_buffer;  // 音频线程写入
    RingBuffer<AudioFrame> read_buffer;   // 主线程读取
    SafeFlag buffer_initialized;
    SafeFlag swap_pending;               // 缓冲区交换标志
    float buffer_length_seconds = 0.2f; // 200ms缓冲区，提供更多容错空间
    int original_buffer_size = 0;        // 保存原始缓冲区大小
    
    // 同步状态监控
    uint64_t last_capture_time = 0;
    uint64_t capture_count = 0;

public:
    HybridAudioDriver();
    ~HybridAudioDriver();
    
    // 初始化和控制
    Error init(int p_mix_rate, AudioDriver::SpeakerMode p_speaker_mode);
    void start();
    void finish();
    
    // 录制控制
    void enable_recording(bool p_enable);
    bool is_recording_enabled() const { return recording_enabled; }
    
    // 获取录制驱动（供 MovieWriter 使用）
    AudioDriverDummy *get_recording_driver() { return recording_driver; }
    
    // 获取音频参数
    int get_channels() const { return channels; }
    int get_mix_rate() const { return mix_rate; }
    
    // AudioCaptureInterface 实现 - 从AudioServer接收音频数据
    virtual void capture_audio_data(const int32_t *p_buffer, int p_frames, int p_channels) override;
    
    // 为MovieWriter提供音频数据
    int get_captured_audio_data(int32_t *p_output_buffer, int p_requested_frames);
    
    // 缓冲区状态
    int get_available_frames() const;
    bool has_audio_data() const;
};

#endif // AUDIO_DRIVER_HYBRID_H 