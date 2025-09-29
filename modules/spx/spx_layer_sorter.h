/**************************************************************************/
/*  spx_layer_sorter.h                                                    */
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

#ifndef SPX_LAYER_SORTER_H
#define SPX_LAYER_SORTER_H

#include "spx_sprite.h"
#include <vector>
#include <unordered_set>
#include <algorithm>

class ISortableSprite;

struct SortInfo {
    GdObj id;
    Point2 pos;
    ISortableSprite* sortable;
};

enum class LayerSortMode {
     NONE, 
     VERTICAL 
};

class SpxLayerSorter {
public:
    static SpxLayerSorter& instance() {
        static SpxLayerSorter inst;
        return inst;
    }

    _FORCE_INLINE_ void set_mode(LayerSortMode mode){
        sort_mode = mode;
    }
    _FORCE_INLINE_ void reset(){
        sorted.clear();
        dirty.clear();
    }

    // New interface: accept any sortable sprites
    void update(const Vector<ISortableSprite*>& sortables);

    // Legacy interface: keep compatibility with SpxSprite
    void update(const RBMap<GdObj, SpxSprite*>& id_objects);

private:
    std::vector<SortInfo> sorted;
    std::vector<SortInfo> dirty;
    std::unordered_set<GdObj> dirty_ids;

    static inline LayerSortMode sort_mode = LayerSortMode::NONE;

    static inline float full_sort_ratio = 0.3f;
    static inline bool sprite_cmp(const SortInfo &a, const SortInfo &b){
        if (a.pos.y == b.pos.y){
            return a.pos.x > b.pos.x;
        } 
        return a.pos.y < b.pos.y;
    };

    void _mark_dirty(ISortableSprite* sp);
    void _collect_sprites(const Vector<ISortableSprite*>& sortables);
    void _incremental_sort();
    void _full_sort();
    void _apply_z_index();

private:
    SpxLayerSorter(){
        reset();
    };
    ~SpxLayerSorter() = default;

    SpxLayerSorter(const SpxLayerSorter&) = delete;
    SpxLayerSorter& operator=(const SpxLayerSorter&) = delete;
};

#endif //SPX_LAYER_SORTER_H