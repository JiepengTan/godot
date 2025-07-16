/**************************************************************************/
/*  thread_safe_frame_buffer.cpp                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "thread_safe_frame_buffer.h"
#include "core/os/os.h"

ThreadSafeFrameBuffer::ThreadSafeFrameBuffer() {
    // 初始化原子变量
    has_new_data.store(false);
    last_sequence.store(0);
    total_updates.store(0);
    buffer_switches.store(0);
}

ThreadSafeFrameBuffer::~ThreadSafeFrameBuffer() {
    // 清理资源
    reset();
}

void ThreadSafeFrameBuffer::update_frame(const Ref<Image> &new_frame, uint64_t timestamp, uint32_t sequence) {
    // 参数验证
    if (new_frame.is_null()) {
        return;
    }
    
    total_updates.fetch_add(1);
    
    // 快速路径：获取写入缓冲区的引用，无需锁定
    FrameData *write_buffer = writing_to_a ? &buffer_a : &buffer_b;
    
    // 更新写入缓冲区（非临界区）
    write_buffer->image = new_frame;
    write_buffer->game_timestamp = timestamp;
    write_buffer->frame_sequence = sequence;
    write_buffer->is_new_frame = true;
    
    // 临界区：切换缓冲区指针
    {
        MutexLock lock(buffer_mutex);
        writing_to_a = !writing_to_a;
        buffer_switches.fetch_add(1);
    }
    
    // 更新状态标志
    has_new_data.store(true);
    last_sequence.store(sequence);
}

ThreadSafeFrameBuffer::FrameData ThreadSafeFrameBuffer::get_current_frame() const {
    MutexLock lock(buffer_mutex);
    
    // 获取稳定的读取缓冲区（非写入缓冲区）
    const FrameData *read_buffer = writing_to_a ? &buffer_b : &buffer_a;
    
    // 复制数据（在锁内进行，确保一致性）
    FrameData result = *read_buffer;
    
    return result;
}

void ThreadSafeFrameBuffer::reset() {
    MutexLock lock(buffer_mutex);
    
    // 清空缓冲区
    buffer_a = FrameData();
    buffer_b = FrameData();
    writing_to_a = true;
    
    // 重置状态
    has_new_data.store(false);
    last_sequence.store(0);
    total_updates.store(0);
    buffer_switches.store(0);
} 