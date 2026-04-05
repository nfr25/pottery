/*
 * molds/pottery_button.c — Button widget.
 *
 * Flow per frame:
 *   1. Hash the id string → stable uint64 widget ID
 *   2. Get-or-create PotteryWidgetState in the state map
 *   3. Ask Clay for the bounding box of our element (via Clay_GetElementData)
 *   4. Update hover / pressed / focused from input state
 *   5. Declare a Clay CUSTOM element (renderer will call draw_button)
 *   6. Return true if clicked this frame
 */

#include "../pottery_internal.h"
#include <stdio.h>
#include <string.h>

/* =========================================================================
 * Hit test helper
 * ========================================================================= */

static bool point_in_bb(Clay_BoundingBox bb, int x, int y) {
    return x >= (int)bb.x && x <= (int)(bb.x + bb.width)
        && y >= (int)bb.y && y <= (int)(bb.y + bb.height);
}

/* =========================================================================
 * pottery_mold_button
 * ========================================================================= */

bool pottery_mold_button(PotteryKiln *kiln, const char *id,
                          const char *label,
                          const PotteryButtonOpts *opts) {
    /* Default opts */
    static const PotteryButtonOpts default_opts = {0};
    if (!opts) opts = &default_opts;

    /* ---- 1. Widget state ---- */
    uint64_t           wid   = pottery_hash_string(id);
    PotteryWidgetState *state = pottery_state_map_get_or_create(
        &kiln->state_map, wid, POTTERY_WIDGET_BUTTON);
    if (!state) return false;
    state->alive = true;

    /* ---- 2. Build payload dans le pool du kiln (valide jusqu'à end_frame) ---- */
    PotteryCustomPayload *payload = pottery_payload_alloc(&kiln->payload_pool);
    fprintf(stderr, "[POTTERY] button mold: id=%s payload=%p\n", id, (void*)payload);
    if (!payload) return false;
    payload->type             = POTTERY_CUSTOM_BUTTON;
    payload->state            = state;
    payload->glaze            = &kiln->glaze;
    payload->button.label          = label;
    payload->button.icon           = opts->base.icon;
    payload->button.default_action = opts->default_action;

    /* ---- 3. Sizing ---- */
    PotterySizing w = (opts->base.width.type  == POTTERY_SIZING_FIT &&
                       opts->base.width.value == 0)
        ? POTTERY_FIT() : opts->base.width;
    PotterySizing h = (opts->base.height.type == POTTERY_SIZING_FIT &&
                       opts->base.height.value == 0)
        ? POTTERY_FIT() : opts->base.height;

    /* ---- 4. Declare Clay element ---- */
    /*
     * We use Clay's CUSTOM element type to pass our payload to the renderer.
     * Padding provides the default button content area.
     */
    uint16_t px = (uint16_t)kiln->glaze.padding_x;
    uint16_t py = (uint16_t)kiln->glaze.padding_y;

    Clay_ElementDeclaration decl = {0};
    decl.layout = (Clay_LayoutConfig){
        .sizing = {
            /* Translate PotterySizing to Clay */
            .width  = (w.type == POTTERY_SIZING_GROW)
                        ? CLAY_SIZING_GROW()
                      : (w.type == POTTERY_SIZING_FIXED)
                        ? CLAY_SIZING_FIXED(w.value)
                        : CLAY_SIZING_FIT(),
            .height = (h.type == POTTERY_SIZING_GROW)
                        ? CLAY_SIZING_GROW()
                      : (h.type == POTTERY_SIZING_FIXED)
                        ? CLAY_SIZING_FIXED(h.value)
                        : CLAY_SIZING_FIT(),
        },
        .padding = { .left = px, .right = px, .top = py, .bottom = py },
    };
    fprintf(stderr, "[POTTERY] button decl: customData=%p\n", (void*)payload);
    decl.custom.customData = payload;

    Clay__OpenElementWithId(Clay_GetElementId((Clay_String){.length=(int)strlen(id),.chars=id}));
    Clay__ConfigureOpenElement(decl);
    Clay__CloseElement();

    /* ---- 5. Hit test & state update using last frame's bounding box ---- */
    /*
     * Clay computes bounding boxes during EndLayout. We read the box computed
     * in the *previous* frame via Clay_GetElementData() — this is the standard
     * immediate-mode pattern: one frame of latency, imperceptible to users.
     */
    Clay_ElementData data = Clay_GetElementData(
        (Clay_GetElementId((Clay_String){.length=(int)strlen(id),.chars=id})));

    bool clicked = false;

    if (data.found) {
        Clay_BoundingBox bb = data.boundingBox;
        PotteryInput    *in = &kiln->input;

        bool hovered = point_in_bb(bb, in->mouse_x, in->mouse_y);

        state->hovered = hovered && !opts->base.disabled;
        state->pressed = hovered && in->mouse_down[0] && !opts->base.disabled;

        /* Click: mouse released inside widget while it was pressed */
        if (hovered && in->mouse_clicked[0] && !opts->base.disabled) {
            clicked = true;
            kiln->input.focused_id = wid;
        }

        /* Focus gained on click */
        state->focused = (kiln->input.focused_id == wid);
    }

    /* Keyboard activation: Space / Enter when focused */
    if (state->focused && !opts->base.disabled) {
        PotteryInput *in = &kiln->input;
        for (int i = 0; i < in->key_count; i++) {
            if (in->key_queue[i].key == POTTERY_KEY_RETURN) {
                clicked = true;
            }
        }
    }

    return clicked;
}
