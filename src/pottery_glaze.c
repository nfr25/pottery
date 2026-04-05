/*
 * pottery_glaze.c — Glaze utilities (interpolation, serialization — future).
 *
 * The two built-in glazes (light/dark) are defined in pottery_kiln.c
 * to avoid a circular dependency with the PotteryKiln struct.
 *
 * This file is reserved for:
 *   - pottery_glaze_lerp()     — animate between two glazes
 *   - pottery_glaze_from_file() — load from a simple INI/JSON theme file
 *   - pottery_glaze_to_file()  — serialize
 */

#include "pottery_internal.h"

/*
 * Linear interpolation between two glazes.
 * t = 0.0 → a, t = 1.0 → b.
 * Useful for smooth light↔dark transitions.
 */
PotteryGlaze pottery_glaze_lerp(const PotteryGlaze *a,
                                  const PotteryGlaze *b,
                                  float t) {
#define LERP_COLOR(field) \
    out.field.r = a->field.r + (b->field.r - a->field.r) * t; \
    out.field.g = a->field.g + (b->field.g - a->field.g) * t; \
    out.field.b = a->field.b + (b->field.b - a->field.b) * t; \
    out.field.a = a->field.a + (b->field.a - a->field.a) * t;

    PotteryGlaze out = *a; /* copy strings and metrics from a */

    LERP_COLOR(background)
    LERP_COLOR(surface)
    LERP_COLOR(surface_alt)
    LERP_COLOR(border)
    LERP_COLOR(primary)
    LERP_COLOR(primary_text)
    LERP_COLOR(hover)
    LERP_COLOR(pressed)
    LERP_COLOR(focused)
    LERP_COLOR(disabled)
    LERP_COLOR(selection)
    LERP_COLOR(text_primary)
    LERP_COLOR(text_secondary)
    LERP_COLOR(text_disabled)

#undef LERP_COLOR

    /* Interpolate metrics */
    out.border_radius   = a->border_radius   + (b->border_radius   - a->border_radius)   * t;
    out.border_width    = a->border_width    + (b->border_width    - a->border_width)    * t;
    out.padding_x       = a->padding_x       + (b->padding_x       - a->padding_x)       * t;
    out.padding_y       = a->padding_y       + (b->padding_y       - a->padding_y)       * t;
    out.spacing         = a->spacing         + (b->spacing         - a->spacing)         * t;
    out.scrollbar_width = a->scrollbar_width + (b->scrollbar_width - a->scrollbar_width) * t;

    /* Fonts: switch at t >= 0.5 (can't interpolate font strings) */
    if (t >= 0.5f) {
        memcpy(out.font_body,  b->font_body,  sizeof(out.font_body));
        memcpy(out.font_label, b->font_label, sizeof(out.font_label));
        memcpy(out.font_title, b->font_title, sizeof(out.font_title));
        memcpy(out.font_mono,  b->font_mono,  sizeof(out.font_mono));
    }

    return out;
}
