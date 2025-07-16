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
    
    // 线程安全的数据传输
    Mutex data_mutex;
    Vector<int32_t> captured_buffer;
    SafeFlag data_ready;

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
    
    // AudioCaptureInterface 实现 - 从AudioServer接收音频数据
    virtual void capture_audio_data(const int32_t *p_buffer, int p_frames, int p_channels) override;
    
    // 为MovieWriter提供音频数据
    int get_captured_audio_data(int32_t *p_output_buffer, int p_requested_frames);
    
    int get_mix_rate() const { return mix_rate; }
    AudioDriver::SpeakerMode get_speaker_mode() const { return speaker_mode; }
    int get_channels() const { return channels; }
};

#endif // AUDIO_DRIVER_HYBRID_H 