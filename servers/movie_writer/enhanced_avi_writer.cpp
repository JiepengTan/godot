/**************************************************************************/
/*  enhanced_avi_writer.cpp                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "enhanced_avi_writer.h"
#include "core/io/image.h"
#include "core/string/print_string.h"

EnhancedAviWriter::EnhancedAviWriter() {
    video_frame_count = 0;
    audio_chunk_count = 0;
    movi_list_pos = 0;
    idx1_pos = 0;
}

EnhancedAviWriter::~EnhancedAviWriter() {
    if (file.is_valid() && file->is_open()) {
        close();
    }
}

void EnhancedAviWriter::write_fourcc(const char *fourcc) {
    file->store_buffer((const uint8_t *)fourcc, 4);
}

void EnhancedAviWriter::write_uint32(uint32_t value) {
    file->store_32(value);
}

void EnhancedAviWriter::write_uint16(uint16_t value) {
    file->store_16(value);
}

void EnhancedAviWriter::write_uint8(uint8_t value) {
    file->store_8(value);
}

void EnhancedAviWriter::write_bytes(const uint8_t *data, size_t size) {
    file->store_buffer(data, size);
}

Error EnhancedAviWriter::open(const String &p_path, uint32_t p_width, uint32_t p_height, 
                             uint32_t p_fps, uint32_t p_audio_rate, uint32_t p_channels, 
                             float p_quality) {
    file_path = p_path;
    video_width = p_width;
    video_height = p_height;
    video_fps = p_fps;
    audio_sample_rate = p_audio_rate;
    audio_channels = p_channels;
    jpeg_quality = p_quality;
    
    // 打开文件
    file = FileAccess::open(p_path, FileAccess::WRITE);
    if (file.is_null()) {
        ERR_PRINT("无法创建AVI文件: " + p_path);
        return ERR_FILE_CANT_OPEN;
    }
    
    // 写入AVI文件头结构
    write_avi_header();
    
    return OK;
}

void EnhancedAviWriter::write_avi_header() {
    // RIFF头
    write_fourcc("RIFF");
    write_uint32(0); // 文件大小，稍后更新
    write_fourcc("AVI ");
    
    // LIST hdrl
    write_fourcc("LIST");
    write_uint32(0); // hdrl大小，稍后更新
    write_fourcc("hdrl");
    
    // 主AVI头
    write_fourcc("avih");
    write_uint32(56); // avih chunk大小
    
    AviHeader avi_header = {};
    avi_header.microsec_per_frame = 1000000 / video_fps;
    avi_header.max_bytes_per_sec = video_width * video_height * 3 * video_fps; // 估算
    avi_header.padding_granularity = 0;
    avi_header.flags = 0x10; // AVIF_HASINDEX
    avi_header.total_frames = 0; // 稍后更新
    avi_header.initial_frames = 0;
    avi_header.streams = 2; // 视频 + 音频
    avi_header.suggested_buffer_size = 0;
    avi_header.width = video_width;
    avi_header.height = video_height;
    
    write_bytes((const uint8_t *)&avi_header, sizeof(AviHeader));
    
    // 写入流头信息
    write_stream_headers();
    
    // LIST movi
    write_fourcc("LIST");
    movi_list_pos = file->get_position(); // 记录位置用于稍后更新大小
    write_uint32(0); // movi大小，稍后更新
    write_fourcc("movi");
}

void EnhancedAviWriter::write_stream_headers() {
    // 视频流头
    write_video_stream_header();
    // 音频流头
    write_audio_stream_header();
}

void EnhancedAviWriter::write_video_stream_header() {
    // LIST strl（视频流）
    write_fourcc("LIST");
    write_uint32(116); // strl大小
    write_fourcc("strl");
    
    // strh（流头）
    write_fourcc("strh");
    write_uint32(56); // strh大小
    
    StreamHeader video_header = {};
    memcpy(video_header.fourcc_type, "vids", 4);
    memcpy(video_header.fourcc_handler, "MJPG", 4);
    video_header.flags = 0;
    video_header.priority = 0;
    video_header.language = 0;
    video_header.initial_frames = 0;
    video_header.scale = 1;
    video_header.rate = video_fps;
    video_header.start = 0;
    video_header.length = 0; // 稍后更新
    video_header.suggested_buffer_size = 0;
    video_header.quality = (uint32_t)(jpeg_quality * 10000);
    video_header.sample_size = 0; // 可变大小
    video_header.left = video_header.top = 0;
    video_header.right = video_width;
    video_header.bottom = video_height;
    
    write_bytes((const uint8_t *)&video_header, sizeof(StreamHeader));
    
    // strf（流格式）
    write_fourcc("strf");
    write_uint32(40); // strf大小（BITMAPINFOHEADER）
    
    // BITMAPINFOHEADER
    write_uint32(40);      // biSize
    write_uint32(video_width);   // biWidth
    write_uint32(video_height);  // biHeight
    write_uint16(1);       // biPlanes
    write_uint16(24);      // biBitCount
    write_fourcc("MJPG");  // biCompression
    write_uint32(video_width * video_height * 3); // biSizeImage
    write_uint32(0);       // biXPelsPerMeter
    write_uint32(0);       // biYPelsPerMeter
    write_uint32(0);       // biClrUsed
    write_uint32(0);       // biClrImportant
}

void EnhancedAviWriter::write_audio_stream_header() {
    // LIST strl（音频流）
    write_fourcc("LIST");
    write_uint32(92); // strl大小
    write_fourcc("strl");
    
    // strh（流头）
    write_fourcc("strh");
    write_uint32(56); // strh大小
    
    StreamHeader audio_header = {};
    memcpy(audio_header.fourcc_type, "auds", 4);
    memcpy(audio_header.fourcc_handler, "    ", 4); // PCM没有特定handler
    audio_header.flags = 0;
    audio_header.priority = 0;
    audio_header.language = 0;
    audio_header.initial_frames = 0;
    audio_header.scale = 1;
    audio_header.rate = audio_sample_rate;
    audio_header.start = 0;
    audio_header.length = 0; // 稍后更新
    audio_header.suggested_buffer_size = 0;
    audio_header.quality = 0;
    audio_header.sample_size = audio_channels * 4; // 32位PCM
    
    write_bytes((const uint8_t *)&audio_header, sizeof(StreamHeader));
    
    // strf（流格式）
    write_fourcc("strf");
    write_uint32(18); // strf大小（WAVEFORMATEX）
    
    // WAVEFORMATEX
    write_uint16(1);      // wFormatTag (PCM)
    write_uint16(audio_channels);   // nChannels
    write_uint32(audio_sample_rate); // nSamplesPerSec
    write_uint32(audio_sample_rate * audio_channels * 4); // nAvgBytesPerSec
    write_uint16(audio_channels * 4); // nBlockAlign
    write_uint16(32);     // wBitsPerSample
    write_uint16(0);      // cbSize
}

Error EnhancedAviWriter::write_video_frame(const Ref<Image> &p_image, uint64_t recording_time, 
                                          uint64_t game_time, uint32_t sequence, uint8_t flags) {
    if (p_image.is_null() || !file.is_valid()) {
        return ERR_INVALID_PARAMETER;
    }
    
    // 首先写入时间戳chunk
    TimestampChunk ts_chunk;
    ts_chunk.recording_timestamp = recording_time;
    ts_chunk.game_timestamp = game_time;
    ts_chunk.frame_sequence = sequence;
    ts_chunk.flags = flags;
    
    write_bytes((const uint8_t *)&ts_chunk, sizeof(TimestampChunk));
    
    // 添加时间戳索引条目
    IndexEntry ts_entry;
    memcpy(ts_entry.fourcc, "00ts", 4);
    ts_entry.flags = 0;
    ts_entry.chunk_offset = file->get_position() - movi_list_pos - 8 - sizeof(TimestampChunk);
    ts_entry.chunk_size = sizeof(TimestampChunk);
    index_entries.push_back(ts_entry);
    
    // 转换图像为JPEG
    Ref<Image> img = p_image->duplicate();
    if (img->get_format() != Image::FORMAT_RGB8) {
        img->convert(Image::FORMAT_RGB8);
    }
    
    PackedByteArray jpeg_data = img->save_jpg_to_buffer(jpeg_quality);
    
    // 写入视频数据chunk
    write_fourcc("00db"); // 00=stream 0, db=data block
    write_uint32(jpeg_data.size());
    write_bytes(jpeg_data.ptr(), jpeg_data.size());
    
    // 字节对齐
    if (jpeg_data.size() % 2 == 1) {
        write_uint8(0);
    }
    
    // 添加视频索引条目
    IndexEntry video_entry;
    memcpy(video_entry.fourcc, "00db", 4);
    video_entry.flags = 0x10; // AVIIF_KEYFRAME
    video_entry.chunk_offset = file->get_position() - movi_list_pos - 8 - jpeg_data.size();
    if (jpeg_data.size() % 2 == 1) {
        video_entry.chunk_offset -= 1;
    }
    video_entry.chunk_size = jpeg_data.size();
    index_entries.push_back(video_entry);
    
    video_frame_count++;
    
    return OK;
}

Error EnhancedAviWriter::write_audio_chunk(const int32_t *p_audio_data, int p_frame_count, 
                                          uint64_t recording_time) {
    if (!p_audio_data || p_frame_count <= 0 || !file.is_valid()) {
        return ERR_INVALID_PARAMETER;
    }
    
    uint32_t data_size = p_frame_count * audio_channels * 4; // 32位PCM
    
    // 写入音频数据chunk
    write_fourcc("01wb"); // 01=stream 1, wb=wave buffer
    write_uint32(data_size);
    write_bytes((const uint8_t *)p_audio_data, data_size);
    
    // 字节对齐
    if (data_size % 2 == 1) {
        write_uint8(0);
    }
    
    // 添加音频索引条目
    IndexEntry audio_entry;
    memcpy(audio_entry.fourcc, "01wb", 4);
    audio_entry.flags = 0;
    audio_entry.chunk_offset = file->get_position() - movi_list_pos - 8 - data_size;
    if (data_size % 2 == 1) {
        audio_entry.chunk_offset -= 1;
    }
    audio_entry.chunk_size = data_size;
    index_entries.push_back(audio_entry);
    
    audio_chunk_count++;
    
    return OK;
}

void EnhancedAviWriter::close() {
    if (!file.is_valid() || !file->is_open()) {
        return;
    }
    
    // 记录当前位置（movi结束位置）
    uint64_t movi_end_pos = file->get_position();
    
    // 写入索引
    write_fourcc("idx1");
    write_uint32(index_entries.size() * 16); // 每个索引条目16字节
    
    for (const IndexEntry &entry : index_entries) {
        write_bytes((const uint8_t *)entry.fourcc, 4);
        write_uint32(entry.flags);
        write_uint32(entry.chunk_offset);
        write_uint32(entry.chunk_size);
    }
    
    uint64_t file_end_pos = file->get_position();
    
    // 更新文件头中的大小信息
    finalize_headers();
    
    file->close();
    
    print_line(String("AVI recording completed: ") + file_path);
    print_line(String("Video frames: ") + String::num_int64(video_frame_count));
    print_line(String("Audio chunks: ") + String::num_int64(audio_chunk_count));
    print_line(String("Index entries: ") + String::num_int64(index_entries.size()));
}

void EnhancedAviWriter::finalize_headers() {
    // 更新RIFF文件大小
    file->seek(4);
    uint64_t file_size = file->get_length() - 8;
    write_uint32((uint32_t)file_size);
    
    // 更新avih中的总帧数
    file->seek(48); // avih.total_frames的位置
    write_uint32(video_frame_count);
    
    // 更新movi大小
    file->seek(movi_list_pos);
    uint64_t movi_size = file->get_length() - movi_list_pos - 4;
    write_uint32((uint32_t)movi_size);
    
    // 恢复文件指针到末尾
    file->seek_end();
} 