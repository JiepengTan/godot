/**************************************************************************/
/*  spx.cpp                                                               */
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

#include "core/extension/spx_util.h"
#include "spx.h"
#include "gdextension_spx_ext.h"
#include "scene/main/node.h"
#include "scene/main/window.h"
#include "scene/main/scene_tree.h"
#include "core/object/class_db.h"
#include "spx_engine.h"
#include "spx_input_proxy.h"
#include "spx_sprite.h"
#include "spx_ui.h"
#include "core/io/dir_access.h"
#include "core/os/thread.h"

//#define MINIZIP_ENABLED
#ifdef MINIZIP_ENABLED
#include "modules/zip/zip_reader.h"
#endif
#include "core/os/thread.h"

// Simple node class for initialization
class SpxEngineNode : public Node {
	GDCLASS(SpxEngineNode, Node);
};

   
#define SPX_ENGINE SpxEngine::get_singleton()

void Spx::register_extension_functions() {
	SpxUtil::register_func = &gdextension_spx_setup_interface;
	SpxUtil::debug_mode = debug_mode;
}

void Spx::set_debug_mode(bool enable) {
	debug_mode = enable;
	SpxUtil::debug_mode = enable;
}

void Spx::register_types() {
	ClassDB::register_class<SpxSprite>();
	ClassDB::register_class<SpxInputProxy>();
}

void Spx::unpack_game_data() {
	if (!project_data_path.is_empty() && !unzip_game_date_on_start) {
#ifdef MINIZIP_ENABLED
		Ref<ZIPReader> zip = memnew(ZIPReader);
		if (zip->open(project_data_path) == OK) {
			String target_dir = project_data_path.get_base_dir();
			DirAccess::make_dir_recursive_absolute(target_dir);
			PackedStringArray zfiles = zip->get_files();
			for (int i = 0; i < zfiles.size(); ++i) {
				String zfile = zfiles[i];
				PackedByteArray pdata = zip->read_file(zfile, false);
				String out_path = target_dir + String("/") + zfile;
				DirAccess::make_dir_recursive_absolute(out_path.get_base_dir());
				Ref<FileAccess> fout = FileAccess::open(out_path, FileAccess::WRITE);
				if (fout.is_valid()) {
					fout->store_buffer(pdata);
				}
			}
			zip->close();
			unzip_game_date_on_start = true;
		} 
#else
		print_line("Minizip is not enabled, project data zip is not supported");
#endif
	}
}

void Spx::on_start(void *p_tree) {
	initialed = true;
	restart_requested = false;  // Initialize restart flag to false
	if (!SpxEngine::has_initialed()) {
		return;
	}
	auto tree = (SceneTree *)p_tree;
	if (tree == nullptr)
		return;
	Window *root = tree->get_root();
	if (root == nullptr) {
		return;
	}

	SpxEngineNode *new_node = memnew(SpxEngineNode);
	new_node->set_name("SpxEngineNode");
	root->add_child(new_node);
	SPX_ENGINE->set_root_node(tree, new_node);
	SPX_ENGINE->on_awake();
}

void Spx::on_fixed_update(double delta) {
	if (!initialed || !SpxEngine::has_initialed()) {
		return;
	}

	SPX_ENGINE->on_fixed_update(delta);
}

void Spx::on_update(double delta) {
	if (!initialed || !SpxEngine::has_initialed()) {
		return;
	}

	// Check if restart was requested from a non-main thread
	if (restart_requested) {
		restart_requested = false;  // Clear the flag first
		print_verbose("Spx::on_update - Executing deferred restart from main thread");
		SPX_ENGINE->restart();
		return;  // Skip the normal update after restart
	}

	SPX_ENGINE->on_update(delta);
}

void Spx::on_destroy() {
	if (!initialed || !SpxEngine::has_initialed()) {
		return;
	}

	print_verbose("Spx::on_destroy");
	SPX_ENGINE->on_destroy();
	initialed = false;
	restart_requested = false;
}

void Spx::reset(int exit_code) {
	if (!initialed || !SpxEngine::has_initialed()) {
		return;
	}

	String msg = vformat("Spx::reset called with exit code: %d", exit_code);
	print_verbose(msg);
	SPX_ENGINE->on_reset();
}

void Spx::restart() {
	if (!initialed || !SpxEngine::has_initialed()) {
		return;
	}
	print_verbose("Spx::restart");

	// Check if we're on the main thread
	if (Thread::is_main_thread()) {
		// If we're on the main thread, restart immediately
		SPX_ENGINE->restart();
	} else {
		// If we're not on the main thread, set the restart flag
		restart_requested = true;
		print_verbose("Spx::restart - Restart requested from non-main thread, will restart in main thread");
	}
}

void Spx::pause() {
	if (!initialed || !SpxEngine::has_initialed()) {
		return;
	}
	SPX_ENGINE->pause();
}

void Spx::resume() {
	if (!initialed || !SpxEngine::has_initialed()) {
		return;
	}
	SPX_ENGINE->resume();
}

bool Spx::is_paused() {
	if (!initialed || !SpxEngine::has_initialed()) {
		return false;
	}
	// Query operations are generally safe from any thread
	return SPX_ENGINE->is_paused();
}

void Spx::next_frame() {
	if (!initialed || !SpxEngine::has_initialed()) {
		return;
	}
	SPX_ENGINE->next_frame();
}
