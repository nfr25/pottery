#ifndef POTTERY_INTERNAL_H
#define POTTERY_INTERNAL_H

/*
 * pottery_internal.h — Private structures, not exposed to application code.
 *
 * Included only by pottery_*.c translation units.
 */

#include "pottery.h"

#include <cairo.h>
#include <pango/pangocairo.h>
#include <librsvg/rsvg.h>

/* Helper : Clay_GetElementId à partir d'une const char* (pas un littéral)
 * CLAY_STRING n'accepte que des littéraux string — POTTERY_ID accepte les variables. */
#define POTTERY_ID(str) Clay_GetElementId((Clay_String){.length=(int)strlen(str),.chars=(str)})

/* Pull in Clay — implementation defined in pottery_kiln.c
 * Pragma GCC system_header supprime les warnings du vendor dans ce contexte. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "../third_party/clay.h"
#pragma GCC diagnostic pop

/* =========================================================================
 * Widget state IDs
 * =========================================================================
 *
 * Each widget is identified by a stable uint64_t hash of the string id
 * passed by the application.  The state map is a flat open-addressing
 * hash table — no malloc per entry, fixed capacity set at kiln creation.
 */

typedef enum {
    POTTERY_WIDGET_NONE   = 0,
    POTTERY_WIDGET_BUTTON,
    POTTERY_WIDGET_LABEL,
    POTTERY_WIDGET_EDIT,
    POTTERY_WIDGET_COMBO,
    POTTERY_WIDGET_LIST,
    POTTERY_WIDGET_TREE,
} PotteryWidgetType;

/* ---- Per-widget state payloads ---- */

typedef struct {
    /* nothing beyond common state for now */
    int _pad;
} PotteryButtonState;

typedef struct {
    /* stb_textedit state + text buffer reference */
    void   *stb_state;      /* opaque: STB_TexteditState, allocated in arena */
    char   *buf;            /* points to application buffer (not owned)      */
    int     buf_size;
    int     scroll_offset;  /* horizontal scroll in pixels                   */
} PotteryEditState;

typedef struct {
    bool  open;             /* popup currently visible */
    int   hovered_item;
} PotteryComboState;

typedef struct {
    float scroll_y;         /* current vertical scroll position */
    int   anchor_row;       /* first visible row (for virtualisation) */
} PotteryListState;

typedef struct {
    float              scroll_y;
    /* expand/collapse bitmask — up to 64 top-level nodes inline,
       heap-allocated beyond that (future) */
    uint64_t           expanded;
} PotteryTreeState;

/* ---- Common widget state header ---- */

typedef struct {
    uint64_t         id;            /* hash of string id                    */
    PotteryWidgetType type;
    bool             hovered;
    bool             pressed;
    bool             focused;
    bool             alive;         /* touched this frame? (for GC)         */

    union {
        PotteryButtonState button;
        PotteryEditState   edit;
        PotteryComboState  combo;
        PotteryListState   list;
        PotteryTreeState   tree;
    };
} PotteryWidgetState;

/* =========================================================================
 * State map  (open-addressing hash table, power-of-2 capacity)
 * ========================================================================= */

typedef struct {
    PotteryWidgetState *entries;    /* flat array, size = capacity            */
    int                 capacity;   /* must be power of 2                     */
    int                 count;
} PotteryStateMap;

bool               pottery_state_map_init    (PotteryStateMap *map, int capacity);
void               pottery_state_map_destroy (PotteryStateMap *map);
PotteryWidgetState *pottery_state_map_get    (PotteryStateMap *map, uint64_t id);
PotteryWidgetState *pottery_state_map_get_or_create (PotteryStateMap *map,
                                                     uint64_t id,
                                                     PotteryWidgetType type);
/* Remove states not touched this frame */
void               pottery_state_map_gc      (PotteryStateMap *map);

uint64_t           pottery_hash_string       (const char *s);

/* =========================================================================
 * Text / Pango subsystem
 * ========================================================================= */

typedef struct {
    PangoContext   *context;
    /* LRU cache of PangoLayout* keyed by (text_ptr, font_desc, width_hint).
     * Simple fixed-size ring buffer — Clay calls measure_text very frequently.
     */
    struct {
        uint64_t        key;
        PangoLayout    *layout;
        int             pixel_width;
        int             pixel_height;
    } cache[64];
    int cache_head;

    /* Font descriptions resolved from glaze */
    PangoFontDescription *font_body;
    PangoFontDescription *font_label;
    PangoFontDescription *font_title;
    PangoFontDescription *font_mono;
} PotteryText;

bool pottery_text_init    (PotteryText *text, cairo_t *cr,
                           const PotteryGlaze *glaze);
void pottery_text_destroy (PotteryText *text);
void pottery_text_update_fonts (PotteryText *text, const PotteryGlaze *glaze);

/* Used as Clay_SetMeasureTextFunction callback */
Clay_Dimensions pottery_text_measure (Clay_StringSlice text,
                                      Clay_TextElementConfig *config,
                                      void *userdata);  /* PotteryText* */

/* Draw text at current Cairo position using Pango */
void pottery_text_draw (cairo_t *cr, PotteryText *text,
                        const char *str, int len,
                        PangoFontDescription *font,
                        const PotteryColor *color);

/* X pixel offset of cursor at byte_index in str */
float pottery_text_cursor_x (PotteryText *text,
                              const char *str, int len,
                              int byte_index,
                              PangoFontDescription *font);

/* Byte index in str closest to pixel x px */
int   pottery_text_index_at_x (PotteryText *text,
                                const char *str, int len,
                                float px,
                                PangoFontDescription *font);

/* =========================================================================
 * SVG / Icon subsystem
 * ========================================================================= */

#define POTTERY_ICON_CACHE_MAX 64

struct PotteryIcon {
    char         name[64];
    RsvgHandle  *handle;
};

typedef struct {
    PotteryIcon icons[POTTERY_ICON_CACHE_MAX];
    int         count;
} PotteryIconCache;

bool pottery_icon_cache_init    (PotteryIconCache *cache);
void pottery_icon_cache_destroy (PotteryIconCache *cache);

PotteryIcon *pottery_icon_cache_load (PotteryIconCache *cache,
                                      const char *name,
                                      const char *svg_path);
PotteryIcon *pottery_icon_cache_load_data (PotteryIconCache *cache,
                                           const char *name,
                                           const char *data, size_t len);
PotteryIcon *pottery_icon_cache_find (PotteryIconCache *cache,
                                      const char *name);

/* Draw icon centered in (x, y, w, h) */
void pottery_icon_draw (cairo_t *cr, PotteryIcon *icon,
                        float x, float y, float size,
                        const PotteryColor *tint);

/* =========================================================================
 * Input state  (updated each frame from backend events)
 * ========================================================================= */

typedef struct {
    int      mouse_x, mouse_y;
    bool     mouse_down[3];     /* left=0 middle=1 right=2 */
    bool     mouse_clicked[3];  /* true for exactly one frame after release */
    float    wheel_dx, wheel_dy;

    uint64_t focused_id;        /* currently focused widget (0 = none)      */
    uint64_t hot_id;            /* hovered widget                           */

    uint32_t mods;              /* POTTERY_MOD_* current modifier state      */

    /* Queued character input — consumed by pottery_mold_edit */
    uint32_t char_queue[16];
    int      char_count;

    /* Queued key events */
    struct { PotteryKey key; uint32_t mods; } key_queue[16];
    int      key_count;
} PotteryInput;

void pottery_input_reset_frame (PotteryInput *input);
void pottery_input_push_event  (PotteryInput *input, const PotteryEvent *evt);

/* =========================================================================
 * Renderer
 * ========================================================================= */

typedef struct {
    cairo_t      *cr;
    PotteryText  *text;
    PotteryGlaze *glaze;
    PotteryInput *input;
    PotteryStateMap *state_map;
} PotteryRenderer;

/* Walk Clay_RenderCommandArray and emit Cairo draw calls */
void pottery_renderer_fire (PotteryRenderer *rend,
                             Clay_RenderCommandArray commands);

/* =========================================================================
 * Custom render command payload
 *
 * Passed via Clay_ElementDeclaration.userData (Clay >= 0.13).
 * The renderer retrieves it from cmd->renderData.custom.customData.
 * ========================================================================= */

typedef enum {
    POTTERY_CUSTOM_BUTTON,
    POTTERY_CUSTOM_EDIT,
    POTTERY_CUSTOM_COMBO_BUTTON,  /* the "header" part of a combo */
    POTTERY_CUSTOM_COMBO_POPUP,
    POTTERY_CUSTOM_LIST_ROW,
    POTTERY_CUSTOM_TREE_ROW,
    POTTERY_CUSTOM_ICON,
    POTTERY_CUSTOM_SCROLLBAR,
} PotteryCustomType;

typedef struct {
    PotteryCustomType  type;
    PotteryWidgetState *state;
    const PotteryGlaze *glaze;
    union {
        struct {
            const char  *label;
            PotteryIcon *icon;
            bool         default_action;
        } button;
        struct {
            PotteryEditState *state;
            const char       *placeholder;
            bool              password;
        } edit;
        struct {
            const char **items;
            int          count;
            int          selected;
        } combo;
        struct {
            const char *text;
            bool        selected;
            int         col;
        } list_row;
        struct {
            const char  *label;
            PotteryIcon *icon;
            int          level;
            bool         has_children;
            bool         expanded;
            bool         selected;
        } tree_row;
        struct {
            PotteryIcon *icon;
            float        size;
        } icon;
    };
} PotteryCustomPayload;

/* =========================================================================
 * Custom payload pool  (arena par frame)
 *
 * Les payloads doivent survivre jusqu'à pottery_renderer_fire().
 * On alloue depuis un pool réinitialisé à chaque begin_frame.
 * ========================================================================= */

#define POTTERY_PAYLOAD_POOL_MAX 256

typedef struct {
    PotteryCustomPayload slots[POTTERY_PAYLOAD_POOL_MAX];
    int count;
} PotteryPayloadPool;

static inline PotteryCustomPayload *pottery_payload_alloc(PotteryPayloadPool *pool) {
    if (pool->count >= POTTERY_PAYLOAD_POOL_MAX) return NULL;
    return &pool->slots[pool->count++];
}

/* =========================================================================
 * PotteryKiln  (full private definition)
 * ========================================================================= */

struct PotteryKiln {
    /* Backend */
    PotteryBackend  *backend;

    /* Cairo */
    cairo_surface_t *surface;
    cairo_t         *cr;

    /* Subsystems */
    PotteryText      text;
    PotteryIconCache icons;
    PotteryStateMap  state_map;
    PotteryInput     input;

    /* Clay memory arena */
    void            *clay_memory;
    size_t           clay_memory_size;

    /* Active glaze */
    PotteryGlaze     glaze;

    /* Payload pool (réinitialisé chaque frame) */
    PotteryPayloadPool payload_pool;

    /* Frame state */
    int              width, height;
    bool             quit_requested;
    uint64_t         frame_number;

    /* Popup layer stack (for combo dropdowns etc.) */
    PotteryCustomPayload *popup_stack[8];
    int                   popup_count;
};

#endif /* POTTERY_INTERNAL_H */
