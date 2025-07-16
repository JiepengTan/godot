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
    total_audio_samples = 0;
    hdrl_size_pos = 0;
    video_length_pos = 0;
    audio_length_pos = 0;
    movi_list_pos = 0;
    movi_size_pos = 0;
    first_frame_time = 0;
    first_frame_written = false;
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

uint32_t EnhancedAviWriter::get_current_chunk_offset() const {
    return (uint32_t)(file->get_position() - movi_list_pos);
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
    
    // Open file
    file = FileAccess::open(p_path, FileAccess::WRITE);
    if (file.is_null()) {
        ERR_PRINT("Cannot create AVI file: " + p_path);
        return ERR_FILE_CANT_OPEN;
    }
    
    // Write AVI file header structure
    write_avi_header();
    
    return OK;
}

void EnhancedAviWriter::write_avi_header() {
    // RIFF header
    write_fourcc("RIFF");
    write_uint32(0); // File size, update later
    write_fourcc("AVI ");
    
    // LIST hdrl
    write_fourcc("LIST");
    hdrl_size_pos = file->get_position(); // Record hdrl size position
    write_uint32(0); // hdrl size, update later
    write_fourcc("hdrl");
    
    // Main AVI header
    write_fourcc("avih");
    write_uint32(56); // avih chunk size
    
    AviHeader avi_header = {};
    avi_header.microsec_per_frame = 1000000 / video_fps;
    avi_header.max_bytes_per_sec = video_width * video_height * 3 * video_fps; // Estimate
    avi_header.padding_granularity = 0;
    avi_header.flags = 0x10; // AVIF_HASINDEX
    avi_header.total_frames = 0; // Update later
    avi_header.initial_frames = 0;
    avi_header.streams = 2; // Video + Audio
    avi_header.suggested_buffer_size = 0;
    avi_header.width = video_width;
    avi_header.height = video_height;
    
    write_bytes((const uint8_t *)&avi_header, sizeof(AviHeader));
    
    // Write stream header information
    write_stream_headers();
    
    // LIST movi
    write_fourcc("LIST");
    movi_size_pos = file->get_position(); // Record movi size position
    write_uint32(0); // movi size, update later
    write_fourcc("movi");
    movi_list_pos = file->get_position(); // movi data start position
}

void EnhancedAviWriter::write_stream_headers() {
    // Video stream header
    write_video_stream_header();
    // Audio stream header
    write_audio_stream_header();
}

void EnhancedAviWriter::write_video_stream_header() {
    // LIST strl (video stream)
    write_fourcc("LIST");
    write_uint32(116); // strl size
    write_fourcc("strl");
    
    // strh (stream header)
    write_fourcc("strh");
    write_uint32(56); // strh size
    
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
    video_header.length = 0; // Update later
    video_header.suggested_buffer_size = 0;
    video_header.quality = (uint32_t)(jpeg_quality * 10000);
    video_header.sample_size = 0; // Variable size
    video_header.left = video_header.top = 0;
    video_header.right = video_width;
    video_header.bottom = video_height;
    
    // Record the position of the video stream length (before writing)
    video_length_pos = file->get_position() + 32; // Offset of the length field
    
    write_bytes((const uint8_t *)&video_header, sizeof(StreamHeader));
    
    // strf (stream format)
    write_fourcc("strf");
    write_uint32(40); // strf size (BITMAPINFOHEADER)
    
    // BITMAPINFOHEADER
    write_uint32(40); // biSize
    write_uint32(video_width); // biWidth
    write_uint32(video_height); // biHeight
    write_uint16(1); // biPlanes
    write_uint16(24); // biBitCount
    write_fourcc("MJPG"); // biCompression
    write_uint32(video_width * video_height * 3); // biSizeImage
    write_uint32(0); // biXPelsPerMeter
    write_uint32(0); // biYPelsPerMeter
    write_uint32(0); // biClrUsed
    write_uint32(0); // biClrImportant
}

void EnhancedAviWriter::write_audio_stream_header() {
    // LIST strl (audio stream)
    write_fourcc("LIST");
    write_uint32(92); // strl size
    write_fourcc("strl");
    
    // strh (stream header)
    write_fourcc("strh");
    write_uint32(56); // strh size
    
    StreamHeader audio_header = {};
    memcpy(audio_header.fourcc_type, "auds", 4);
    memcpy(audio_header.fourcc_handler, "    ", 4); // No specific handler for PCM
    audio_header.flags = 0;
    audio_header.priority = 0;
    audio_header.language = 0;
    audio_header.initial_frames = 0;
    audio_header.scale = 1;
    audio_header.rate = audio_sample_rate;
    audio_header.start = 0;
    audio_header.length = 0; // Update later
    audio_header.suggested_buffer_size = 0;
    audio_header.quality = 0;
    audio_header.sample_size = audio_channels * 2; // 16-bit PCM
    
    // Record the position of the audio stream length
    audio_length_pos = file->get_position() + 32; // Offset of the length field
    
    write_bytes((const uint8_t *)&audio_header, sizeof(StreamHeader));
    
    // strf (stream format)
    write_fourcc("strf");
    write_uint32(18); // strf size (WAVEFORMATEX)
    
    // WAVEFORMATEX
    write_uint16(1); // wFormatTag (PCM)
    write_uint16(audio_channels); // nChannels
    write_uint32(audio_sample_rate); // nSamplesPerSec
    write_uint32(audio_sample_rate * audio_channels * 2); // nAvgBytesPerSec (16-bit)
    write_uint16(audio_channels * 2); // nBlockAlign (16-bit)
    write_uint16(16); // wBitsPerSample (changed to 16-bit)
    write_uint16(0); // cbSize
}

Error EnhancedAviWriter::write_video_frame(const Ref<Image> &p_image, uint64_t recording_time, 
                                          uint64_t game_time, uint32_t sequence, uint8_t flags) {
    if (p_image.is_null() || !file.is_valid()) {
        return ERR_INVALID_PARAMETER;
    }
    
    // Record the time of the first frame as a reference
    if (!first_frame_written) {
        first_frame_time = recording_time;
        first_frame_written = true;
    }
    
    // Convert image to JPEG
    Ref<Image> img = p_image->duplicate();
    if (img->get_format() != Image::FORMAT_RGB8) {
        img->convert(Image::FORMAT_RGB8);
    }
    
    PackedByteArray jpeg_data = img->save_jpg_to_buffer(jpeg_quality);
    
    // Record current chunk start position (for index)
    uint32_t chunk_offset = get_current_chunk_offset();
    
    // Write video data chunk
    write_fourcc("00db"); // 00=stream 0, db=data block
    write_uint32(jpeg_data.size());
    write_bytes(jpeg_data.ptr(), jpeg_data.size());
    
    // Byte alignment
    if (jpeg_data.size() % 2 == 1) {
        write_uint8(0);
    }
    
    // Add video index entry
    IndexEntry video_entry;
    memcpy(video_entry.fourcc, "00db", 4);
    video_entry.flags = 0x10; // AVIIF_KEYFRAME
    video_entry.chunk_offset = chunk_offset;
    video_entry.chunk_size = jpeg_data.size();
    index_entries.push_back(video_entry);
    
    video_frame_count++;
    
    if (video_frame_count % 100 == 0) {
        print_line(String("Video frames written: ") + String::num_int64(video_frame_count));
    }
    
    return OK;
}

Error EnhancedAviWriter::write_audio_chunk(const int32_t *p_audio_data, int p_frame_count, 
                                          uint64_t recording_time) {
    if (!p_audio_data || p_frame_count <= 0 || !file.is_valid()) {
        return ERR_INVALID_PARAMETER;
    }
    
    // Convert 32-bit audio data to 16-bit PCM
    Vector<int16_t> audio_16bit;
    audio_16bit.resize(p_frame_count * audio_channels);
    
    for (int i = 0; i < p_frame_count * audio_channels; i++) {
        // Convert 32-bit data to 16-bit with proper scaling
        int32_t sample_32 = p_audio_data[i];
        // Assuming 32-bit data is in the range of a 32-bit integer, convert to 16-bit integer
        int16_t sample_16 = (int16_t)CLAMP(sample_32 >> 16, -32768, 32767);
        audio_16bit.write[i] = sample_16;
    }
    
    uint32_t data_size = p_frame_count * audio_channels * 2; // 16-bit PCM
    
    // Record current chunk start position
    uint32_t chunk_offset = get_current_chunk_offset();
    
    // Write audio data chunk
    write_fourcc("01wb"); // 01=stream 1, wb=wave buffer
    write_uint32(data_size);
    write_bytes((const uint8_t *)audio_16bit.ptr(), data_size);
    
    // Byte alignment
    if (data_size % 2 == 1) {
        write_uint8(0);
    }
    
    // Add audio index entry
    IndexEntry audio_entry;
    memcpy(audio_entry.fourcc, "01wb", 4);
    audio_entry.flags = 0;
    audio_entry.chunk_offset = chunk_offset;
    audio_entry.chunk_size = data_size;
    index_entries.push_back(audio_entry);
    
    audio_chunk_count++;
    total_audio_samples += p_frame_count;
    
    if (audio_chunk_count % 100 == 0) {
        print_line(String("Audio chunks written: ") + String::num_int64(audio_chunk_count));
    }
    
    return OK;
}

void EnhancedAviWriter::close() {
    if (!file.is_valid() || !file->is_open()) {
        return;
    }
    
    // Write index
    write_fourcc("idx1");
    write_uint32(index_entries.size() * 16); // 16 bytes per index entry
    
    for (const IndexEntry &entry : index_entries) {
        write_bytes((const uint8_t *)entry.fourcc, 4);
        write_uint32(entry.flags);
        write_uint32(entry.chunk_offset);
        write_uint32(entry.chunk_size);
    }
    
    // Update size information in the file header
    finalize_headers();
    
    file->close();
    
    print_line(String("=== AVI Combined Recording Completed ==="));
    print_line(String("Output file: ") + file_path);
    print_line(String("Video frames: ") + String::num_int64(video_frame_count));
    print_line(String("Audio chunks: ") + String::num_int64(audio_chunk_count)); 
    print_line(String("Total audio samples: ") + String::num_int64(total_audio_samples));
    print_line(String("Index entries: ") + String::num_int64(index_entries.size()));
    
    if (first_frame_written && video_frame_count > 0) {
        float duration_sec = (float)video_frame_count / (float)video_fps;
        print_line(String("Video duration: ") + String::num(duration_sec, 2) + " seconds");
        
        if (total_audio_samples > 0) {
            float audio_duration_sec = (float)total_audio_samples / (float)audio_sample_rate;
            print_line(String("Audio duration: ") + String::num(audio_duration_sec, 2) + " seconds");
            
            float sync_diff = Math::abs(duration_sec - audio_duration_sec);
            if (sync_diff < 0.1f) {
                print_line("✓ Audio-video sync normal");
            } else {
                print_line(String("⚠ Audio-video duration difference: ") + String::num(sync_diff, 3) + " seconds");
            }
        }
    }
}

void EnhancedAviWriter::finalize_headers() {
    uint64_t current_pos = file->get_position();
    
    // 1. Update RIFF file size
    file->seek(4);
    uint64_t file_size = current_pos - 8;
    write_uint32((uint32_t)file_size);
    
    // 2. Update hdrl size
    file->seek(hdrl_size_pos);
    uint64_t hdrl_size = movi_size_pos - hdrl_size_pos - 4;
    write_uint32((uint32_t)hdrl_size);
    
    // 3. Update total frames in avih
    file->seek(48); // Position of avih.total_frames (fixed offset)
    write_uint32(video_frame_count);
    
    // 4. Update video stream length
    if (video_length_pos > 0) {
        file->seek(video_length_pos);
        write_uint32(video_frame_count);
    }
    
    // 5. Update audio stream length (in number of samples)
    if (audio_length_pos > 0) {
        file->seek(audio_length_pos);
        write_uint32((uint32_t)total_audio_samples);
    }
    
    // 6. Update movi size
    file->seek(movi_size_pos);
    uint64_t movi_size = current_pos - movi_size_pos - 4;
    write_uint32((uint32_t)movi_size);
    
    // Restore file pointer to the end
    file->seek(current_pos);
} 