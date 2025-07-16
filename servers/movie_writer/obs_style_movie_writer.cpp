/**************************************************************************/
/*  obs_style_movie_writer.cpp                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "obs_style_movie_writer.h"
#include "core/string/print_string.h"
#include "core/config/project_settings.h"
#include "servers/audio_server.h"
#include "servers/rendering_server.h"
#include "servers/display_server.h"

#ifdef WEB_ENABLED
#include "platform/web/godot_audio.h"
#endif

ObsStyleMovieWriter::ObsStyleMovieWriter() :
    current_state(STATE_UNINITIALIZED),
    game_frame_sequence(0),
    recording_start_time(0),
    audio_driver_replaced(false),
    last_add_frame_time(0),
    frames_added_count(0) {
    
    // Load configuration from project settings
    obs_config = get_standard_config();
    
    // Apply project settings
    if (ProjectSettings::get_singleton()->has_setting("movie_writer/obs_video_fps")) {
        obs_config.video_fps = GLOBAL_GET("movie_writer/obs_video_fps");
    }
    if (ProjectSettings::get_singleton()->has_setting("movie_writer/obs_video_quality")) {
        obs_config.jpeg_quality = GLOBAL_GET("movie_writer/obs_video_quality");
    }
    if (ProjectSettings::get_singleton()->has_setting("movie_writer/obs_audio_sample_rate")) {
        obs_config.audio_sample_rate = GLOBAL_GET("movie_writer/obs_audio_sample_rate");
    }
    if (ProjectSettings::get_singleton()->has_setting("movie_writer/obs_audio_channels")) {
        obs_config.audio_channels = GLOBAL_GET("movie_writer/obs_audio_channels");
    }
    if (ProjectSettings::get_singleton()->has_setting("movie_writer/obs_enable_timestamp_chunks")) {
        obs_config.enable_timestamp_chunks = GLOBAL_GET("movie_writer/obs_enable_timestamp_chunks");
    }
    if (ProjectSettings::get_singleton()->has_setting("movie_writer/obs_enable_repeat_frame_marking")) {
        obs_config.enable_repeat_frame_marking = GLOBAL_GET("movie_writer/obs_enable_repeat_frame_marking");
    }
    if (ProjectSettings::get_singleton()->has_setting("movie_writer/obs_enable_debug_output")) {
        obs_config.enable_debug_output = GLOBAL_GET("movie_writer/obs_enable_debug_output");
    }
    if (ProjectSettings::get_singleton()->has_setting("movie_writer/obs_enable_combined_recording")) {
        obs_config.enable_combined_recording = GLOBAL_GET("movie_writer/obs_enable_combined_recording");
    }
    obs_config.enable_combined_recording = false;
}

ObsStyleMovieWriter::~ObsStyleMovieWriter() {
    if (current_state == STATE_RECORDING) {
        write_end();
    }
    cleanup_components();
}

bool ObsStyleMovieWriter::handles_file(const String &p_path) const {
    return p_path.get_extension().to_lower() == "avi";
}

void ObsStyleMovieWriter::get_supported_extensions(List<String> *r_extensions) const {
    r_extensions->push_back("avi");
}

bool ObsStyleMovieWriter::is_supported_format(const String &p_extension) {
    return p_extension.to_lower() == "avi";
}

uint32_t ObsStyleMovieWriter::get_audio_mix_rate() const {
    return obs_config.audio_sample_rate;
}

AudioServer::SpeakerMode ObsStyleMovieWriter::get_audio_speaker_mode() const {
    return obs_config.audio_channels == 2 ? AudioServer::SPEAKER_MODE_STEREO : AudioServer::SPEAKER_SURROUND_31;
}

Error ObsStyleMovieWriter::write_begin(const Size2i &p_movie_size, uint32_t p_fps, const String &p_base_path) {
    if (current_state != STATE_UNINITIALIZED) {
        ERR_PRINT("ObsStyleMovieWriter: Recorder state is incorrect");
        return ERR_INVALID_PARAMETER;
    }
    
    output_file_path = p_base_path;
    
    // Update video resolution in config
    obs_config.video_width = p_movie_size.width;
    obs_config.video_height = p_movie_size.height;
    
    if (obs_config.enable_debug_output) {
        print_line("=== OBS-style Recording Started ===");
        print_line(String("Output file: ") + output_file_path);
        print_line(String("Video resolution: ") + String::num_int64(obs_config.video_width) + "x" + String::num_int64(obs_config.video_height));
        print_line(String("Target FPS: ") + String::num_int64(obs_config.video_fps));
        print_line(String("Audio config: ") + String::num_int64(obs_config.audio_sample_rate) + "Hz, " + String::num_int64(obs_config.audio_channels) + "ch");
    }
    
    // Validate configuration
    Error config_error = validate_config();
    if (config_error != OK) {
        return config_error;
    }
    
    // Set up recording components
    Error setup_error = setup_components();
    if (setup_error != OK) {
        cleanup_components();
        return setup_error;
    }
    
    // Set up audio capture
    Error audio_error = setup_audio_capture();
    if (audio_error != OK) {
        cleanup_components();
        return audio_error;
    }
    print_line("obs_config.enable_combined_recording = ",obs_config.enable_combined_recording);
    // Start recording thread
    if (obs_config.enable_combined_recording) {
        // Start combined recording thread
        combined_recording_active = true;
        combined_recording_thread = memnew(Thread);
        combined_recording_thread->start(combined_recording_thread_function, this);
        
        if (obs_config.enable_debug_output) {
            print_line("Combined recording thread started");
        }
    } else {
        // Start separate recording thread
        Error video_start_error = video_recorder->start_recording();
        if (video_start_error != OK) {
            ERR_PRINT("ObsStyleMovieWriter: Failed to start video recording");
            cleanup_components();
            return video_start_error;
        }
        
        Error audio_start_error = audio_recorder->start_recording();
        if (audio_start_error != OK) {
            ERR_PRINT("ObsStyleMovieWriter: Failed to start audio recording");
            video_recorder->stop_recording();
            cleanup_components();
            return audio_start_error;
        }
    }
    
    // Update state
    update_recording_state(STATE_RECORDING);
    recording_start_time = OS::get_singleton()->get_ticks_usec();
    game_frame_sequence = 0;
    frames_added_count = 0;
    
    if (obs_config.enable_debug_output) {
        print_line("OBS-style recording initialization completed, starting recording...");
    }
    
    return OK;
}

Error ObsStyleMovieWriter::write_frame(const Ref<Image> &p_image, const int32_t *p_audio_data) {
    if (current_state != STATE_RECORDING) {
        return ERR_UNCONFIGURED;
    }
    
    uint64_t current_time = OS::get_singleton()->get_ticks_usec();
    
    // Update game frame data to double buffer
    if (p_image.is_valid()) {
        frame_buffer->update_frame(p_image, current_time, game_frame_sequence);
        game_frame_sequence++;
        frames_added_count++;
    }
    
    last_add_frame_time = current_time;
    
    // Process audio data
    if (obs_config.enable_combined_recording && p_audio_data) {
        // Combined recording mode: add audio data to buffer
        uint32_t audio_mix_rate = get_audio_mix_rate();
        uint32_t audio_channels = obs_config.audio_channels;
        uint32_t samples_per_frame = audio_mix_rate / 60; // Assume game runs at 60fps
        
        {
            MutexLock lock(audio_buffer_mutex);
            
            // Add audio samples to buffer
            for (uint32_t i = 0; i < samples_per_frame * audio_channels; i++) {
                pending_audio_samples.push_back(p_audio_data[i]);
            }
            
            // Limit buffer size to avoid infinite growth
            uint32_t max_buffer_samples = audio_mix_rate * audio_channels * 5; // 5 seconds buffer
            if (pending_audio_samples.size() > max_buffer_samples) {
                uint32_t excess = pending_audio_samples.size() - max_buffer_samples;
                // Remove excess samples from the beginning
                for (uint32_t i = 0; i < excess; i++) {
                    pending_audio_samples.remove_at(0);
                }
            }
        }
    } else {
        // Separate recording mode: audio captured by HybridAudioDriver
        // p_audio_data parameter is not used in separate recording mode
    }
    
    return OK;
}

void ObsStyleMovieWriter::write_end() {
    if (current_state != STATE_RECORDING) {
        return;
    }
    
    if (obs_config.enable_debug_output) {
        print_line("ObsStyleMovieWriter: Stopping recording...");
    }
    
    update_recording_state(STATE_STOPPING);
    
    if (obs_config.enable_combined_recording) {
        // Stop combined recording
        cleanup_combined_recording();
        
        // Close AVI file
        if (avi_writer) {
            avi_writer->close();
        }
    } else {
        // Stop separate recording thread
        if (video_recorder) {
            video_recorder->stop_recording();
        }
        
        if (audio_recorder) {
            audio_recorder->stop_recording();
        }
        
        // Close AVI file (if used)
        if (avi_writer) {
            avi_writer->close();
        }
    }
    
    // Restore audio driver
    restore_audio_driver();
    
    // Print recording summary
    if (obs_config.enable_debug_output) {
        print_recording_summary();
    }
    
    // Clean up components
    cleanup_components();
    
    update_recording_state(STATE_UNINITIALIZED);
    
    print_line("obs output_file_path===>" + output_file_path);
#ifdef WEB_ENABLED
    // Web platform automatically downloads recorded files
    if (obs_config.enable_debug_output) {
        print_line("ObsStyleMovieWriter: Web platform detected, starting automatic file downloads...");
    }
    
    // Download video file
    if (!output_file_path.is_empty()) {
        String video_file = output_file_path;
        if (obs_config.enable_combined_recording) {
            // Combined recording mode: single AVI file
            video_file += ".avi";
        } else {
            // Separate recording mode: video file
            video_file += "_video.avi";
        }
        
        // Call Web platform file download interface
        CharString video_path_utf8 = video_file.utf8();
        CharString download_name_utf8 = video_file.get_file().utf8();
        int download_result = godot_web_download_file(video_path_utf8.get_data(), download_name_utf8.get_data());
        
        if (download_result == 1) {
            print_line("ObsStyleMovieWriter: Video file download initiated: " + video_file.get_file());
        } else {
            print_line("ObsStyleMovieWriter: Failed to download video file: " + video_file);
        }
    }
    
    // Download audio file (separate recording mode)
    if (!obs_config.enable_combined_recording && !output_file_path.is_empty()) {
        String audio_file = output_file_path + "_audio.avi";
        
        CharString audio_path_utf8 = audio_file.utf8();
        CharString audio_name_utf8 = audio_file.get_file().utf8();
        int audio_download_result = godot_web_download_file(audio_path_utf8.get_data(), audio_name_utf8.get_data());
        
        if (audio_download_result == 1) {
            print_line("ObsStyleMovieWriter: Audio file download initiated: " + audio_file.get_file());
        } else {
            print_line("ObsStyleMovieWriter: Failed to download audio file: " + audio_file);
        }
    }
    
    // Download original Web audio recording (MediaRecorder data)
    CharString web_audio_name = String("web_recorded_audio.webm").utf8();
    int web_audio_result = godot_web_download_recorded_audio(web_audio_name.get_data());
    
    if (web_audio_result == 1) {
        print_line("ObsStyleMovieWriter: Web audio recording download initiated: web_recorded_audio.webm");
    } else {
        print_line("ObsStyleMovieWriter: No web audio recording data to download");
    }
    
    print_line("ObsStyleMovieWriter: Web platform file downloads completed");
#endif
    
    print_line("=== OBS-style Recording Completed ===");
}

Error ObsStyleMovieWriter::setup_components() {
    if (obs_config.enable_combined_recording) {
        return setup_combined_recording();
    }
    
    // Original separate recording mode
    // Create double buffer
    frame_buffer = new ThreadSafeFrameBuffer();
    if (!frame_buffer) {
        ERR_PRINT("ObsStyleMovieWriter: Failed to create frame buffer");
        return ERR_OUT_OF_MEMORY;
    }
    
    // Create video recorder
    video_recorder = new IndependentVideoRecorder();
    if (!video_recorder) {
        ERR_PRINT("ObsStyleMovieWriter: Failed to create video recorder");
        return ERR_OUT_OF_MEMORY;
    }
    
    // Configure video recorder
    IndependentVideoRecorder::RecordingConfig video_config;
    video_config.target_fps = obs_config.video_fps;
    video_config.video_width = obs_config.video_width;
    video_config.video_height = obs_config.video_height;
    video_config.jpeg_quality = obs_config.jpeg_quality;
    video_config.enable_timestamp_chunks = obs_config.enable_timestamp_chunks;
    video_config.enable_repeat_frame_marking = obs_config.enable_repeat_frame_marking;
    
    Error video_init_error = video_recorder->initialize(frame_buffer, output_file_path + "_video.avi", video_config);
    if (video_init_error != OK) {
        ERR_PRINT("ObsStyleMovieWriter: Video recorder initialization failed");
        return video_init_error;
    }
    
    // Create audio recorder
    audio_recorder = new IndependentAudioRecorder();
    if (!audio_recorder) {
        ERR_PRINT("ObsStyleMovieWriter: Failed to create audio recorder");
        return ERR_OUT_OF_MEMORY;
    }
    
    // Configure audio recorder
    IndependentAudioRecorder::AudioConfig audio_config;
    audio_config.sample_rate = obs_config.audio_sample_rate;
    audio_config.channels = obs_config.audio_channels;
    audio_config.chunk_size = (obs_config.audio_sample_rate * obs_config.audio_chunk_ms) / 1000;
    audio_config.buffer_size_seconds = obs_config.audio_buffer_seconds;
    audio_config.enable_audio_monitoring = obs_config.enable_audio_monitoring;
    
    // Note: audio_recorder needs to be associated with HybridAudioDriver in setup_audio_capture
    
    update_recording_state(STATE_INITIALIZED);
    
    return OK;
}

void ObsStyleMovieWriter::cleanup_components() {
    // Clean up combined recording related components
    if (obs_config.enable_combined_recording) {
        cleanup_combined_recording();
    }
    
    // Clean up separate recording components
    if (video_recorder) {
        delete video_recorder;
        video_recorder = nullptr;
    }
    
    if (audio_recorder) {
        delete audio_recorder;
        audio_recorder = nullptr;
    }
    
    if (avi_writer) {
        delete avi_writer;
        avi_writer = nullptr;
    }
    
    if (frame_buffer) {
        delete frame_buffer;
        frame_buffer = nullptr;
    }
    
    hybrid_audio_driver = nullptr; // Managed by MovieWriter, only clear reference
}

Error ObsStyleMovieWriter::setup_audio_capture() {
#ifdef WEB_ENABLED
    // Web platform automatically enables combined recording mode, using MovieWriter's Web audio recording functionality
    if (!obs_config.enable_combined_recording) {
        print_line("ObsStyleMovieWriter: Web platform detected, auto-enabling combined recording mode");
        obs_config.enable_combined_recording = true;
    }
#endif

    if (obs_config.enable_combined_recording) {
        // Combined recording mode: audio data passed directly via write_frame, no need to set up HybridAudioDriver
        if (obs_config.enable_debug_output) {
#ifdef WEB_ENABLED
            print_line("ObsStyleMovieWriter: Combined recording mode (Web): using MediaRecorder API audio from MovieWriter");
#else
            print_line("Combined recording mode: skipping HybridAudioDriver setup, using direct audio transfer");
#endif
        }
        return OK;
    }
    
    // Separate recording mode: requires setting up HybridAudioDriver (only for PC)
#ifdef WEB_ENABLED
    ERR_PRINT("ObsStyleMovieWriter: Separate recording mode is not supported on Web platform. Use combined recording mode.");
    return ERR_UNCONFIGURED;
#else
    // Reuse MovieWriter's HybridAudioDriver to avoid conflicts
    hybrid_audio_driver = MovieWriter::get_hybrid_audio_driver();
    if (!hybrid_audio_driver) {
        ERR_PRINT("ObsStyleMovieWriter: MovieWriter's HybridAudioDriver not available");
        return ERR_UNCONFIGURED;
    }
    
    print_line("ObsStyleMovieWriter: Reusing MovieWriter's HybridAudioDriver");
    
    // Configure audio recorder
    IndependentAudioRecorder::AudioConfig audio_config;
    audio_config.sample_rate = obs_config.audio_sample_rate;
    audio_config.channels = obs_config.audio_channels;
    audio_config.chunk_size = (obs_config.audio_sample_rate * obs_config.audio_chunk_ms) / 1000;
    audio_config.buffer_size_seconds = obs_config.audio_buffer_seconds;
    audio_config.enable_audio_monitoring = obs_config.enable_audio_monitoring;
    
    Error audio_init_error = audio_recorder->initialize(hybrid_audio_driver, output_file_path + "_audio.avi", audio_config);
    if (audio_init_error != OK) {
        ERR_PRINT("ObsStyleMovieWriter: Audio recorder initialization failed");
        return audio_init_error;
    }
    
    // Register audio recorder with HybridAudioDriver
    hybrid_audio_driver->register_audio_recorder(audio_recorder);
    
    // Enable recording mode
    hybrid_audio_driver->enable_recording(true);
    
    // HybridAudioDriver has already been started and registered by MovieWriter, no need to repeat
    print_line("ObsStyleMovieWriter: Using existing HybridAudioDriver from MovieWriter");
#endif
    
    return OK;
}

void ObsStyleMovieWriter::restore_audio_driver() {
    if (hybrid_audio_driver && audio_recorder) {
        // Unregister audio recorder (but do not delete or stop HybridAudioDriver, it's managed by MovieWriter)
        hybrid_audio_driver->unregister_audio_recorder(audio_recorder);
        print_line("ObsStyleMovieWriter: Audio recorder unregistered from HybridAudioDriver");
    }
    
    // Only clear reference, do not delete object (HybridAudioDriver is managed by MovieWriter)
    hybrid_audio_driver = nullptr;
    original_audio_driver = nullptr;
}

void ObsStyleMovieWriter::update_recording_state(RecordingState new_state) {
    if (current_state != new_state) {
        current_state = new_state;
        
        if (obs_config.enable_debug_output) {
            print_line(String("ObsStyleMovieWriter: State changed to ") + get_state_name());
        }
    }
}

Error ObsStyleMovieWriter::validate_config() const {
    if (obs_config.video_width == 0 || obs_config.video_height == 0) {
        ERR_PRINT("ObsStyleMovieWriter: Invalid video resolution");
        return ERR_INVALID_PARAMETER;
    }
    
    if (obs_config.video_fps == 0 || obs_config.video_fps > 120) {
        ERR_PRINT("ObsStyleMovieWriter: Invalid video frame rate");
        return ERR_INVALID_PARAMETER;
    }
    
    if (obs_config.audio_sample_rate < 8000 || obs_config.audio_sample_rate > 192000) {
        ERR_PRINT("ObsStyleMovieWriter: Invalid audio sample rate");
        return ERR_INVALID_PARAMETER;
    }
    
    if (obs_config.audio_channels == 0 || obs_config.audio_channels > 8) {
        ERR_PRINT("ObsStyleMovieWriter: Invalid audio channel count");
        return ERR_INVALID_PARAMETER;
    }
    
    if (obs_config.jpeg_quality < 0.1f || obs_config.jpeg_quality > 1.0f) {
        ERR_PRINT("ObsStyleMovieWriter: Invalid JPEG quality");
        return ERR_INVALID_PARAMETER;
    }
    
    return OK;
}

String ObsStyleMovieWriter::get_state_name() const {
    switch (current_state) {
        case STATE_UNINITIALIZED: return "Uninitialized";
        case STATE_INITIALIZED: return "Initialized";
        case STATE_RECORDING: return "Recording";
        case STATE_STOPPING: return "Stopping";
        case STATE_ERROR: return "Error state";
        default: return "Unknown state";
    }
}

// Configuration presets
ObsStyleMovieWriter::ObsRecordingConfig ObsStyleMovieWriter::get_high_quality_config() {
    ObsRecordingConfig config;
    config.video_fps = 30;
    config.jpeg_quality = 0.95f;
    config.audio_sample_rate = 48000;
    config.audio_channels = 2;
    config.audio_buffer_seconds = 3;
    config.enable_timestamp_chunks = true;
    config.enable_repeat_frame_marking = true;
    config.enable_debug_output = true;
    return config;
}

ObsStyleMovieWriter::ObsRecordingConfig ObsStyleMovieWriter::get_standard_config() {
    ObsRecordingConfig config;
    config.video_fps = 30;
    config.jpeg_quality = 0.85f;
    config.audio_sample_rate = 48000;
    config.audio_channels = 2;
    config.audio_buffer_seconds = 2;
    config.enable_timestamp_chunks = true;
    config.enable_repeat_frame_marking = true;
    config.enable_debug_output = true;
    return config;
}

ObsStyleMovieWriter::ObsRecordingConfig ObsStyleMovieWriter::get_performance_config() {
    ObsRecordingConfig config;
    config.video_fps = 30;
    config.jpeg_quality = 0.75f;
    config.audio_sample_rate = 44100;
    config.audio_channels = 2;
    config.audio_buffer_seconds = 1;
    config.enable_timestamp_chunks = false;
    config.enable_repeat_frame_marking = true;
    config.enable_debug_output = false;
    return config;
}

// Statistics and debug features
ObsStyleMovieWriter::CombinedStats ObsStyleMovieWriter::get_combined_statistics() const {
    CombinedStats stats;
    
    if (video_recorder) {
        stats.video_stats = video_recorder->get_statistics();
    }
    
    if (audio_recorder) {
        stats.audio_stats = audio_recorder->get_statistics();
    }
    
    stats.game_frames_added = frames_added_count;
    stats.total_recording_duration_us = OS::get_singleton()->get_ticks_usec() - recording_start_time;
    
    if (video_recorder) {
        stats.overall_repeat_frame_ratio = video_recorder->get_repeat_frame_ratio();
    }
    
    return stats;
}

String ObsStyleMovieWriter::get_comprehensive_debug_info() const {
    String info;
    info += "=== ObsStyleMovieWriter Comprehensive Debug Info ===\n";
    info += String("Current state: ") + get_state_name() + "\n";
    info += String("Output file: ") + output_file_path + "\n";
    info += String("Game frame sequence: ") + String::num_int64(game_frame_sequence) + "\n";
    info += String("Game frames added: ") + String::num_int64(frames_added_count) + "\n";
    
    if (recording_start_time > 0) {
        uint64_t duration = OS::get_singleton()->get_ticks_usec() - recording_start_time;
        info += String("Recording duration: ") + String::num_real(duration / 1000000.0) + " seconds\n";
    }
    
    info += "\n--- Recording Configuration ---\n";
    info += String("Video FPS: ") + String::num_int64(obs_config.video_fps) + "\n";
    info += String("Video resolution: ") + String::num_int64(obs_config.video_width) + "x" + String::num_int64(obs_config.video_height) + "\n";
    info += String("JPEG quality: ") + String::num_real(obs_config.jpeg_quality) + "\n";
    info += String("Audio sample rate: ") + String::num_int64(obs_config.audio_sample_rate) + "Hz\n";
    info += String("Audio channels: ") + String::num_int64(obs_config.audio_channels) + "\n";
    
    if (video_recorder) {
        info += "\n--- Video Recorder Info ---\n";
        info += video_recorder->get_debug_info() + "\n";
    }
    
    if (audio_recorder) {
        info += "\n--- Audio Recorder Info ---\n";
        info += audio_recorder->get_debug_info() + "\n";
    }
    
    if (frame_buffer) {
        info += "\n--- Frame Buffer Info ---\n";
        info += String("Total updates: ") + String::num_int64(frame_buffer->get_total_updates()) + "\n";
        info += String("Buffer switches: ") + String::num_int64(frame_buffer->get_buffer_switches()) + "\n";
        info += String("Last sequence: ") + String::num_int64(frame_buffer->get_last_sequence()) + "\n";
    }
    
    info += "\n=========================================\n";
    
    return info;
}

void ObsStyleMovieWriter::print_recording_summary() const {
    CombinedStats stats = get_combined_statistics();
    
    print_line("=== OBS-style Recording Summary ===");
    print_line(String("Recording duration: ") + String::num_real(stats.total_recording_duration_us / 1000000.0) + " seconds");
    print_line(String("Game frames: ") + String::num_int64(stats.game_frames_added));
    print_line(String("Recorded video frames: ") + String::num_int64(stats.video_stats.total_recorded_frames));
    print_line(String("New frames: ") + String::num_int64(stats.video_stats.new_frames_count));
    print_line(String("Repeated frames: ") + String::num_int64(stats.video_stats.repeated_frames_count));
    print_line(String("Repeated frame ratio: ") + String::num_real(stats.overall_repeat_frame_ratio * 100.0f) + "%");
    print_line(String("Audio chunks: ") + String::num_int64(stats.audio_stats.total_chunks_recorded));
    print_line(String("Audio samples: ") + String::num_int64(stats.audio_stats.total_samples_recorded));
    print_line(String("Audio buffer overruns: ") + String::num_int64(stats.audio_stats.buffer_overruns));
    print_line(String("Audio buffer underruns: ") + String::num_int64(stats.audio_stats.buffer_underruns));
    
    if (avi_writer) {
        print_line(String("AVI file video frames: ") + String::num_int64(avi_writer->get_video_frame_count()));
        print_line(String("AVI file audio chunks: ") + String::num_int64(avi_writer->get_audio_chunk_count()));
    }
    
    print_line("==================");
}

void ObsStyleMovieWriter::set_recording_config(const ObsRecordingConfig &p_config) {
    if (current_state == STATE_RECORDING) {
        ERR_PRINT("ObsStyleMovieWriter: Cannot change configuration while recording");
        return;
    }
    
    obs_config = p_config;
    
    if (obs_config.enable_debug_output) {
        print_line("ObsStyleMovieWriter: Configuration updated");
    }
}

Error ObsStyleMovieWriter::pause_recording() {
    ERR_PRINT("ObsStyleMovieWriter: Pause functionality not yet implemented");
    return ERR_UNAVAILABLE;
}

Error ObsStyleMovieWriter::resume_recording() {
    ERR_PRINT("ObsStyleMovieWriter: Resume functionality not yet implemented");
    return ERR_UNAVAILABLE;
}

bool ObsStyleMovieWriter::is_paused() const {
    return false; // Pause functionality not yet implemented
}

// ========== Combined Recording Mode Implementation ==========

Error ObsStyleMovieWriter::setup_combined_recording() {
	if (obs_config.enable_debug_output) {
		print_line("ObsStyleMovieWriter: Setting up combined recording mode (video+audio merged to single file)");
	}
	
	// Create double buffer (for video frames only)
	frame_buffer = new ThreadSafeFrameBuffer();
	if (!frame_buffer) {
		ERR_PRINT("ObsStyleMovieWriter: Failed to create frame buffer");
		return ERR_OUT_OF_MEMORY;
	}
	
	// Create enhanced AVI writer
	avi_writer = new EnhancedAviWriter();
	if (!avi_writer) {
		ERR_PRINT("ObsStyleMovieWriter: Failed to create EnhancedAviWriter");
		return ERR_OUT_OF_MEMORY;
	}
	
	// Initialize AVI writer (output file for combined mode)
	String combined_file_path = output_file_path + "_combined.avi";
	Error avi_error = avi_writer->open(
		combined_file_path,
		obs_config.video_width,
		obs_config.video_height,
		obs_config.video_fps,
		obs_config.audio_sample_rate,
		obs_config.audio_channels,
		obs_config.jpeg_quality
	);
	
	if (avi_error != OK) {
		ERR_PRINT("ObsStyleMovieWriter: Failed to initialize combined AVI file");
		return avi_error;
	}
	
	// Initialize audio buffer
	pending_audio_samples.clear();
	last_video_frame_time = 0;
	last_audio_chunk_time = 0;
	combined_recording_active = false;
	
	if (obs_config.enable_debug_output) {
		print_line(String("Combined recording file: ") + combined_file_path);
		print_line("Frame buffer and AVI writer initialized successfully");
	}
	
	update_recording_state(STATE_INITIALIZED);
	return OK;
}

void ObsStyleMovieWriter::cleanup_combined_recording() {
	// Stop combined recording thread
	if (combined_recording_thread && combined_recording_active) {
		combined_recording_active = false;
		combined_recording_thread->wait_to_finish();
		memdelete(combined_recording_thread);
		combined_recording_thread = nullptr;
	}
	
	// Clean up audio buffer
	{
		MutexLock lock(audio_buffer_mutex);
		pending_audio_samples.clear();
	}
	
	if (obs_config.enable_debug_output) {
		print_line("Combined recording thread stopped, buffers cleared");
	}
}

void ObsStyleMovieWriter::combined_recording_thread_function(void *p_userdata) {
    ObsStyleMovieWriter *writer = static_cast<ObsStyleMovieWriter *>(p_userdata);
    writer->combined_recording_loop();
}

void ObsStyleMovieWriter::combined_recording_loop() {
	if (obs_config.enable_debug_output) {
		print_line("Combined recording thread started");
	}
	
	uint64_t frame_interval_us = 1000000 / obs_config.video_fps; // microseconds
	uint64_t next_frame_time = OS::get_singleton()->get_ticks_usec();
	
	uint32_t recorded_frames = 0;
	uint64_t loop_start_time = OS::get_singleton()->get_ticks_usec();
	
	while (combined_recording_active) {
		uint64_t current_time = OS::get_singleton()->get_ticks_usec();
		
		// Check if it's time to record the next frame
		if (current_time >= next_frame_time) {
			Error write_error = write_combined_frame_and_audio();
			if (write_error != OK) {
				ERR_PRINT("Combined frame recording write failed");
				break;
			}
			
			recorded_frames++;
			next_frame_time += frame_interval_us;
			
			// Debug output
			if (recorded_frames % 300 == 0 && obs_config.enable_debug_output) { // Output every 10 seconds
				uint64_t elapsed_us = current_time - loop_start_time;
				float elapsed_sec = elapsed_us / 1000000.0f;
				float actual_fps = recorded_frames / elapsed_sec;
				print_line(String("Combined recording progress: ") + String::num_int64(recorded_frames) + 
						  " frames, actual FPS: " + String::num(actual_fps, 1));
			}
		}
		
		// Precise sleep time control
		uint64_t sleep_time_us = next_frame_time - current_time;
		if (sleep_time_us > 0 && sleep_time_us < frame_interval_us) {
			OS::get_singleton()->delay_usec(MIN(sleep_time_us, 5000)); // Sleep for at most 5ms
		} else {
			// Avoid high CPU usage
			OS::get_singleton()->delay_usec(1000); // 1ms
		}
	}
	
	if (obs_config.enable_debug_output) {
		uint64_t total_elapsed_us = OS::get_singleton()->get_ticks_usec() - loop_start_time;
		float total_elapsed_sec = total_elapsed_us / 1000000.0f;
		float average_fps = recorded_frames / total_elapsed_sec;
		print_line(String("Combined recording thread ended: total frames ") + String::num_int64(recorded_frames) + 
				  ", average FPS: " + String::num(average_fps, 2));
	}
}

Error ObsStyleMovieWriter::write_combined_frame_and_audio() {
	if (!avi_writer || !frame_buffer) {
		return ERR_UNCONFIGURED;
	}
	
	uint64_t current_time = OS::get_singleton()->get_ticks_usec();
	
	// 1. Get current video frame
#ifdef WEB_ENABLED
	// On web, get frame data directly from RenderingServer
	RID main_vp_rid = RenderingServer::get_singleton()->viewport_find_from_screen_attachment(DisplayServer::MAIN_WINDOW_ID);
	RID main_vp_texture = RenderingServer::get_singleton()->viewport_get_texture(main_vp_rid);
	Ref<Image> vp_tex = RenderingServer::get_singleton()->texture_2d_get(main_vp_texture);
	if (RenderingServer::get_singleton()->viewport_is_using_hdr_2d(main_vp_rid)) {
		vp_tex->convert(Image::FORMAT_RGBA8);
		vp_tex->linear_to_srgb();
	}
	
	if (vp_tex.is_null()) {
		// No video frame, skip this recording
		return OK;
	}
	
	// Create frame data structure (no need for complex logic of ThreadSafeFrameBuffer on web)
	ThreadSafeFrameBuffer::FrameData frame_data;
	frame_data.image = vp_tex;
	frame_data.game_timestamp = current_time;
	frame_data.frame_sequence = frames_added_count;
	frame_data.is_new_frame = true;
#else
	// Use ThreadSafeFrameBuffer on PC
	ThreadSafeFrameBuffer::FrameData frame_data = frame_buffer->get_current_frame();
	
	if (frame_data.image.is_null()) {
		// No video frame, skip this recording
		return OK;
	}
#endif
	
	// 2. Prepare video frame flags
	uint8_t frame_flags = EnhancedAviWriter::FRAME_FLAG_NEW;
	if (!frame_data.is_new_frame && last_video_frame_time > 0) {
		frame_flags = EnhancedAviWriter::FRAME_FLAG_REPEATED;
	}
	
	// 3. Write video frame
	Error video_error = avi_writer->write_video_frame(frame_data.image, current_time, frame_data.game_timestamp, frame_data.frame_sequence, frame_flags);
	if (video_error != OK) {
		ERR_PRINT("Failed to write video frame");
		return video_error;
	}
	
	last_video_frame_time = current_time;
	
	// 4. Process audio data
	Vector<int32_t> audio_to_write;
	{
		MutexLock lock(audio_buffer_mutex);
		
		// Calculate the number of audio samples to write (synchronized with video frame rate)
		uint32_t samples_per_frame = obs_config.audio_sample_rate / obs_config.video_fps;
		uint32_t samples_needed = samples_per_frame * obs_config.audio_channels;
		
		if (pending_audio_samples.size() >= samples_needed) {
			// Enough audio data
			audio_to_write.resize(samples_needed);
			int32_t *write_ptr = audio_to_write.ptrw();
			for (uint32_t i = 0; i < samples_needed; i++) {
				write_ptr[i] = pending_audio_samples[i];
			}
			
			// Remove used samples from the buffer
			for (uint32_t i = 0; i < samples_needed; i++) {
				pending_audio_samples.remove_at(0);
			}
		} else if (!pending_audio_samples.is_empty()) {
			// Not enough audio data, pad with existing data and silence
			audio_to_write.resize(samples_needed);
			
			// Copy existing data
			uint32_t available_samples = pending_audio_samples.size();
			int32_t *write_ptr = audio_to_write.ptrw();
			for (uint32_t i = 0; i < available_samples; i++) {
				write_ptr[i] = pending_audio_samples[i];
			}
			
			// Fill the rest with silence
			for (uint32_t i = available_samples; i < samples_needed; i++) {
				write_ptr[i] = 0;
			}
			
			pending_audio_samples.clear();
		} else {
			// No audio data, write silence
			audio_to_write.resize(samples_needed);
			int32_t *write_ptr = audio_to_write.ptrw();
			for (uint32_t i = 0; i < samples_needed; i++) {
				write_ptr[i] = 0;
			}
		}
	}
	
	// 5. Write audio data
	if (!audio_to_write.is_empty()) {
		uint32_t audio_frames = audio_to_write.size() / obs_config.audio_channels;
		Error audio_error = avi_writer->write_audio_chunk(audio_to_write.ptr(), audio_frames, current_time);
		if (audio_error != OK) {
			ERR_PRINT("Failed to write audio chunk");
			return audio_error;
		}
		
		last_audio_chunk_time = current_time;
	}
	
	return OK;
} 