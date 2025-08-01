# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a customized fork of Godot Engine 4.4 with a custom SPX (Sprite eXtension) module. The project focuses on enhanced sprite-based game development with strong web platform support and runtime video recording capabilities.

## Build Commands

**Primary build system:**

不要尝试自己编译，如果需要编译或执行，通知用户自己手动编译


## Architecture

**Core Languages:** C++, Python (SCons), JavaScript/TypeScript (web), GDScript, GLSL

**Key Directories:**
- `modules/spx/` - Custom SPX module with sprite system, audio, UI, input, physics managers
- `core/` - Core engine functionality
- `scene/` - Scene system and node hierarchy  
- `servers/` - Rendering, audio, physics servers
- `servers/movie_writer/` - Advanced video recording system with OBS-style capabilities
- `platform/` - Platform-specific implementations (macOS, Windows, Linux, Web, iOS, Android)
- `editor/` - Godot editor implementation
- `tests/` - Unit testing framework

**SPX Module Components:**
- Sprite management (`spx_sprite`, `spx_sprite_mgr`)
- Audio system (`spx_audio_mgr`, `spx_audio_bus_pool`)
- Scene management (`spx_scene_mgr`)
- UI system (`spx_ui`, `spx_ui_mgr`)
- Input handling (`spx_input_mgr`, `spx_input_proxy`)
- Resource management (`spx_res_mgr`)
- Physics integration (`spx_physic_mgr`)
- Camera system (`spx_camera_mgr`)
- Platform abstraction (`spx_platform_mgr`)

## Development Patterns

**Build Configuration:**
- Always include `spx=yes` when building to enable the custom SPX module
- Use `dev_build=yes` and `debug_symbols=yes` for development
- Use `tests=yes` when working on testing
- Use `fast_unsafe=yes` for quick incremental builds during development

**Platform Support:**
- Primary focus on macOS, Windows, Linux, and Web platforms
- Web platform uses WebGL/WebAssembly via Emscripten
- Mobile platforms (iOS, Android) are supported but secondary

**Modular Architecture:**
- Each major feature is implemented as a separate module in `modules/`
- Platform-specific code is isolated in `platform/` directory
- Core engine functionality is separated from custom SPX extensions

**Recent Features:**
- Runtime video recorder for web and PC platforms
- Worker mode support for multi-threading
- Decoupled SPX module for independent development

## Video Recording System

**Location:** `servers/movie_writer/`

**Key Components:**
- `ObsStyleMovieWriter` - OBS-style fixed-framerate recording (30fps output regardless of game performance)
- `EnhancedAviWriter` - AVI format with custom timestamp chunks and frame analysis
- `IndependentVideoRecorder` - Multi-threaded video recording with performance tracking
- `ThreadSafeFrameBuffer` - Double-buffered frame management for recording threads
- `IndependentAudioRecorder` - Continuous audio capture independent of game framerate

**Recording Modes:**
- **Offline Rendering**: Frame-by-frame deterministic recording for highest quality
- **Real-time Recording**: OBS-style recording that captures actual gameplay performance
- **Web Platform**: MediaRecorder API integration with WebGL/WebAssembly support

**Key Features:**
- Frame analysis (new/repeated/dropped frame detection)
- Custom metadata preservation with timing information
- Cross-platform support (desktop + web)
- Configurable quality presets (high/standard/performance)
- Audio/video synchronization with hybrid audio driver

**Usage Patterns:**
```bash
# Video recording is integrated into the engine runtime
# See VIDEO_RECORDING_ANALYSIS.md for detailed technical documentation
```