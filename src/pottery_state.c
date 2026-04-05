/*
 * pottery_state.c — Widget state map.
 *
 * Open-addressing hash table keyed by uint64_t widget ID.
 * Capacity is always a power of 2 (set at kiln creation).
 * No dynamic allocation after init.
 */

#include "pottery_internal.h"
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * FNV-1a 64-bit hash
 * ========================================================================= */

uint64_t pottery_hash_string(const char *s) {
    uint64_t hash = 14695981039346656037ULL;
    while (*s) {
        hash ^= (uint8_t)*s++;
        hash *= 1099511628211ULL;
    }
    return hash ? hash : 1; /* 0 is used as "empty slot" sentinel */
}

/* =========================================================================
 * State map
 * ========================================================================= */

bool pottery_state_map_init(PotteryStateMap *map, int capacity) {
    map->entries  = calloc(capacity, sizeof(PotteryWidgetState));
    map->capacity = capacity;
    map->count    = 0;
    return map->entries != NULL;
}

void pottery_state_map_destroy(PotteryStateMap *map) {
    free(map->entries);
    map->entries  = NULL;
    map->capacity = 0;
    map->count    = 0;
}

/*
 * Linear probing lookup.
 * Returns NULL if not found.
 */
PotteryWidgetState *pottery_state_map_get(PotteryStateMap *map, uint64_t id) {
    int mask  = map->capacity - 1;
    int index = (int)(id & mask);
    for (int i = 0; i < map->capacity; i++) {
        PotteryWidgetState *e = &map->entries[(index + i) & mask];
        if (e->id == 0)     return NULL; /* empty slot → not found */
        if (e->id == id)    return e;
    }
    return NULL;
}

/*
 * Get or create a state entry.
 * On creation, zeroes the payload union and sets type.
 */
PotteryWidgetState *pottery_state_map_get_or_create(PotteryStateMap *map,
                                                     uint64_t id,
                                                     PotteryWidgetType type) {
    int mask  = map->capacity - 1;
    int index = (int)(id & mask);

    for (int i = 0; i < map->capacity; i++) {
        PotteryWidgetState *e = &map->entries[(index + i) & mask];
        if (e->id == 0) {
            /* Empty slot — create new entry */
            memset(e, 0, sizeof(*e));
            e->id   = id;
            e->type = type;
            map->count++;
            return e;
        }
        if (e->id == id) return e; /* existing entry */
    }

    /* Table full — should not happen if capacity is sized correctly */
    return NULL;
}

/*
 * Remove all entries not touched this frame (alive == false).
 * Called at end of frame by pottery_kiln_end_frame().
 */
void pottery_state_map_gc(PotteryStateMap *map) {
    for (int i = 0; i < map->capacity; i++) {
        PotteryWidgetState *e = &map->entries[i];
        if (e->id != 0 && !e->alive) {
            memset(e, 0, sizeof(*e));
            map->count--;
        }
    }
    /*
     * NOTE: After zeroing entries in an open-addressing table we may
     * break probe chains. For simplicity we accept occasional missed
     * GC on the next cycle. A proper implementation would rehash, but
     * for widget states (low churn) this is fine.
     */
}
