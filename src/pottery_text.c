/*
 * pottery_text.c — Pango/Cairo text subsystem.
 *
 * Responsibilities:
 *   - Maintain PangoContext and resolved PangoFontDescriptions
 *   - Provide Clay_SetMeasureTextFunction callback (pottery_text_measure)
 *   - LRU ring-buffer cache of PangoLayouts to avoid re-creating them
 *     on every Clay measure call (called O(n_text_elements) per frame)
 *   - pottery_text_draw() for the renderer
 */

#include "pottery_internal.h"

#include <string.h>
#include <stdlib.h>

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

/* Simple cache key: hash of (chars, length, font_ptr) */
static uint64_t make_cache_key(const char *chars, int len,
                                PangoFontDescription *font) {
    uint64_t h = 14695981039346656037ULL;
    for (int i = 0; i < len; i++) {
        h ^= (uint8_t)chars[i];
        h *= 1099511628211ULL;
    }
    /* Mix in font pointer as surrogate for font identity */
    h ^= (uint64_t)(uintptr_t)font;
    h *= 1099511628211ULL;
    return h ? h : 1;
}

/* Retrieve or create a PangoLayout for the given text + font.
 * Returned layout is owned by the cache — do not unref. */
static PangoLayout *cache_get_or_create(PotteryText *text,
                                         const char *chars, int len,
                                         PangoFontDescription *font) {
    uint64_t key = make_cache_key(chars, len, font);

    /* Search cache (linear scan over 64 entries — fast enough) */
    for (int i = 0; i < 64; i++) {
        if (text->cache[i].key == key)
            return text->cache[i].layout;
    }

    /* Miss — create new PangoLayout */
    PangoLayout *layout = pango_layout_new(text->context);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, chars, len);

    int pw, ph;
    pango_layout_get_pixel_size(layout, &pw, &ph);

    /* Evict oldest entry (ring buffer) */
    int slot = text->cache_head;
    if (text->cache[slot].layout)
        g_object_unref(text->cache[slot].layout);

    text->cache[slot].key          = key;
    text->cache[slot].layout       = layout;
    text->cache[slot].pixel_width  = pw;
    text->cache[slot].pixel_height = ph;

    text->cache_head = (slot + 1) & 63;
    return layout;
}

/* =========================================================================
 * Init / destroy
 * ========================================================================= */

bool pottery_text_init(PotteryText *text, cairo_t *cr,
                       const PotteryGlaze *glaze) {
    memset(text, 0, sizeof(*text));

    text->context = pango_cairo_create_context(cr);
    if (!text->context) return false;

    pottery_text_update_fonts(text, glaze);
    return true;
}

void pottery_text_destroy(PotteryText *text) {
    for (int i = 0; i < 64; i++) {
        if (text->cache[i].layout) {
            g_object_unref(text->cache[i].layout);
            text->cache[i].layout = NULL;
        }
    }
    if (text->font_body)  pango_font_description_free(text->font_body);
    if (text->font_label) pango_font_description_free(text->font_label);
    if (text->font_title) pango_font_description_free(text->font_title);
    if (text->font_mono)  pango_font_description_free(text->font_mono);
    if (text->context)    g_object_unref(text->context);
}

void pottery_text_update_fonts(PotteryText *text, const PotteryGlaze *glaze) {
    if (text->font_body)  pango_font_description_free(text->font_body);
    if (text->font_label) pango_font_description_free(text->font_label);
    if (text->font_title) pango_font_description_free(text->font_title);
    if (text->font_mono)  pango_font_description_free(text->font_mono);

    text->font_body  = pango_font_description_from_string(glaze->font_body);
    text->font_label = pango_font_description_from_string(glaze->font_label);
    text->font_title = pango_font_description_from_string(glaze->font_title);
    text->font_mono  = pango_font_description_from_string(glaze->font_mono);

    /* Invalidate the layout cache (fonts changed) */
    for (int i = 0; i < 64; i++) {
        if (text->cache[i].layout) {
            g_object_unref(text->cache[i].layout);
            text->cache[i].layout = NULL;
            text->cache[i].key    = 0;
        }
    }
}

/* =========================================================================
 * Clay measure callback
 *
 * Clay calls this for every text element during layout.
 * fontId convention (matches pottery_renderer.c):
 *   0 = body, 1 = label, 2 = title, 3 = mono
 * ========================================================================= */

Clay_Dimensions pottery_text_measure(Clay_StringSlice slice,
                                      Clay_TextElementConfig *config,
                                      void *userdata) {
    PotteryText *text = (PotteryText *)userdata;

    PangoFontDescription *font;
    switch (config->fontId) {
        case 1:  font = text->font_label; break;
        case 2:  font = text->font_title; break;
        case 3:  font = text->font_mono;  break;
        default: font = text->font_body;  break;
    }

    if (!font) return (Clay_Dimensions){ 0, 0 };

    PangoLayout *layout = cache_get_or_create(
        text, slice.chars, (int)slice.length, font);

    int pw, ph;
    pango_layout_get_pixel_size(layout, &pw, &ph);

    return (Clay_Dimensions){ (float)pw, (float)ph };
}

/* =========================================================================
 * pottery_text_draw
 *
 * Draws a UTF-8 string at the current Cairo origin using Pango.
 * The caller sets the Cairo current point (cairo_move_to) before calling.
 * ========================================================================= */

void pottery_text_draw(cairo_t *cr, PotteryText *text,
                       const char *str, int len,
                       PangoFontDescription *font,
                       const PotteryColor *color) {
    if (!str || len == 0) return;

    cairo_set_source_rgba(cr, color->r, color->g, color->b, color->a);

    PangoLayout *layout = cache_get_or_create(text, str, len, font);

    /* Update Pango context for the current Cairo transform */
    pango_cairo_update_context(cr, text->context);

    pango_cairo_show_layout(cr, layout);
}

/* =========================================================================
 * pottery_text_cursor_x
 *
 * Returns the x pixel offset of the cursor at byte index `byte_index`
 * within `str`, using `font`. Used by pottery_mold_edit for cursor drawing.
 * ========================================================================= */

float pottery_text_cursor_x(PotteryText *text,
                             const char *str, int len,
                             int byte_index,
                             PangoFontDescription *font) {
    PangoLayout *layout = cache_get_or_create(text, str, len, font);

    PangoRectangle strong, weak;
    pango_layout_get_cursor_pos(layout, byte_index, &strong, &weak);
    (void)weak;

    return (float)(strong.x / PANGO_SCALE);
}

/* =========================================================================
 * pottery_text_index_at_x
 *
 * Returns the byte index in `str` closest to pixel x `px`.
 * Used by pottery_mold_edit for click-to-place-cursor.
 * ========================================================================= */

int pottery_text_index_at_x(PotteryText *text,
                              const char *str, int len,
                              float px,
                              PangoFontDescription *font) {
    PangoLayout *layout = cache_get_or_create(text, str, len, font);

    int index, trailing;
    pango_layout_xy_to_index(layout,
        (int)(px * PANGO_SCALE), 0,
        &index, &trailing);

    return index + trailing;
}
