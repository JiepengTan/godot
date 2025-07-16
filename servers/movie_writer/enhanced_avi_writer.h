/**************************************************************************/
/*  enhanced_avi_writer.h                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#ifndef ENHANCED_AVI_WRITER_H
#define ENHANCED_AVI_WRITER_H

#include "core/io/file_access.h"
#include "core/io/image.h"
#include "core/os/os.h"

/**
 * 增强的AVI写入器，支持自定义时间戳和帧标记信息
 * 基于标准AVI格式扩展，添加用于性能分析的元数据
 */
class EnhancedAviWriter {
public:
    // 帧标志位定义
    enum FrameFlags : uint8_t {
        FRAME_FLAG_NEW = 0x01,        // 新帧
        FRAME_FLAG_REPEATED = 0x02,   // 重复帧
        FRAME_FLAG_DROPPED = 0x04,    // 丢帧（预留）
        FRAME_FLAG_RESERVED = 0x08    // 保留标志
    };
    
    // 自定义时间戳chunk结构
    struct TimestampChunk {
        char fourcc[4] = {'0', '0', 't', 's'};  // "00ts"
        uint32_t size = 24;                     // 数据大小
        uint64_t recording_timestamp;           // 录制时间戳（微秒）
        uint64_t game_timestamp;               // 游戏时间戳（微秒）
        uint32_t frame_sequence;               // 游戏帧序号
        uint8_t flags;                         // 标志位
        uint8_t reserved[3];                   // 保留字段
        
        TimestampChunk() : recording_timestamp(0), game_timestamp(0), 
                          frame_sequence(0), flags(0) {
            reserved[0] = reserved[1] = reserved[2] = 0;
        }
    };

private:
    Ref<FileAccess> file;
    String file_path;
    
    // AVI文件结构
    struct AviHeader {
        uint32_t microsec_per_frame;
        uint32_t max_bytes_per_sec;
        uint32_t padding_granularity;
        uint32_t flags;
        uint32_t total_frames;
        uint32_t initial_frames;
        uint32_t streams;
        uint32_t suggested_buffer_size;
        uint32_t width;
        uint32_t height;
        uint32_t reserved[4];
    };
    
    struct StreamHeader {
        char fourcc_type[4];    // 'vids' 或 'auds'
        char fourcc_handler[4]; // 'MJPG' 或 'PCM '
        uint32_t flags;
        uint16_t priority;
        uint16_t language;
        uint32_t initial_frames;
        uint32_t scale;
        uint32_t rate;          // fps = rate/scale
        uint32_t start;
        uint32_t length;        // frames count
        uint32_t suggested_buffer_size;
        uint32_t quality;
        uint32_t sample_size;
        uint16_t left, top, right, bottom;
    };
    
    // 录制参数
    uint32_t video_width = 1920;
    uint32_t video_height = 1080;
    uint32_t video_fps = 30;
    uint32_t audio_sample_rate = 48000;
    uint32_t audio_channels = 2;
    float jpeg_quality = 0.85f;
    
    // 文件偏移量跟踪
    uint64_t movi_list_pos = 0;
    uint64_t idx1_pos = 0;
    uint32_t video_frame_count = 0;
    uint32_t audio_chunk_count = 0;
    
    // 索引条目
    struct IndexEntry {
        char fourcc[4];
        uint32_t flags;
        uint32_t chunk_offset;
        uint32_t chunk_size;
    };
    
    Vector<IndexEntry> index_entries;
    
    // 内部方法
    void write_fourcc(const char *fourcc);
    void write_uint32(uint32_t value);
    void write_uint16(uint16_t value);
    void write_uint8(uint8_t value);
    void write_bytes(const uint8_t *data, size_t size);
    
    void write_avi_header();
    void write_stream_headers();
    void write_video_stream_header();
    void write_audio_stream_header();
    void finalize_headers();
    
public:
    EnhancedAviWriter();
    ~EnhancedAviWriter();
    
    /**
     * 初始化AVI文件
     */
    Error open(const String &p_path, uint32_t p_width, uint32_t p_height, 
               uint32_t p_fps, uint32_t p_audio_rate, uint32_t p_channels, 
               float p_quality = 0.85f);
    
    /**
     * 写入带时间戳的视频帧
     */
    Error write_video_frame(const Ref<Image> &p_image, uint64_t recording_time, 
                           uint64_t game_time, uint32_t sequence, uint8_t flags);
    
    /**
     * 写入音频数据块
     */
    Error write_audio_chunk(const int32_t *p_audio_data, int p_frame_count, 
                           uint64_t recording_time);
    
    /**
     * 完成文件写入
     */
    void close();
    
    /**
     * 获取统计信息
     */
    uint32_t get_video_frame_count() const { return video_frame_count; }
    uint32_t get_audio_chunk_count() const { return audio_chunk_count; }
    
    /**
     * 设置JPEG质量
     */
    void set_jpeg_quality(float p_quality) { jpeg_quality = CLAMP(p_quality, 0.1f, 1.0f); }
};

#endif // ENHANCED_AVI_WRITER_H 