/**************************************************************************/
/*  post_merge_processor.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "post_merge_processor.h"
#include "core/os/os.h"
#include "core/io/file_access.h"
#include "core/io/dir_access.h"
#include "core/os/time.h"

PostMergeProcessor::PostMergeProcessor() {
    // Initialize with default configuration
}

PostMergeProcessor::~PostMergeProcessor() {
}

void PostMergeProcessor::set_config(const MergeConfig &p_config) {
    config = p_config;
}

PostMergeProcessor::MergeResult PostMergeProcessor::merge_files(const String &video_path, const String &audio_path, const String &output_path) {
    MergeResult result;
    uint64_t start_time = OS::get_singleton()->get_ticks_usec();
    
    if (config.enable_debug_output) {
        print_line("PostMergeProcessor: Starting merge operation");
        print_line("  Video file: " + video_path);
        print_line("  Audio file: " + audio_path);
        print_line("  Output file: " + output_path);
        print_line("  Method: " + get_method_name(config.method));
    }
    
    // Validate merge request
    Error validation_error = validate_merge_request(video_path, audio_path, output_path);
    if (validation_error != OK) {
        result.error_code = validation_error;
        result.error_message = "Validation failed";
        return result;
    }
    
    // Execute merge based on selected method
    switch (config.method) {
        case METHOD_FFMPEG_SYSTEM:
            result.error_code = ffmpeg_system_merge(video_path, audio_path, output_path, result);
            break;
            
        case METHOD_CUSTOM_AVI:
            result.error_code = custom_avi_merge(video_path, audio_path, output_path, result);
            break;
            
        case METHOD_NONE:
            if (config.enable_debug_output) {
                print_line("PostMergeProcessor: No merge requested, keeping separate files");
            }
            result.error_code = OK;
            result.output_file_path = video_path; // Return video file as primary output
            break;
            
        default:
            result.error_code = ERR_PARAMETER_RANGE_ERROR;
            result.error_message = "Unknown merge method";
            break;
    }
    
    // Calculate merge duration
    uint64_t end_time = OS::get_singleton()->get_ticks_usec();
    result.merge_duration_seconds = (end_time - start_time) / 1000000.0f;
    
    // Clean up intermediate files if requested and merge was successful
    if (result.error_code == OK && config.keep_intermediate_files == false && config.method != METHOD_NONE) {
        Error cleanup_error = cleanup_intermediate_files(video_path, audio_path);
        result.intermediate_files_cleaned = (cleanup_error == OK);
        
        if (config.enable_debug_output) {
            if (result.intermediate_files_cleaned) {
                print_line("PostMergeProcessor: Intermediate files cleaned up successfully");
            } else {
                print_line("PostMergeProcessor: Warning - Failed to clean up intermediate files");
            }
        }
    }
    
    // Store output file path
    if (result.error_code == OK && config.method != METHOD_NONE) {
        result.output_file_path = output_path;
    }
    
    if (config.enable_debug_output) {
        print_line(String("PostMergeProcessor: Merge completed in ") + String::num(result.merge_duration_seconds, 2) + " seconds");
        if (result.error_code != OK) {
            print_line("PostMergeProcessor: Merge failed - " + result.error_message);
        }
    }
    
    return result;
}

Error PostMergeProcessor::ffmpeg_system_merge(const String &video_path, const String &audio_path, const String &output_path, MergeResult &result) {
#ifdef WEB_ENABLED
    // Web platform doesn't support system command execution
    result.error_message = "FFmpeg system merge not supported on Web platform";
    return ERR_UNAVAILABLE;
#else
    if (!check_ffmpeg_availability()) {
        result.error_message = "FFmpeg not found in system PATH";
        return ERR_FILE_NOT_FOUND;
    }
    
    // Build FFmpeg command arguments
    List<String> args;
    args.push_back("-i");
    args.push_back(video_path);
    args.push_back("-i");
    args.push_back(audio_path);
    args.push_back("-c");
    args.push_back("copy");  // Stream copy, no re-encoding
    args.push_back(output_path);
    args.push_back("-y");    // Overwrite output file if exists
    
    if (config.enable_debug_output) {
        String command_line = config.ffmpeg_path;
        for (const String &arg : args) {
            command_line += " \"" + arg + "\"";
        }
        print_line("PostMergeProcessor: Executing - " + command_line);
    }
    
    // Execute FFmpeg command
    String stdout_output;
    int exit_code = OS::get_singleton()->execute(config.ffmpeg_path, args, &stdout_output);
    
    if (config.enable_debug_output && !stdout_output.is_empty()) {
        print_line("PostMergeProcessor: FFmpeg output - " + stdout_output);
    }
    
    if (exit_code != 0) {
        result.error_message = String("FFmpeg failed with exit code ") + String::num_int64(exit_code);
        if (!stdout_output.is_empty()) {
            result.error_message += " - " + stdout_output;
        }
        return ERR_FILE_CANT_WRITE;
    }
    
    // Verify output file was created
    if (!file_exists(output_path)) {
        result.error_message = "Output file was not created by FFmpeg";
        return ERR_FILE_CANT_WRITE;
    }
    
    return OK;
#endif
}

Error PostMergeProcessor::custom_avi_merge(const String &video_path, const String &audio_path, const String &output_path, MergeResult &result) {
    // Phase 2 implementation - placeholder for now
    result.error_message = "Custom AVI merge not implemented yet (Phase 2)";
    return ERR_UNAVAILABLE;
}

bool PostMergeProcessor::check_ffmpeg_availability() {
#ifdef WEB_ENABLED
    return false;
#else
    // Try to execute ffmpeg -version to check availability
    List<String> args;
    args.push_back("-version");
    
    String output;
    int exit_code = OS::get_singleton()->execute(config.ffmpeg_path, args, &output);
    
    return (exit_code == 0 && output.contains("ffmpeg"));
#endif
}

bool PostMergeProcessor::file_exists(const String &path) {
    Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
    return file.is_valid();
}

Error PostMergeProcessor::cleanup_intermediate_files(const String &video_path, const String &audio_path) {
    Error video_error = OK;
    Error audio_error = OK;
    
    if (file_exists(video_path)) {
        String video_dir = video_path.get_base_dir();
        Ref<DirAccess> dir = DirAccess::open(video_dir);
        if (dir.is_valid()) {
            video_error = dir->remove(video_path.get_file());
        } else {
            video_error = ERR_FILE_CANT_OPEN;
        }
    }
    
    if (file_exists(audio_path)) {
        String audio_dir = audio_path.get_base_dir();
        Ref<DirAccess> dir = DirAccess::open(audio_dir);
        if (dir.is_valid()) {
            audio_error = dir->remove(audio_path.get_file());
        } else {
            audio_error = ERR_FILE_CANT_OPEN;
        }
    }
    
    // Return error if either cleanup failed
    if (video_error != OK) return video_error;
    if (audio_error != OK) return audio_error;
    
    return OK;
}

uint64_t PostMergeProcessor::get_file_size(const String &path) {
    Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
    if (file.is_valid()) {
        return file->get_length();
    }
    return 0;
}

bool PostMergeProcessor::is_method_available(MergeMethod method) {
    switch (method) {
        case METHOD_FFMPEG_SYSTEM:
#ifdef WEB_ENABLED
            return false;
#else
            return check_ffmpeg_availability();
#endif
            
        case METHOD_CUSTOM_AVI:
            // Always available (Phase 2)
            return false; // Not implemented yet
            
        case METHOD_NONE:
            return true;
            
        default:
            return false;
    }
}

PostMergeProcessor::MergeMethod PostMergeProcessor::get_recommended_method() {
#ifdef WEB_ENABLED
    return METHOD_NONE; // Web platform keeps separate files
#else
    if (is_method_available(METHOD_FFMPEG_SYSTEM)) {
        return METHOD_FFMPEG_SYSTEM;
    } else if (is_method_available(METHOD_CUSTOM_AVI)) {
        return METHOD_CUSTOM_AVI;
    } else {
        return METHOD_NONE;
    }
#endif
}

String PostMergeProcessor::get_method_name(MergeMethod method) {
    switch (method) {
        case METHOD_FFMPEG_SYSTEM:
            return "FFmpeg System Command";
        case METHOD_CUSTOM_AVI:
            return "Custom AVI Merge";
        case METHOD_NONE:
            return "No Merge";
        default:
            return "Unknown";
    }
}

Error PostMergeProcessor::validate_merge_request(const String &video_path, const String &audio_path, const String &output_path) {
    // Check if method is available
    if (!is_method_available(config.method)) {
        return ERR_UNAVAILABLE;
    }
    
    // For non-merge method, skip file validation
    if (config.method == METHOD_NONE) {
        return OK;
    }
    
    // Check input files exist
    if (!file_exists(video_path)) {
        return ERR_FILE_NOT_FOUND;
    }
    
    if (!file_exists(audio_path)) {
        return ERR_FILE_NOT_FOUND;
    }
    
    // Check input files are not empty
    if (get_file_size(video_path) == 0) {
        return ERR_FILE_CORRUPT;
    }
    
    if (get_file_size(audio_path) == 0) {
        return ERR_FILE_CORRUPT;
    }
    
    // Check output path is valid (directory exists)
    String output_dir = output_path.get_base_dir();
    if (!output_dir.is_empty()) {
        Ref<DirAccess> dir = DirAccess::open(output_dir);
        if (!dir.is_valid()) {
            return ERR_FILE_BAD_PATH;
        }
    }
    
    return OK;
}