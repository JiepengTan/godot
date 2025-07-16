/**************************************************************************/
/*  thread_safe_frame_buffer.h                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#ifndef THREAD_SAFE_FRAME_BUFFER_H
#define THREAD_SAFE_FRAME_BUFFER_H

#include "core/io/image.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include <atomic>

/**
 * 线程安全的双缓冲帧数据管理器
 * 实现游戏主线程与录制线程之间的安全数据交换
 */
class ThreadSafeFrameBuffer {
public:
    struct FrameData {
        Ref<Image> image;           // 图像数据
        uint64_t game_timestamp;    // 游戏时间戳（微秒）
        uint32_t frame_sequence;    // 游戏帧序号
        bool is_new_frame = false;  // 是否为新帧
        
        FrameData() : game_timestamp(0), frame_sequence(0), is_new_frame(false) {}
        
        FrameData(const FrameData &other) 
            : image(other.image)
            , game_timestamp(other.game_timestamp)
            , frame_sequence(other.frame_sequence)
            , is_new_frame(other.is_new_frame) {}
        
        FrameData &operator=(const FrameData &other) {
            if (this != &other) {
                image = other.image;
                game_timestamp = other.game_timestamp;
                frame_sequence = other.frame_sequence;
                is_new_frame = other.is_new_frame;
            }
            return *this;
        }
    };

private:
    // 双缓冲区
    FrameData buffer_a;
    FrameData buffer_b;
    bool writing_to_a = true;           // 游戏线程写入标志
    
    // 同步控制
    mutable Mutex buffer_mutex;         // 缓冲区切换保护
    std::atomic<bool> has_new_data{false};    // 新数据标志
    std::atomic<uint32_t> last_sequence{0};   // 最后处理的序列号
    
    // 统计信息
    std::atomic<uint64_t> total_updates{0};
    std::atomic<uint64_t> buffer_switches{0};

public:
    ThreadSafeFrameBuffer();
    ~ThreadSafeFrameBuffer();
    
    /**
     * 游戏线程调用：更新画面
     * @param new_frame 新的图像帧
     * @param timestamp 游戏时间戳
     * @param sequence 游戏帧序号
     */
    void update_frame(const Ref<Image> &new_frame, uint64_t timestamp, uint32_t sequence);
    
    /**
     * 录制线程调用：获取稳定画面
     * @return 当前稳定的帧数据
     */
    FrameData get_current_frame() const;
    
    /**
     * 检查是否有新数据
     */
    bool has_new_frame() const { return has_new_data.load(); }
    
    /**
     * 获取最后处理的序列号
     */
    uint32_t get_last_sequence() const { return last_sequence.load(); }
    
    /**
     * 获取统计信息
     */
    uint64_t get_total_updates() const { return total_updates.load(); }
    uint64_t get_buffer_switches() const { return buffer_switches.load(); }
    
    /**
     * 重置缓冲区
     */
    void reset();
};

#endif // THREAD_SAFE_FRAME_BUFFER_H 