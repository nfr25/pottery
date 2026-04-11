# Pottery — User Guide

## Getting Started

### Dependencies (MSYS2 / MinGW-w64)

```bash
pacman -S mingw-w64-x86_64-cairo
pacman -S mingw-w64-x86_64-pango
pacman -S mingw-w64-x86_64-librsvg
```

Clay and stb_textedit are vendored in `third_party/` — no installation needed.

### Minimal Application

```c
#include "pottery.h"

PotteryBackend *pottery_backend_win32_create(void);

int main(void) {
    PotteryBackend *backend = pottery_backend_win32_create();
    PotteryGlaze    glaze   = pottery_glaze_light();  /* or pottery_glaze_dark() */

    PotteryKiln *kiln = pottery_kiln_create(&(PotteryKilnDesc){
        .title   = "My App",
        .width   = 800,
        .height  = 600,
        .backend = backend,
        .glaze   = &glaze,
    });

    while (pottery_kiln_begin_frame(kiln)) {
        /* declare your UI here */
        pottery_kiln_end_frame(kiln);
    }

    pottery_kiln_destroy(kiln);
    return 0;
}
```

---

## Layout — Vessels

Vessels are layout containers backed by Clay's flexbox engine.
Because C macros cannot contain multi-line compound literals with commas,
always declare vessel descriptors as local variables first.

```c
PotteryVesselDesc root = {
    .id        = "root",           /* unique string ID */
    .direction = POTTERY_DIRECTION_COLUMN,  /* or ROW */
    .width     = POTTERY_GROW(),   /* fill available width */
    .height    = POTTERY_GROW(),   /* fill available height */
    .padding_x = 12,               /* horizontal padding in px */
    .padding_y = 12,               /* vertical padding in px */
    .gap       = 8,                /* gap between children in px */
};

POTTERY_VESSEL(kiln, &root) {
    /* child widgets go here */
}
```

### Sizing Options

| Macro | Meaning |
|-------|---------|
| `POTTERY_GROW()` | Fill all remaining space |
| `POTTERY_FIT()` | Shrink-wrap content |
| `POTTERY_FIXED(px)` | Exact pixel size |
| `POTTERY_PERCENT(f)` | Fraction of parent (0.0–1.0) |

### Nesting Vessels

```c
PotteryVesselDesc col = {
    .id = "left_panel",
    .direction = POTTERY_DIRECTION_COLUMN,
    .width  = POTTERY_FIXED(200),
    .height = POTTERY_GROW(),
    .gap    = 6,
};

PotteryVesselDesc row = {
    .id = "toolbar_row",
    .direction = POTTERY_DIRECTION_ROW,
    .width  = POTTERY_GROW(),
    .height = POTTERY_FIT(),
    .gap    = 4,
};

POTTERY_VESSEL(kiln, &col) {
    POTTERY_VESSEL(kiln, &row) {
        pottery_mold_button(kiln, "btn1", "New",  NULL);
        pottery_mold_button(kiln, "btn2", "Open", NULL);
    }
    pottery_mold_list(kiln, "items", &selected, &list_opts);
}
```

---

## Widgets

### Label

```c
pottery_mold_label(kiln, "my_label", "Hello, World!", NULL);

/* With options */
PotteryLabelOpts opts = {
    .base.width = POTTERY_GROW(),
    .wrap       = true,   /* word-wrap long text */
};
pottery_mold_label(kiln, "desc", "A long description...", &opts);
```

### Button

Returns `true` on click.

```c
if (pottery_mold_button(kiln, "ok_btn", "OK", NULL)) {
    /* handle click */
}

/* Primary (accent color) button */
PotteryButtonOpts opts = {
    .base.width     = POTTERY_FIXED(100),
    .default_action = true,   /* blue background */
};
if (pottery_mold_button(kiln, "save_btn", "Save", &opts)) {
    save_file();
}

/* Button with SVG icon */
PotteryButtonOpts icon_opts = {
    .base.width = POTTERY_FIXED(120),
    .base.icon  = pottery_kiln_find_icon(kiln, "save"),
    .default_action = true,
};
pottery_mold_button(kiln, "save_btn", "Save", &icon_opts);
```

### Spacer and Separator

```c
/* Push subsequent widgets to the right/bottom */
pottery_mold_spacer(kiln, 0);       /* GROW spacer */
pottery_mold_spacer(kiln, 16.0f);   /* fixed 16px gap */

/* Horizontal or vertical line */
pottery_mold_separator(kiln, true);   /* horizontal */
pottery_mold_separator(kiln, false);  /* vertical */
```

### Edit (single-line text input)

Returns `true` when content changes.

```c
char name[128] = {0};

if (pottery_mold_edit(kiln, "name_edit", name, sizeof(name), NULL)) {
    printf("Name changed: %s\n", name);
}

/* With placeholder and options */
PotteryEditOpts opts = {
    .base.width  = POTTERY_GROW(),
    .placeholder = "Enter your name...",
    .max_length  = 64,
    .password    = false,
};
pottery_mold_edit(kiln, "name_edit", name, sizeof(name), &opts);
```

Keyboard shortcuts work out of the box:
- Arrow keys, Home, End — cursor movement
- Ctrl+A — select all
- Ctrl+C / Ctrl+X / Ctrl+V — clipboard
- Ctrl+Z / Ctrl+Y — undo / redo

### Combo (dropdown)

Returns `true` when selection changes.

```c
static const char *colors[] = { "Red", "Green", "Blue", "Yellow" };
int selected_color = 0;

if (pottery_mold_combo(kiln, "color_combo",
        colors, 4, &selected_color, NULL)) {
    printf("Selected: %s\n", colors[selected_color]);
}

/* With fixed width */
PotteryComboOpts opts = { .base.width = POTTERY_FIXED(160) };
pottery_mold_combo(kiln, "color_combo", colors, 4, &selected_color, &opts);
```

### List View

The list view uses `PotteryModel` to decouple data from presentation.

#### Simple table — PotteryTableModel

```c
/* Row-major flat array: data[row * cols + col] */
static const char *data[] = {
    "alice.c",   "Core",  "Main module",
    "bob.c",     "Utils", "Helper functions",
    "carol.c",   "UI",    "Interface code",
};
static const char *headers[] = { "File", "Category", "Description" };

static PotteryTableModel model;
static bool init = false;
if (!init) {
    pottery_table_model_init(&model, data, headers, 3, 3);
    init = true;
}

static PotteryListColumn cols[] = {
    { "File",        150.0f, POTTERY_ALIGN_START },
    { "Category",     90.0f, POTTERY_ALIGN_START },
    { "Description",   0.0f, POTTERY_ALIGN_START }, /* 0 = GROW */
};

int selected_row = 0;
PotteryListOpts opts = {
    .base.width    = POTTERY_GROW(),
    .base.height   = POTTERY_GROW(),
    .model         = &model.base,
    .columns       = cols,
    .column_count  = 3,
    .show_header   = true,
};

if (pottery_mold_list(kiln, "file_list", &selected_row, &opts)) {
    printf("Selected row: %d\n", selected_row);
}
```

#### Custom model — implement PotteryModel callbacks

```c
typedef struct {
    PotteryModel base;   /* MUST be first field */
    MyData      *data;
    int          count;
} MyModel;

static int my_row_count(PotteryModel *m) {
    return ((MyModel *)m)->count;
}
static int my_col_count(PotteryModel *m) {
    (void)m; return 2;
}
static const char *my_get_cell(PotteryModel *m, int row, int col) {
    MyModel *mm = (MyModel *)m;
    return col == 0 ? mm->data[row].name : mm->data[row].value;
}
static const char *my_get_header(PotteryModel *m, int col) {
    (void)m;
    return col == 0 ? "Name" : "Value";
}

/* Initialize */
MyModel model = {
    .base = {
        .row_count  = my_row_count,
        .col_count  = my_col_count,
        .get_cell   = my_get_cell,
        .get_header = my_get_header,
    },
    .data  = my_data_array,
    .count = my_data_count,
};
```

### Tree View

The tree view uses the same `PotteryModel` interface, with two additional
callbacks: `child_count` and `parent_row`.

```c
typedef struct { const char *name; int parent; } Node;

static const Node nodes[] = {
    { "src/",       -1 },   /* row 0: root */
    { "backends/",   0 },   /* row 1: child of 0 */
    { "win32.c",     1 },   /* row 2: child of 1 */
    { "molds/",      0 },   /* row 3: child of 0 */
    { "button.c",    3 },   /* row 4: child of 3 */
    { "include/",   -1 },   /* row 5: root */
    { "pottery.h",   5 },   /* row 6: child of 5 */
};

typedef struct { PotteryModel base; } MyTreeModel;

static int tn_row_count  (PotteryModel *m) { (void)m; return 7; }
static int tn_col_count  (PotteryModel *m) { (void)m; return 1; }
static const char *tn_cell(PotteryModel *m, int row, int col) {
    (void)m; (void)col; return nodes[row].name;
}
static int tn_child_count(PotteryModel *m, int row) {
    int n = 0;
    for (int i = 0; i < 7; i++)
        if (nodes[i].parent == row) n++;
    return n;
}
static int tn_parent(PotteryModel *m, int row) {
    (void)m; return nodes[row].parent;
}

static MyTreeModel tree_model = {{
    .row_count   = tn_row_count,
    .col_count   = tn_col_count,
    .get_cell    = tn_cell,
    .child_count = tn_child_count,
    .parent_row  = tn_parent,
}};

int selected_node = 0;
PotteryTreeOpts opts = {
    .base.width  = POTTERY_GROW(),
    .base.height = POTTERY_FIXED(200),
    .model       = &tree_model.base,
    .indent      = 16.0f,
};

if (pottery_mold_tree(kiln, "src_tree", &selected_node, &opts)) {
    printf("Selected: %s\n", nodes[selected_node].name);
}
```

---

## Toolbar

The toolbar is drawn directly with Cairo (not Clay) and lives outside the
scroll area. Activate it after `pottery_kiln_create`:

```c
pottery_toolbar_enable(kiln);

/* Add items */
pottery_toolbar_add_icon(kiln, ID_NEW,  draw_new_icon,  "New",  POTTERY_BTN_NORMAL);
pottery_toolbar_add_icon(kiln, ID_SAVE, draw_save_icon, "Save", POTTERY_BTN_NORMAL);
pottery_toolbar_add_sep(kiln);

/* Toggle button — stays pressed until clicked again */
pottery_toolbar_add_btn(kiln, ID_GRID, "Grid", POTTERY_BTN_TOGGLE);

/* Split button — main click + dropdown arrow */
pottery_toolbar_add_svg(kiln, ID_ZOOM, zoom_icon, "Zoom", POTTERY_BTN_SPLIT);

/* Multi-mode button — cycles through N states on click */
pottery_toolbar_add_icon_multimode(kiln, ID_SNAP, draw_snap_icon,
                                    "Snap mode", 3);  /* 3 modes: off/grid/point */

/* Callback */
pottery_toolbar_set_cb(kiln, on_toolbar_click, &app);
```

### Toolbar callback

```c
static void on_toolbar_click(int id, void *userdata) {
    AppState *app = (AppState *)userdata;
    switch (id) {
        case ID_NEW:  new_document(app);   break;
        case ID_SAVE: save_document(app);  break;
        case ID_GRID: toggle_grid(app);    break;
        /* Split button arrow: id = POTTERY_TB_SPLIT_BASE + original_id */
        case POTTERY_TB_SPLIT_BASE + ID_ZOOM:
            show_zoom_menu(app);
            break;
    }
}
```

### Custom Cairo icon function

```c
/* Signature: void fn(void *cr, float cx, float cy, int mode)  */
/* Cast cr to cairo_t* inside the function.                     */
/* mode = current mode index for multi-mode buttons.            */

static void draw_grid_icon(void *vr, float cx, float cy, int mode) {
    cairo_t *cr = (cairo_t *)vr;
    float alpha = (mode > 0) ? 1.0f : 0.3f;  /* dim when off */

    cairo_set_source_rgba(cr, 0.4, 0.7, 1.0, alpha);
    cairo_set_line_width(cr, 1.2f);
    for (float d = -6; d <= 6; d += 6) {
        cairo_move_to(cr, cx + d, cy - 7);
        cairo_line_to(cr, cx + d, cy + 7);
        cairo_move_to(cr, cx - 7, cy + d);
        cairo_line_to(cr, cx + 7, cy + d);
    }
    cairo_stroke(cr);
}
```

### Runtime toolbar state

```c
/* Change pressed state programmatically */
pottery_toolbar_set_pressed(kiln, ID_GRID, true);

/* Enable / disable */
pottery_toolbar_set_enabled(kiln, ID_SAVE, has_unsaved_changes);

/* Read or set multi-mode */
int current_snap = pottery_toolbar_get_mode(kiln, ID_SNAP);
pottery_toolbar_set_mode(kiln, ID_SNAP, 2);
```

---

## Statusbar

```c
pottery_statusbar_enable(kiln);

/* In the frame loop — update as needed */
pottery_statusbar_set(kiln, 0, "Ready",          0);      /* section 0: GROW */
pottery_statusbar_set(kiln, 1, "Layer: Default", 160.0f); /* section 1: 160px */
pottery_statusbar_set(kiln, 2, "1024 x 768",     100.0f); /* section 2: 100px */
```

Section width `0` means GROW — it fills the remaining space.
Fixed-width sections are right-aligned by successive declaration order.

---

## Theming — Glaze

```c
/* Built-in themes */
PotteryGlaze light = pottery_glaze_light();
PotteryGlaze dark  = pottery_glaze_dark();

/* Switch at runtime */
pottery_kiln_set_glaze(kiln, &dark);

/* Smooth transition (t: 0.0 = a, 1.0 = b) */
PotteryGlaze mid = pottery_glaze_lerp(&light, &dark, 0.5f);
pottery_kiln_set_glaze(kiln, &mid);

/* Custom glaze */
PotteryGlaze custom = pottery_glaze_light();  /* start from a built-in */
custom.primary   = (PotteryColor){ 0.8f, 0.2f, 0.2f, 1.0f };  /* red accent */
custom.font_body[0] = '\0';
strncpy(custom.font_body, "Consolas 11", sizeof(custom.font_body));
pottery_kiln_set_glaze(kiln, &custom);
```

---

## SVG Icons

```c
/* Load from file */
PotteryIcon *icon = pottery_kiln_load_icon(kiln, "save", "icons/save.svg");

/* Load from embedded data */
static const char svg_data[] =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
    "<path d='M2 8 L6 12 L14 4' stroke='white' stroke-width='2' fill='none'/>"
    "</svg>";
PotteryIcon *check = pottery_kiln_load_icon_data(
    kiln, "check", svg_data, sizeof(svg_data) - 1);

/* Use in a button */
PotteryButtonOpts opts = {
    .base.icon      = check,
    .default_action = true,
};
pottery_mold_button(kiln, "ok_btn", "OK", &opts);
```

---

## Common Patterns

### Conditional UI

```c
POTTERY_VESSEL(kiln, &panel) {
    pottery_mold_label(kiln, "status", status_text, NULL);

    if (is_logged_in) {
        pottery_mold_edit(kiln, "msg", message, sizeof(message), NULL);
        if (pottery_mold_button(kiln, "send", "Send", &primary_opts))
            send_message(message);
    } else {
        if (pottery_mold_button(kiln, "login", "Log in", &primary_opts))
            show_login_dialog();
    }
}
```

### Push widget to the right

```c
PotteryVesselDesc row = { .id="row", .direction=POTTERY_DIRECTION_ROW,
                           .width=POTTERY_GROW(), .gap=6 };
POTTERY_VESSEL(kiln, &row) {
    pottery_mold_button(kiln, "back", "← Back", NULL);
    pottery_mold_spacer(kiln, 0);          /* GROW spacer pushes right */
    pottery_mold_button(kiln, "next", "Next →", &primary_opts);
}
```

### Dynamic list from application data

```c
/* The model callbacks are called every frame — always up to date */
static int my_row_count(PotteryModel *m) {
    return app_get_item_count();   /* reads live data */
}
static const char *my_get_cell(PotteryModel *m, int row, int col) {
    return app_get_item_name(row); /* reads live data */
}
```

---

## Rules and Pitfalls

**① IDs must be unique and stable**
Two widgets sharing the same ID share the same state — guaranteed bug.
An ID that changes between frames means state is lost every frame.

**② `CLAY_STRING` only accepts string literals**
```c
/* ✓ Compile-time literal */
Clay__OpenElementWithId(Clay_GetElementId(CLAY_STRING("my_btn")));

/* ✓ Runtime variable — use POTTERY_ID macro */
Clay__OpenElementWithId(POTTERY_ID(id));

/* ✗ Compile error */
Clay__OpenElementWithId(Clay_GetElementId(CLAY_STRING(id)));
```

**③ Payload pool lifetime = one frame**
Payloads allocated via `pottery_payload_alloc()` are reset at the start
of each frame. Never store a payload pointer across frames.

**④ Compound literals cannot span macro arguments**
```c
/* ✗ Commas inside the literal look like macro arguments */
POTTERY_VESSEL(kiln, &(PotteryVesselDesc){ .id="r", .gap=8 });

/* ✓ Declare as a local variable first */
PotteryVesselDesc desc = { .id = "root", .gap = 8 };
POTTERY_VESSEL(kiln, &desc) { ... }
```

**⑤ The active loop and CPU**
Pottery redraws every frame even when nothing changes. For applications
with heavy rendering (CAD canvas, 3D viewport), keep the canvas as a
separate Win32 child window with its own `WM_PAINT` cycle. Pottery
handles only the surrounding UI panels.

---

*Pottery v0.1 — Clay · Cairo · Pango · librsvg · stb_textedit*
