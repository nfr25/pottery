#ifndef POTTERY_H
#define POTTERY_H

/*
 * pottery.h — Public API
 * Single include for application code.
 *
 * Stack: Clay (layout) · Cairo (rendering) · Pango (text) · librsvg (SVG)
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* =========================================================================
 * Forward declarations
 * ========================================================================= */

typedef struct PotteryKiln    PotteryKiln;
typedef struct PotteryGlaze   PotteryGlaze;
typedef struct PotteryIcon    PotteryIcon;

/* =========================================================================
 * Events  (produced by the backend, consumed by Pottery)
 * ========================================================================= */

typedef enum {
    POTTERY_EVENT_NONE = 0,
    POTTERY_EVENT_QUIT,
    POTTERY_EVENT_RESIZE,
    POTTERY_EVENT_MOUSE_MOVE,
    POTTERY_EVENT_MOUSE_DOWN,
    POTTERY_EVENT_MOUSE_UP,
    POTTERY_EVENT_MOUSE_WHEEL,
    POTTERY_EVENT_KEY_DOWN,
    POTTERY_EVENT_KEY_UP,
    POTTERY_EVENT_CHAR,         /* Unicode codepoint for text input */
} PotteryEventType;

/* Keycodes (subset — backend maps native keys to these) */
typedef enum {
    POTTERY_KEY_UNKNOWN = 0,
    POTTERY_KEY_LEFT, POTTERY_KEY_RIGHT,
    POTTERY_KEY_UP,   POTTERY_KEY_DOWN,
    POTTERY_KEY_HOME, POTTERY_KEY_END,
    POTTERY_KEY_BACKSPACE, POTTERY_KEY_DELETE,
    POTTERY_KEY_RETURN, POTTERY_KEY_ESCAPE, POTTERY_KEY_TAB,
    POTTERY_KEY_A, POTTERY_KEY_C, POTTERY_KEY_V, POTTERY_KEY_X,
    POTTERY_KEY_Z, POTTERY_KEY_Y,
} PotteryKey;

/* Modifier flags */
#define POTTERY_MOD_SHIFT   (1u << 0)
#define POTTERY_MOD_CTRL    (1u << 1)
#define POTTERY_MOD_ALT     (1u << 2)

typedef struct {
    PotteryEventType type;
    uint32_t         mods;      /* POTTERY_MOD_* bitmask */
    union {
        struct { int x, y, button; } mouse;
        struct { int x, y;          } resize;
        struct { float dx, dy;      } wheel;
        struct { PotteryKey key;    } keyboard;
        struct { uint32_t codepoint;} text;
    };
} PotteryEvent;

/* =========================================================================
 * Backend abstraction
 * ========================================================================= */

/*
 * A backend provides a native window and delivers events.
 * Cairo surfaces are created by the backend using the appropriate
 * Cairo surface type (cairo_win32_surface, cairo_xlib_surface, …).
 * The backend hands a cairo_surface_t* to the kiln; the kiln owns
 * the rendering pipeline from there.
 */
typedef struct {
    /* Create window and initial Cairo surface. Returns false on failure. */
    bool     (*init)        (void *backend_data, int w, int h, const char *title);

    /* Resize the backing Cairo surface. Called on POTTERY_EVENT_RESIZE. */
    bool     (*resize)      (void *backend_data, int w, int h);

    /* Poll one event. Returns false when there are no more events. */
    bool     (*poll_event)  (void *backend_data, PotteryEvent *evt);

    /* Present the rendered cairo_surface_t to the screen. */
    void     (*present)     (void *backend_data);

    /* Clipboard access (UTF-8) */
    char    *(*clipboard_get)   (void *backend_data);              /* caller frees */
    void     (*clipboard_set)   (void *backend_data, const char *utf8);

    /* Cleanup */
    void     (*destroy)     (void *backend_data);

    /* Opaque backend state (e.g. HWND, HDC, …) */
    void    *data;
} PotteryBackend;

/* =========================================================================
 * Glaze  (theme)
 * ========================================================================= */

typedef struct { float r, g, b, a; } PotteryColor;

typedef struct PotteryGlaze {
    /* Surface colors */
    PotteryColor background;    /* window background          */
    PotteryColor surface;       /* widget base color          */
    PotteryColor surface_alt;   /* alternate rows, panels     */
    PotteryColor border;        /* default border             */

    /* Accent / interactive */
    PotteryColor primary;       /* buttons, focus ring, …     */
    PotteryColor primary_text;  /* text on primary background */

    /* State overlays (applied as alpha blend on top of surface) */
    PotteryColor hover;         /* e.g. {1,1,1, 0.06}         */
    PotteryColor pressed;       /* e.g. {0,0,0, 0.12}         */
    PotteryColor focused;       /* e.g. primary at 0.25 alpha */
    PotteryColor disabled;      /* e.g. {0.5,0.5,0.5, 0.38}  */
    PotteryColor selection;     /* text selection highlight    */

    /* Text */
    PotteryColor text_primary;
    PotteryColor text_secondary;
    PotteryColor text_disabled;

    /* Typography (Pango font description strings) */
    char font_body[64];         /* e.g. "Segoe UI 10"         */
    char font_label[64];        /* e.g. "Segoe UI 9"          */
    char font_title[64];        /* e.g. "Segoe UI Bold 11"    */
    char font_mono[64];         /* e.g. "Consolas 10"         */

    /* Metrics (pixels) */
    float border_radius;        /* widget corner radius        */
    float border_width;
    float padding_x;
    float padding_y;
    float spacing;              /* gap between widgets         */
    float icon_size;            /* default icon size           */
    float scrollbar_width;
} PotteryGlaze;

/* Built-in glazes */
PotteryGlaze  pottery_glaze_light (void);
PotteryGlaze  pottery_glaze_dark  (void);

/* =========================================================================
 * Kiln  (main context)
 * ========================================================================= */

typedef struct PotteryKilnDesc {
    const char     *title;
    int             width;
    int             height;
    PotteryBackend *backend;    /* must be non-NULL */
    PotteryGlaze   *glaze;     /* NULL → pottery_glaze_light() */
    int             max_widgets;/* 0 → default 1024            */
    bool            statusbar;  /* true = afficher une statusbar */
} PotteryKilnDesc;

PotteryKiln *pottery_kiln_create  (const PotteryKilnDesc *desc);
void         pottery_kiln_destroy (PotteryKiln *kiln);

/* Set / replace the active glaze at runtime */
void         pottery_kiln_set_glaze (PotteryKiln *kiln, const PotteryGlaze *glaze);

/*
 * Main loop helpers.
 *
 * pottery_kiln_begin_frame() :
 *   - drains the backend event queue
 *   - updates internal input state (hover, focus, …)
 *   - calls Clay_BeginLayout()
 *   Returns false when the application should quit.
 *
 * pottery_kiln_end_frame() :
 *   - calls Clay_EndLayout() → render command array
 *   - walks commands → Cairo draw calls
 *   - calls backend->present()
 */
bool         pottery_kiln_begin_frame (PotteryKiln *kiln);
void         pottery_kiln_end_frame   (PotteryKiln *kiln);

/* Icon management */
PotteryIcon *pottery_kiln_load_icon      (PotteryKiln *kiln, const char *name, const char *svg_path);
PotteryIcon *pottery_kiln_load_icon_data (PotteryKiln *kiln, const char *name,
                                          const char *svg_data, size_t len);
PotteryIcon *pottery_kiln_find_icon      (PotteryKiln *kiln, const char *name);

/* =========================================================================
 * Vessel  (layout container — thin wrapper over Clay containers)
 * ========================================================================= */

typedef enum {
    POTTERY_DIRECTION_ROW    = 0,
    POTTERY_DIRECTION_COLUMN = 1,
} PotteryDirection;

typedef enum {
    POTTERY_ALIGN_START  = 0,
    POTTERY_ALIGN_CENTER = 1,
    POTTERY_ALIGN_END    = 2,
} PotteryAlign;

typedef enum {
    POTTERY_SIZING_FIT     = 0,  /* shrink-wrap children      */
    POTTERY_SIZING_GROW    = 1,  /* fill available space       */
    POTTERY_SIZING_FIXED   = 2,  /* exact pixel size           */
    POTTERY_SIZING_PERCENT = 3,  /* fraction of parent         */
} PotterySizingType;

typedef struct {
    PotterySizingType type;
    float value;                 /* used for FIXED and PERCENT */
    float min, max;              /* optional clamps (0 = unconstrained) */
} PotterySizing;

#define POTTERY_FIT()          ((PotterySizing){ POTTERY_SIZING_FIT,     0.0f, 0.0f, 0.0f })
#define POTTERY_GROW()         ((PotterySizing){ POTTERY_SIZING_GROW,    0.0f, 0.0f, 0.0f })
#define POTTERY_FIXED(px)      ((PotterySizing){ POTTERY_SIZING_FIXED,   (px), 0.0f, 0.0f })
#define POTTERY_PERCENT(f)     ((PotterySizing){ POTTERY_SIZING_PERCENT, (f),  0.0f, 0.0f })

typedef struct {
    const char      *id;
    PotteryDirection direction;
    PotteryAlign     align_x;
    PotteryAlign     align_y;
    PotterySizing    width;
    PotterySizing    height;
    float            padding_x;
    float            padding_y;
    float            gap;
    bool             scroll_x;
    bool             scroll_y;
    bool             clip;
} PotteryVesselDesc;

/* Usage:
 *   POTTERY_VESSEL(kiln, &desc) {
 *       pottery_mold_button(...);
 *       ...
 *   }
 */
void pottery_vessel_begin (PotteryKiln *kiln, const PotteryVesselDesc *desc);
void pottery_vessel_end   (PotteryKiln *kiln);

#define POTTERY_VESSEL(kiln, desc) \
    for (int _v = (pottery_vessel_begin(kiln, desc), 0); \
         _v == 0; \
         pottery_vessel_end(kiln), _v++)

/* =========================================================================
 * Molds  (widgets)
 * ========================================================================= */

/* --- Shared option structs --- */

typedef struct {
    PotterySizing    width;
    PotterySizing    height;
    bool             disabled;
    PotteryIcon     *icon;      /* optional leading icon */
} PotteryMoldOpts;

/* --- Button --- */

typedef struct {
    PotteryMoldOpts  base;
    bool             default_action; /* draws primary-colored */
} PotteryButtonOpts;

/* Returns true on click */
bool pottery_mold_button (PotteryKiln *kiln, const char *id,
                          const char *label, const PotteryButtonOpts *opts);

/* --- Label --- */

typedef struct {
    PotteryMoldOpts  base;
    bool             wrap;
    PotteryAlign     align;
} PotteryLabelOpts;

void pottery_mold_label (PotteryKiln *kiln, const char *id,
                         const char *text, const PotteryLabelOpts *opts);

/* --- Edit (single line) --- */

typedef struct {
    PotteryMoldOpts  base;
    const char      *placeholder;
    bool             password;      /* mask characters */
    int              max_length;    /* 0 = unlimited   */
} PotteryEditOpts;

/* Returns true when content changed */
bool pottery_mold_edit (PotteryKiln *kiln, const char *id,
                        char *buf, int buf_size,
                        const PotteryEditOpts *opts);

/* --- Combo --- */

typedef struct {
    PotteryMoldOpts  base;
} PotteryComboOpts;

/* Returns true when selection changed */
bool pottery_mold_combo (PotteryKiln *kiln, const char *id,
                         const char **items, int item_count,
                         int *selected, const PotteryComboOpts *opts);

/* =========================================================================
 * PotteryModel — modèle de données abstrait (inspiré Qt MVC)
 *
 * L'application implémente les callbacks nécessaires.
 * Pottery ne touche jamais aux données directement.
 * Le même modèle peut alimenter un listview ET un treeview.
 * ========================================================================= */

typedef struct PotteryModel {
    /* --- Données tabulaires (list + tree) --- */

    /* Nombre de lignes de premier niveau */
    int         (*row_count)   (struct PotteryModel *model);

    /* Nombre de colonnes */
    int         (*col_count)   (struct PotteryModel *model);

    /* Texte d'une cellule */
    const char *(*get_cell)    (struct PotteryModel *model, int row, int col);

    /* En-tête de colonne (NULL = pas d'en-tête) */
    const char *(*get_header)  (struct PotteryModel *model, int col);

    /* Icône optionnelle pour une ligne (NULL = pas d'icône) */
    PotteryIcon *(*get_icon)   (struct PotteryModel *model, int row);

    /* --- Données hiérarchiques (tree uniquement) --- */

    /* Nombre d'enfants d'une ligne (-1 = ligne plate, pas de tree) */
    int         (*child_count) (struct PotteryModel *model, int row);

    /* Index du parent (-1 = racine) */
    int         (*parent_row)  (struct PotteryModel *model, int row);

    /* --- Données applicatives opaques --- */
    void *userdata;
} PotteryModel;

/* Helpers pour créer un modèle simple depuis un tableau 2D de strings */
typedef struct {
    PotteryModel        base;       /* DOIT être le premier champ */
    const char * const *data;       /* tableau[row][col] */
    const char * const *headers;    /* tableau[col], NULL = pas d'en-tête */
    int                 rows;
    int                 cols;
} PotteryTableModel;

/* Initialise un PotteryTableModel depuis des données statiques */
void pottery_table_model_init (PotteryTableModel *m,
                                const char * const *data,
                                const char * const *headers,
                                int rows, int cols);

/* --- List view --- */

typedef struct {
    const char  *label;
    float        width;         /* 0 = auto / GROW */
    PotteryAlign align;
} PotteryListColumn;

typedef struct {
    PotteryMoldOpts    base;
    PotteryModel      *model;       /* source de données               */
    PotteryListColumn *columns;     /* description des colonnes        */
    int                column_count;/* 0 = une colonne auto            */
    bool               show_header; /* afficher la ligne d'en-tête     */
    float              row_height;  /* 0 = auto depuis font metrics    */
} PotteryListOpts;

/* Returns true when selection changed; *selected_row updated */
bool pottery_mold_list (PotteryKiln *kiln, const char *id,
                        int *selected_row, const PotteryListOpts *opts);

/* --- Tree view --- */

typedef struct {
    PotteryMoldOpts  base;
    PotteryModel    *model;       /* source de données (child_count + parent_row requis) */
    float            indent;      /* pixels par niveau, 0 = 16px par défaut             */
    float            row_height;  /* 0 = auto depuis font metrics                       */
} PotteryTreeOpts;

/* Returns true when selection changed; *selected_row = model_row sélectionné */
bool pottery_mold_tree (PotteryKiln *kiln, const char *id,
                        int *selected_row,
                        const PotteryTreeOpts *opts);

/* =========================================================================
 * Statusbar
 * ========================================================================= */

#define POTTERY_STATUSBAR_MAX_SECTIONS 8

/* Set text of a statusbar section (0 = left, ...).
 * width_hint : pixel width of the section, 0 = GROW (fills remaining space).
 * Call each frame or only when content changes — stored in kiln state. */
void pottery_statusbar_set (PotteryKiln *kiln, int section,
                             const char *text, float width_hint);

/* Clear all sections */
void pottery_statusbar_clear (PotteryKiln *kiln);

/* =========================================================================
 * Separator / Spacer utilities
 * ========================================================================= */

void pottery_mold_separator (PotteryKiln *kiln, bool horizontal);
void pottery_mold_spacer    (PotteryKiln *kiln, float size);

#endif /* POTTERY_H */
