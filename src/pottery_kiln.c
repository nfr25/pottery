/*
 * pottery_kiln.c — Kiln lifecycle, frame loop, glaze management.
 */

#define CLAY_IMPLEMENTATION
#include "pottery_internal.h"
#include <stdio.h>

#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* =========================================================================
 * Glaze defaults
 * ========================================================================= */

PotteryGlaze pottery_glaze_light(void) {
    PotteryGlaze g = {0};

    g.background     = (PotteryColor){ 0.95f, 0.95f, 0.96f, 1.0f };
    g.surface        = (PotteryColor){ 1.00f, 1.00f, 1.00f, 1.0f };
    g.surface_alt    = (PotteryColor){ 0.97f, 0.97f, 0.98f, 1.0f };
    g.border         = (PotteryColor){ 0.80f, 0.80f, 0.83f, 1.0f };

    g.primary        = (PotteryColor){ 0.20f, 0.46f, 0.90f, 1.0f };
    g.primary_text   = (PotteryColor){ 1.00f, 1.00f, 1.00f, 1.0f };

    g.hover          = (PotteryColor){ 1.00f, 1.00f, 1.00f, 0.06f };
    g.pressed        = (PotteryColor){ 0.00f, 0.00f, 0.00f, 0.12f };
    g.focused        = (PotteryColor){ 0.20f, 0.46f, 0.90f, 0.25f };
    g.disabled       = (PotteryColor){ 0.50f, 0.50f, 0.50f, 0.38f };
    g.selection      = (PotteryColor){ 0.20f, 0.46f, 0.90f, 0.35f };

    g.text_primary   = (PotteryColor){ 0.10f, 0.10f, 0.12f, 1.0f };
    g.text_secondary = (PotteryColor){ 0.45f, 0.45f, 0.50f, 1.0f };
    g.text_disabled  = (PotteryColor){ 0.60f, 0.60f, 0.63f, 1.0f };

    strncpy(g.font_body,  "Segoe UI 10",       sizeof(g.font_body));
    strncpy(g.font_label, "Segoe UI 9",        sizeof(g.font_label));
    strncpy(g.font_title, "Segoe UI Bold 11",  sizeof(g.font_title));
    strncpy(g.font_mono,  "Consolas 10",       sizeof(g.font_mono));

    g.border_radius   = 4.0f;
    g.border_width    = 1.0f;
    g.padding_x       = 10.0f;
    g.padding_y       = 5.0f;
    g.spacing         = 6.0f;
    g.icon_size       = 16.0f;
    g.scrollbar_width = 8.0f;

    return g;
}

PotteryGlaze pottery_glaze_dark(void) {
    PotteryGlaze g = {0};

    g.background     = (PotteryColor){ 0.12f, 0.12f, 0.14f, 1.0f };
    g.surface        = (PotteryColor){ 0.18f, 0.18f, 0.21f, 1.0f };
    g.surface_alt    = (PotteryColor){ 0.15f, 0.15f, 0.18f, 1.0f };
    g.border         = (PotteryColor){ 0.30f, 0.30f, 0.35f, 1.0f };

    g.primary        = (PotteryColor){ 0.35f, 0.60f, 1.00f, 1.0f };
    g.primary_text   = (PotteryColor){ 0.05f, 0.05f, 0.08f, 1.0f };

    g.hover          = (PotteryColor){ 1.00f, 1.00f, 1.00f, 0.07f };
    g.pressed        = (PotteryColor){ 0.00f, 0.00f, 0.00f, 0.18f };
    g.focused        = (PotteryColor){ 0.35f, 0.60f, 1.00f, 0.30f };
    g.disabled       = (PotteryColor){ 0.50f, 0.50f, 0.50f, 0.38f };
    g.selection      = (PotteryColor){ 0.35f, 0.60f, 1.00f, 0.40f };

    g.text_primary   = (PotteryColor){ 0.92f, 0.92f, 0.95f, 1.0f };
    g.text_secondary = (PotteryColor){ 0.60f, 0.60f, 0.65f, 1.0f };
    g.text_disabled  = (PotteryColor){ 0.40f, 0.40f, 0.45f, 1.0f };

    strncpy(g.font_body,  "Segoe UI 10",       sizeof(g.font_body));
    strncpy(g.font_label, "Segoe UI 9",        sizeof(g.font_label));
    strncpy(g.font_title, "Segoe UI Bold 11",  sizeof(g.font_title));
    strncpy(g.font_mono,  "Consolas 10",       sizeof(g.font_mono));

    g.border_radius   = 4.0f;
    g.border_width    = 1.0f;
    g.padding_x       = 10.0f;
    g.padding_y       = 5.0f;
    g.spacing         = 6.0f;
    g.icon_size       = 16.0f;
    g.scrollbar_width = 8.0f;

    return g;
}

/* =========================================================================
 * Clay error handler
 * ========================================================================= */

static void pottery_clay_error(Clay_ErrorData err) {
    /* TODO: route to a proper logging facility */
    (void)err;
}

/* =========================================================================
 * Kiln lifecycle
 * ========================================================================= */

PotteryKiln *pottery_kiln_create(const PotteryKilnDesc *desc) {
    assert(desc);
    assert(desc->backend);

    PotteryKiln *kiln = calloc(1, sizeof(PotteryKiln));
    if (!kiln) return NULL;

    kiln->backend = desc->backend;
    kiln->width   = desc->width;
    kiln->height  = desc->height;
    kiln->glaze   = desc->glaze ? *desc->glaze : pottery_glaze_light();

    /* ---- Backend init ---- */
    if (!desc->backend->init(desc->backend->data,
                              desc->width, desc->height, desc->title)) {
        free(kiln);
        return NULL;
    }

    /* ---- Cairo surface (created by backend, referenced here) ----
     * The backend allocates the surface and stores it in backend->data.
     * We retrieve it via a convention: the first field of every backend
     * data struct is a cairo_surface_t*.
     * This avoids adding a get_surface() callback for now.
     */
    kiln->surface = *((cairo_surface_t **)desc->backend->data);
    kiln->cr      = cairo_create(kiln->surface);

    /* ---- Text subsystem ---- */
    if (!pottery_text_init(&kiln->text, kiln->cr, &kiln->glaze)) {
        cairo_destroy(kiln->cr);
        free(kiln);
        return NULL;
    }

    /* ---- Icon cache ---- */
    pottery_icon_cache_init(&kiln->icons);

    /* ---- State map ---- */
    int cap = desc->max_widgets > 0 ? desc->max_widgets : 1024;
    /* Round up to next power of 2 */
    int pow2 = 1;
    while (pow2 < cap) pow2 <<= 1;
    pottery_state_map_init(&kiln->state_map, pow2);

    /* ---- Clay ---- */
    Clay_SetMaxElementCount(4096);
    kiln->clay_memory_size = Clay_MinMemorySize();
    kiln->clay_memory      = malloc(kiln->clay_memory_size);

    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(
        kiln->clay_memory_size, kiln->clay_memory);

    Clay_Initialize(arena,
        (Clay_Dimensions){ (float)desc->width, (float)desc->height },
        (Clay_ErrorHandler){ pottery_clay_error, NULL });

    Clay_SetMeasureTextFunction(pottery_text_measure, &kiln->text);

    kiln->has_statusbar = desc->statusbar;

    return kiln;
}

void pottery_kiln_destroy(PotteryKiln *kiln) {
    if (!kiln) return;
    pottery_text_destroy(&kiln->text);
    pottery_icon_cache_destroy(&kiln->icons);
    pottery_state_map_destroy(&kiln->state_map);
    free(kiln->clay_memory);
    cairo_destroy(kiln->cr);
    kiln->backend->destroy(kiln->backend->data);
    free(kiln);
}

void pottery_kiln_set_glaze(PotteryKiln *kiln, const PotteryGlaze *glaze) {
    assert(kiln && glaze);
    kiln->glaze = *glaze;
    pottery_text_update_fonts(&kiln->text, glaze);
}

/* =========================================================================
 * Frame loop
 * ========================================================================= */

bool pottery_kiln_begin_frame(PotteryKiln *kiln) {
    assert(kiln);

    pottery_input_reset_frame(&kiln->input);
    kiln->payload_pool.count = 0;  /* reset payload pool */

    /* Drain event queue */
    PotteryEvent evt;
    while (kiln->backend->poll_event(kiln->backend->data, &evt)) {
        if (evt.type == POTTERY_EVENT_QUIT) {
            kiln->quit_requested = true;
            return false;
        }
        if (evt.type == POTTERY_EVENT_RESIZE) {
            kiln->width  = evt.resize.x;
            kiln->height = evt.resize.y;
            kiln->backend->resize(kiln->backend->data,
                                   kiln->width, kiln->height);
            /* Update the surface reference (backend may reallocate) */
            kiln->surface = *((cairo_surface_t **)kiln->backend->data);
            cairo_destroy(kiln->cr);
            kiln->cr = cairo_create(kiln->surface);
            /* Mettre à jour le contexte Pango avec le nouveau cairo_t */
            pango_cairo_update_context(kiln->cr, kiln->text.context);
            /* Inform Clay */
            Clay_SetLayoutDimensions(
                (Clay_Dimensions){ (float)kiln->width, (float)kiln->height });
        }
        pottery_input_push_event(&kiln->input, &evt);
    }

    /* Update Clay pointer info */
    Clay_SetPointerState(
        (Clay_Vector2){ (float)kiln->input.mouse_x,
                        (float)kiln->input.mouse_y },
        kiln->input.mouse_down[0]);

    /* Mark all states as not-yet-alive for GC */
    for (int i = 0; i < kiln->state_map.capacity; i++) {
        if (kiln->state_map.entries[i].id != 0)
            kiln->state_map.entries[i].alive = false;
    }

    /* Informer Clay du scroll molette — nécessaire pour les clip elements */
    Clay_UpdateScrollContainers(
        true,  /* enableDragScrolling */
        (Clay_Vector2){ kiln->input.wheel_dx, kiln->input.wheel_dy },
        0.016f /* deltaTime ~ 60fps */
    );

    Clay_BeginLayout();
    return true;
}

void pottery_kiln_end_frame(PotteryKiln *kiln) {
    assert(kiln);

    Clay_RenderCommandArray commands = Clay_EndLayout(0.0f); /* deltaTime: 0 = pas d'animations Clay */

    /* Clear background */
    const PotteryColor *bg = &kiln->glaze.background;
    cairo_set_source_rgba(kiln->cr, bg->r, bg->g, bg->b, bg->a);
    cairo_paint(kiln->cr);

    /* ---- Statusbar Clay element (rendu automatique) ---- */
    if (kiln->has_statusbar) {
        pottery_kiln_render_statusbar(kiln);
    }

    /* Walk render commands */
    PotteryRenderer rend = {
        .cr        = kiln->cr,
        .text      = &kiln->text,
        .glaze     = &kiln->glaze,
        .input     = &kiln->input,
        .state_map = &kiln->state_map,
    };
    pottery_renderer_fire(&rend, commands);

    /* Present */
    kiln->backend->present(kiln->backend->data);

    /* GC stale widget states */
    pottery_state_map_gc(&kiln->state_map);

    kiln->frame_number++;
}

/* =========================================================================
 * Icon helpers (delegated to icon cache)
 * ========================================================================= */

PotteryIcon *pottery_kiln_load_icon(PotteryKiln *kiln,
                                     const char *name,
                                     const char *svg_path) {
    return pottery_icon_cache_load(&kiln->icons, name, svg_path);
}

PotteryIcon *pottery_kiln_load_icon_data(PotteryKiln *kiln,
                                          const char *name,
                                          const char *svg_data, size_t len) {
    return pottery_icon_cache_load_data(&kiln->icons, name, svg_data, len);
}

PotteryIcon *pottery_kiln_find_icon(PotteryKiln *kiln, const char *name) {
    return pottery_icon_cache_find(&kiln->icons, name);
}
