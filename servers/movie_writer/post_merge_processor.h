/**************************************************************************/
/*  post_merge_processor.h                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#ifndef POST_MERGE_PROCESSOR_H
#define POST_MERGE_PROCESSOR_H

#include "core/string/ustring.h"
#include "core/error/error_macros.h"

/**
 * Post-processing video/audio file merger
 * Implements simplified approach: independent audio/video sampling + post-merge
 * Similar to: ffmpeg -i video.avi -i audio.avi -c copy output.avi -y
 */
class PostMergeProcessor {
public:
    // Merge methods supported
    enum MergeMethod {
        METHOD_FFMPEG_SYSTEM,   // Use system FFmpeg command
        METHOD_CUSTOM_AVI,      // Custom AVI container merge (Phase 2)
        METHOD_NONE             // No merge, keep separate files
    };
    
    // Merge configuration
    struct MergeConfig {
        MergeMethod method = METHOD_FFMPEG_SYSTEM;
        bool keep_intermediate_files = false;    // Keep original video/audio files after merge
        bool enable_debug_output = true;        // Enable debug logging
        String ffmpeg_path = "ffmpeg";          // FFmpeg executable path
        
        MergeConfig() {}
    };
    
    // Merge result information
    struct MergeResult {
        Error error_code = OK;
        String output_file_path;
        String error_message;
        float merge_duration_seconds = 0.0f;
        bool intermediate_files_cleaned = false;
        
        MergeResult() {}
    };

private:
    MergeConfig config;
    
    // FFmpeg system command implementation
    Error ffmpeg_system_merge(const String &video_path, const String &audio_path, const String &output_path, MergeResult &result);
    
    // Custom AVI merge implementation (Phase 2)
    Error custom_avi_merge(const String &video_path, const String &audio_path, const String &output_path, MergeResult &result);
    
    // Utility methods
    bool check_ffmpeg_availability();
    bool file_exists(const String &path);
    Error cleanup_intermediate_files(const String &video_path, const String &audio_path);
    uint64_t get_file_size(const String &path);

public:
    PostMergeProcessor();
    ~PostMergeProcessor();
    
    /**
     * Set merge configuration
     */
    void set_config(const MergeConfig &p_config);
    const MergeConfig &get_config() const { return config; }
    
    /**
     * Main merge interface
     * Merges separate video and audio files into a single output file
     * 
     * @param video_path Path to video file (e.g., "movie_video.avi")
     * @param audio_path Path to audio file (e.g., "movie_audio.avi") 
     * @param output_path Path for merged output file (e.g., "movie_merged.avi")
     * @return MergeResult with operation result and details
     */
    MergeResult merge_files(const String &video_path, const String &audio_path, const String &output_path);
    
    /**
     * Check if merge method is available on current platform
     */
    bool is_method_available(MergeMethod method);
    
    /**
     * Get recommended merge method for current platform
     */
    MergeMethod get_recommended_method();
    
    /**
     * Get human-readable method name
     */
    String get_method_name(MergeMethod method);
    
    /**
     * Validate file paths and configuration before merge
     */
    Error validate_merge_request(const String &video_path, const String &audio_path, const String &output_path);
};

#endif // POST_MERGE_PROCESSOR_H