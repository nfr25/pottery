# Pottery — Architecture Guide

## The Technology Stack

```
┌─────────────────────────────────────────────────────┐
│                   Application                       │
│        pottery_mold_button(), pottery_mold_list()   │
├─────────────────────────────────────────────────────┤
│               Pottery  (this project)               │
│    Widgets · State map · Toolbar · Statusbar        │
├──────────────────┬──────────────────────────────────┤
│      Clay        │            Cairo                 │
│   Layout engine  │       2D vector rendering        │
│  (positions,     │  (rectangles, text, SVG, paths)  │
│   sizes)         │                                  │
├──────────────────┤   Pango   (text layout)          │
│                  │   librsvg (SVG icons)            │
├──────────────────┴──────────────────────────────────┤
│           Backend  Win32  (or X11, Cocoa)           │
│          Window · Events · Surface presentation     │
└─────────────────────────────────────────────────────┘
```

Each layer has a single, clear responsibility:

| Layer | Role | What it does NOT do |
|-------|------|---------------------|
| **Clay** | Computes positions and sizes | Drawing |
| **Cairo** | Draws pixels, curves, text | Layout |
| **Pango** | Measures and renders text | Anything else |
| **librsvg** | Renders SVG icons | Anything else |
| **Pottery** | Manages state, orchestrates | Layout computation |
| **Backend** | OS window and events | Rendering |

---

## Immediate Mode vs Retained Mode

Pottery is a **hybrid** — this is the most important thing to understand.

### Clay: pure immediate mode

Clay recomputes the **entire layout from scratch every frame**. If you do not
call `pottery_mold_button(kiln, "my_btn", ...)` in a given frame, that button
simply does not exist that frame. This is the same philosophy as Dear ImGui.

```c
// Frame N: the button exists
pottery_mold_button(kiln, "ok", "OK", NULL);

// Frame N+1: condition is false → button disappears automatically
if (show_ok)
    pottery_mold_button(kiln, "ok", "OK", NULL);
```

**Advantage**: the UI automatically follows application state.
No manual synchronization, no complex callbacks, no "update UI" calls.

### Pottery: retained mode for widget state

Unlike pure ImGui, Pottery maintains **persistent state per widget** in the
`PotteryStateMap` — a hash table indexed by the widget's string ID.

```
PotteryStateMap (open-addressing hash table)
  "name_edit"  → PotteryWidgetState { focused=true, edit.cursor=5, ... }
  "file_list"  → PotteryWidgetState { list.scroll_y=120.0, ... }
  "lang_combo" → PotteryWidgetState { combo.open=false, ... }
```

This state **survives across frames**: text typed into an Edit field, the
scroll position of a List, the selected item in a Tree — all preserved
automatically. The application does not need to store this state itself.

---

## Frame Lifecycle

```
┌─────────────────────────────────────────────────────────────────┐
│  pottery_kiln_begin_frame(kiln)                                 │
│                                                                 │
│  1. pottery_input_reset_frame()                                 │
│     Clear one-shot events from the previous frame:             │
│     mouse_clicked=false, wheel_dy=0, char_queue empty...       │
│                                                                 │
│  2. backend->poll_event()  ← drain the Win32 event queue       │
│     WM_MOUSEMOVE  → input.mouse_x/y                            │
│     WM_LBUTTONUP  → input.mouse_clicked[0] = true              │
│     WM_MOUSEWHEEL → input.wheel_dy                             │
│     WM_CHAR       → input.char_queue[]                         │
│     WM_SIZE       → resize Cairo surface + Clay dimensions     │
│                                                                 │
│  3. pottery_toolbar_handle_event()                              │
│     Toolbar consumes mouse events within its zone              │
│     before they reach Clay widgets                             │
│                                                                 │
│  4. mouse_y -= toolbar_height                                   │
│     Recalibration: Clay works in coordinates relative          │
│     to its zone (below the toolbar), not screen coordinates    │
│                                                                 │
│  5. Clay_SetPointerState()  ← tell Clay where the mouse is     │
│  6. Clay_UpdateScrollContainers()  ← forward mouse wheel       │
│  7. Clay_BeginLayout()  ← open the UI declaration phase        │
└─────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│  Application: build_ui(kiln, &app)                             │
│                                                                 │
│  POTTERY_VESSEL(kiln, &root) {                                  │
│      pottery_mold_label(...)                                    │
│      pottery_mold_edit(...)                                     │
│      if (pottery_mold_button(...)) do_something();              │
│  }                                                              │
│                                                                 │
│  Each mold:                                                     │
│  a) Retrieves or creates its state in the StateMap             │
│  b) Declares a Clay element (CUSTOM, TEXT, or RECTANGLE)       │
│  c) Reads the bounding box from the PREVIOUS frame             │
│  d) Updates its state (hover, pressed, focused...)             │
│  e) Returns a bool if an action occurred (click, change...)    │
└─────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│  pottery_kiln_end_frame(kiln)                                   │
│                                                                 │
│  1. Clay_EndLayout()                                            │
│     → Clay computes ALL positions and sizes                    │
│     → Returns Clay_RenderCommandArray                          │
│                                                                 │
│  2. Clear the Clay zone (between toolbar and statusbar)         │
│     cairo_rectangle() + cairo_fill() with glaze.background     │
│                                                                 │
│  3. cairo_translate(0, toolbar_height)                          │
│     Shift Cairo: Clay coordinate 0,0 = just below toolbar      │
│                                                                 │
│  4. pottery_renderer_fire()  ← walk the Clay render commands   │
│     RECTANGLE → cairo_fill()                                   │
│     TEXT      → pango_cairo_show_layout()                      │
│     BORDER    → cairo_stroke()                                  │
│     CUSTOM    → draw_button() / draw_edit() / draw_list()      │
│     SCISSOR   → cairo_clip() / cairo_restore()                 │
│                                                                 │
│  5. pottery_kiln_render_toolbar()    ← Cairo direct, on top    │
│  6. pottery_kiln_render_statusbar()  ← Cairo direct, on top    │
│                                                                 │
│  7. cairo_surface_flush()  ← flush to the Win32 DC             │
│  8. backend->present()  ← BitBlt to the screen window         │
│                                                                 │
│  9. pottery_state_map_gc()                                      │
│     Remove state for widgets not declared this frame           │
│     (widget disappeared from UI → state freed automatically)   │
└─────────────────────────────────────────────────────────────────┘
```

---

## Anatomy of a Mold

A mold does **two things simultaneously**:
1. **Declares** to Clay what to place and what size it should be
2. **Responds** to events (click, keyboard) on that widget

```c
bool pottery_mold_button(PotteryKiln *kiln, const char *id,
                          const char *label,
                          const PotteryButtonOpts *opts) {

    // ① Retrieve or create persistent widget state
    uint64_t wid = pottery_hash_string(id);
    PotteryWidgetState *state = pottery_state_map_get_or_create(
        &kiln->state_map, wid, POTTERY_WIDGET_BUTTON);
    state->alive = true;  // ← mark "seen this frame", prevents GC

    // ② Allocate a payload in the frame pool
    //    (automatically freed at the next begin_frame)
    PotteryCustomPayload *payload = pottery_payload_alloc(&kiln->payload_pool);
    payload->type         = POTTERY_CUSTOM_BUTTON;
    payload->button.label = label;
    payload->state        = state;  // renderer needs hover/pressed state

    // ③ Declare the element to Clay
    Clay_ElementDeclaration decl = {0};
    decl.layout.sizing.height = CLAY_SIZING_FIXED(28);
    decl.layout.sizing.width  = CLAY_SIZING_FIXED(80);
    decl.custom.customData    = payload;  // forwarded to renderer

    Clay__OpenElementWithId(POTTERY_ID(id));
    Clay__ConfigureOpenElement(decl);
    Clay__CloseElement();

    // ④ Read bounding box computed in the PREVIOUS frame
    Clay_ElementData data = Clay_GetElementData(POTTERY_ID(id));
    bool clicked = false;

    if (data.found) {
        bool hovered = point_in_bb(data.boundingBox,
                                    kiln->input.mouse_x,
                                    kiln->input.mouse_y);
        state->hovered = hovered;

        // ⑤ A click = mouse released while hovering
        if (hovered && kiln->input.mouse_clicked[0]) {
            clicked = true;
            kiln->input.focused_id = wid;
        }
        state->focused = (kiln->input.focused_id == wid);
    }

    return clicked;  // ← application acts on true
}
```

### The one-frame latency

Clay computes bounding boxes in `EndLayout` (step 1 of `end_frame`).
Molds read these boxes in the **next frame** via `Clay_GetElementData`.
One frame of latency ≈ 16ms at 60fps — completely imperceptible.
On the very first frame there is no hit-testing; from the second frame
onwards everything works normally.

---

## The CUSTOM Mechanism

For complex widgets, Pottery uses Clay's `CUSTOM` render command type.
Clay does not know how to draw a button — it delegates to the Pottery renderer.

```
Mold                     Clay                      Renderer
──────────────────────────────────────────────────────────────
pottery_mold_button()
  payload = { label, state, glaze }
  decl.custom.customData = payload
  Clay__ConfigureOpenElement(decl)
                     ↓
          [Clay computes position and size]
                     ↓
          Clay_RenderCommand {
            commandType = CUSTOM,
            boundingBox = { x=16, y=80, w=80, h=28 },
            renderData.custom.customData = payload
          }
                     ↓
                               pottery_renderer_fire()
                                 case CUSTOM:
                                   draw_button(cr, bb, payload)
                                     cairo_fill()    ← colored background
                                     cairo_stroke()  ← border
                                     pango_show()    ← centered label
```

---

## The State Map

```
pottery_hash_string("name_edit")  →  0xA3F2B7...  (FNV-1a 64-bit)
                                            ↓
┌────────────────────────────────────────────────────────────┐
│  PotteryStateMap  (open-addressing, power-of-2 capacity)  │
│                                                            │
│  slot 0  : empty                                           │
│  slot 1  : id=0xA3F2  type=EDIT                           │
│              hovered=false  focused=true  alive=true       │
│              edit.stb_state → STB_TexteditState            │
│              edit.cursor_pos = 5                           │
│              edit.scroll_offset = 0                        │
│  slot 2  : empty                                           │
│  slot 3  : id=0x7B1C  type=LIST                           │
│              list.scroll_y = 120.0                         │
│  ...                                                       │
└────────────────────────────────────────────────────────────┘

End-of-frame GC (pottery_state_map_gc):
  alive=true  → widget declared this frame → keep
  alive=false → widget absent this frame   → remove
```

---

## Toolbar and Statusbar — Direct Cairo

Unlike Clay widgets, toolbar and statusbar are drawn **directly with Cairo**
after the Clay render pass. Why?

- **Fixed height** → no need for Clay's layout engine
- **Always visible** → no scrolling, no clipping
- **Drawn last** → guaranteed to appear on top of all Clay content

```
y=0    ┌────────────────────────────────┐
       │  TOOLBAR   (Cairo direct, 36px)│
y=36   ├────────────────────────────────┤
       │                                │
       │  CLAY ZONE                     │
       │  height = H - 36 - 22         │
       │  Cairo translated by +36px    │
       │                                │
y=H-22 ├────────────────────────────────┤
       │  STATUSBAR (Cairo direct, 22px)│
y=H    └────────────────────────────────┘
```

---

## C Structs as Interfaces

One of Pottery's design principles — inherited from Cairo, SQLite, and Clay
themselves — is using **structs of function pointers** to achieve polymorphism
without C++ inheritance or vtables:

```c
typedef struct {
    bool  (*init)         (void *data, int w, int h, const char *title);
    bool  (*poll_event)   (void *data, PotteryEvent *evt);
    void  (*present)      (void *data);
    char *(*clipboard_get)(void *data);
    void  (*clipboard_set)(void *data, const char *utf8);
    void  (*destroy)      (void *data);
    void  *data;           /* opaque backend state (HWND, HDC, ...) */
} PotteryBackend;
```

Swapping from Win32 to X11 is simply providing a different `PotteryBackend`
struct. No inheritance, no virtual dispatch overhead, fully explicit.

---

## Integrating with an Existing Application

If your application already has a Win32 canvas (such as the BIM engine),
Pottery manages the UI **around** the canvas. The canvas remains an
independent Win32 child window with its own `WM_PAINT` — Pottery does not
touch it.

```
Parent Win32 window
  ├── Pottery surface (toolbar + Clay content + statusbar)
  └── Canvas Win32 child window (CAD/GIS rendering, independent)
```

Pottery redraws the UI at its own frame rate.
The canvas redraws only when data changes (`InvalidateRect`).
The two are completely independent.

---

*Pottery v0.1 — Clay · Cairo · Pango · librsvg · stb_textedit*
