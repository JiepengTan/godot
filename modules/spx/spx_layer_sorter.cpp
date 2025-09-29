/**************************************************************************/
/*  spx_layer_sorter.cpp                                                  */
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
#include "spx_layer_sorter.h"

// New interface implementation
void SpxLayerSorter::update(const Vector<ISortableSprite*>& sortables) {
    if(sort_mode == LayerSortMode::NONE) return;

    _collect_sprites(sortables);

    if (dirty.empty()) return;

    float ratio = float(dirty.size()) / float(sorted.size());
    if (ratio > full_sort_ratio) {
        _full_sort();
    } else {
        _incremental_sort();
    }

    _apply_z_index();
}

// Legacy interface: convert RBMap to Vector
void SpxLayerSorter::update(const RBMap<GdObj, SpxSprite*>& id_objects) {
    Vector<ISortableSprite*> sortables;
    for (auto &pair : id_objects) {
        if (pair.value) {
            sortables.push_back(pair.value);
        }
    }
    update(sortables);
}

void SpxLayerSorter::_mark_dirty(ISortableSprite* sp) {
    if (!sp) return;
    GdObj id = sp->get_sort_id();
    if (dirty_ids.find(id) != dirty_ids.end()) return;

    dirty.push_back({id, sp->get_sort_position(), sp});
    dirty_ids.insert(id);
}

void SpxLayerSorter::_collect_sprites(const Vector<ISortableSprite*>& sortables) {
    // Remove invalid sprites
    sorted.erase(
        std::remove_if(sorted.begin(), sorted.end(),
            [](const SortInfo &s) {
                return !s.sortable || !s.sortable->is_node_valid();
            }),
        sorted.end()
    );

    if (sorted.empty()) {
        sorted.reserve(sortables.size());
        for (auto sp : sortables) {
            if (!sp || !sp->is_node_valid()) continue;
            sorted.push_back({sp->get_sort_id(), sp->get_sort_position(), sp});
        }
        std::sort(sorted.begin(), sorted.end(), sprite_cmp);
    } else {
        // Create a set of existing IDs for quick lookup
        std::unordered_set<GdObj> existing_ids;
        for (const auto &s : sorted) {
            existing_ids.insert(s.id);
        }

        for (auto sp : sortables) {
            if (!sp || !sp->is_node_valid()) continue;

            GdObj sp_id = sp->get_sort_id();
            auto sorted_it = std::find_if(sorted.begin(), sorted.end(),
                [sp_id](const SortInfo &s) { return s.id == sp_id; });

            if (sorted_it != sorted.end()) {
                Point2 cur_pos = sp->get_sort_position();
                if (sorted_it->pos != cur_pos) {
                    sorted_it->pos = cur_pos;
                    sorted_it->sortable = sp;
                    _mark_dirty(sp);
                }
            } else {
                sorted.push_back({sp_id, sp->get_sort_position(), sp});
                _mark_dirty(sp);
            }
        }
    }
}

void SpxLayerSorter::_incremental_sort() {
    sorted.erase(std::remove_if(sorted.begin(), sorted.end(),
    [&](const SortInfo &s) {
        return dirty_ids.find(s.id) != dirty_ids.end();
    }), sorted.end());

    for (auto &d : dirty) {
        auto pos = std::lower_bound(sorted.begin(), sorted.end(), d, sprite_cmp);
        sorted.insert(pos, d);
    }

    dirty.clear();
    dirty_ids.clear();
}

void SpxLayerSorter::_full_sort() {
    for (auto &s : sorted) {
        if (s.sortable && s.sortable->is_node_valid()) {
            s.pos = s.sortable->get_sort_position();
        }
    }
    std::sort(sorted.begin(), sorted.end(), sprite_cmp);
    dirty.clear();
    dirty_ids.clear();
}

void SpxLayerSorter::_apply_z_index() {
    int z = 1;
    for (auto &s : sorted) {
        if (s.sortable && s.sortable->is_node_valid() && s.sortable->get_sort_z_index() != z){
            s.sortable->set_sort_z_index(z);
        }
        ++z;
    }
}
