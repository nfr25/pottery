/*
 * pottery_svg.c — SVG icon cache backed by librsvg + Cairo.
 */

#include "pottery_internal.h"
#include <string.h>
#include <math.h>

/* =========================================================================
 * Cache init / destroy
 * ========================================================================= */

bool pottery_icon_cache_init(PotteryIconCache *cache) {
    memset(cache, 0, sizeof(*cache));
    return true;
}

void pottery_icon_cache_destroy(PotteryIconCache *cache) {
    for (int i = 0; i < cache->count; i++) {
        if (cache->icons[i].handle) {
            g_object_unref(cache->icons[i].handle);
            cache->icons[i].handle = NULL;
        }
    }
    cache->count = 0;
}

/* =========================================================================
 * Load helpers
 * ========================================================================= */

PotteryIcon *pottery_icon_cache_load(PotteryIconCache *cache,
                                      const char *name,
                                      const char *svg_path) {
    if (cache->count >= POTTERY_ICON_CACHE_MAX) return NULL;

    GError *err = NULL;
    RsvgHandle *handle = rsvg_handle_new_from_file(svg_path, &err);
    if (!handle) {
        if (err) g_error_free(err);
        return NULL;
    }

    PotteryIcon *icon = &cache->icons[cache->count++];
    strncpy(icon->name, name, sizeof(icon->name) - 1);
    icon->handle = handle;
    return icon;
}

PotteryIcon *pottery_icon_cache_load_data(PotteryIconCache *cache,
                                           const char *name,
                                           const char *data, size_t len) {
    if (cache->count >= POTTERY_ICON_CACHE_MAX) return NULL;

    GError *err = NULL;
    RsvgHandle *handle = rsvg_handle_new_from_data(
        (const guint8 *)data, (gsize)len, &err);
    if (!handle) {
        if (err) g_error_free(err);
        return NULL;
    }

    PotteryIcon *icon = &cache->icons[cache->count++];
    strncpy(icon->name, name, sizeof(icon->name) - 1);
    icon->handle = handle;
    return icon;
}

PotteryIcon *pottery_icon_cache_find(PotteryIconCache *cache,
                                      const char *name) {
    for (int i = 0; i < cache->count; i++) {
        if (strcmp(cache->icons[i].name, name) == 0)
            return &cache->icons[i];
    }
    return NULL;
}

/* =========================================================================
 * pottery_icon_draw
 *
 * Renders `icon` centered in a square of `size` pixels at (x, y).
 * `tint` is blended multiplicatively if non-NULL (approximated via
 * cairo_set_source_rgba + OPERATOR_MULTIPLY — not perfect, but avoids
 * a second surface allocation for the common case of monochrome icons).
 * ========================================================================= */

void pottery_icon_draw(cairo_t *cr, PotteryIcon *icon,
                       float x, float y, float size,
                       const PotteryColor *tint) {
    if (!icon || !icon->handle) return;

    /* Get natural SVG dimensions */
    RsvgRectangle viewport = { x, y, size, size };

    cairo_save(cr);

    GError *err = NULL;
    rsvg_handle_render_document(icon->handle, cr, &viewport, &err);
    if (err) g_error_free(err);

    /* Tint: paint a solid color over the icon using MULTIPLY or IN operator.
     * MULTIPLY works well for dark icons on light backgrounds;
     * for general use, consider a separate mask approach. */
    if (tint && tint->a > 0.0f) {
        cairo_set_operator(cr, CAIRO_OPERATOR_MULTIPLY);
        cairo_set_source_rgba(cr, tint->r, tint->g, tint->b, tint->a);
        cairo_rectangle(cr, x, y, size, size);
        cairo_fill(cr);
    }

    cairo_restore(cr);
}
