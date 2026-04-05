/*
 * molds/pottery_label.c — Label and utility molds (separator, spacer).
 */

#include "../pottery_internal.h"
#include <string.h>

/* =========================================================================
 * pottery_mold_label
 * ========================================================================= */

void pottery_mold_label(PotteryKiln *kiln, const char *id,
                         const char *text,
                         const PotteryLabelOpts *opts) {
    static const PotteryLabelOpts default_opts = {0};
    if (!opts) opts = &default_opts;

    PotterySizing w = (opts->base.width.type == POTTERY_SIZING_FIT &&
                       opts->base.width.value == 0)
        ? POTTERY_FIT() : opts->base.width;

    Clay_TextElementConfig text_cfg = {
        .textColor = {
            (uint8_t)(kiln->glaze.text_primary.r * 255),
            (uint8_t)(kiln->glaze.text_primary.g * 255),
            (uint8_t)(kiln->glaze.text_primary.b * 255),
            (uint8_t)(kiln->glaze.text_primary.a * 255),
        },
        .fontId   = 0,
        .fontSize = 0,
        .wrapMode = opts->wrap ? CLAY_TEXT_WRAP_WORDS : CLAY_TEXT_WRAP_NONE,
    };

    Clay_ElementDeclaration decl = {0};
    decl.layout = (Clay_LayoutConfig){
        .sizing = {
            .width  = (w.type == POTTERY_SIZING_GROW)  ? CLAY_SIZING_GROW()
                    : (w.type == POTTERY_SIZING_FIXED)  ? CLAY_SIZING_FIXED(w.value)
                    :                                     CLAY_SIZING_FIT(),
            .height = CLAY_SIZING_FIT(),
        },
    };

    Clay_String clay_id   = { .length = (int)strlen(id),  .chars = id   };
    Clay_String clay_text = { .length = (int)strlen(text), .chars = text };

    /* Wrapper : un element container pour le sizing, puis le text element */
    Clay__OpenElementWithId(Clay_GetElementId(clay_id));
    Clay__ConfigureOpenElement(decl);
    Clay__OpenTextElement(clay_text, text_cfg);
    Clay__CloseElement();
}

/* =========================================================================
 * pottery_mold_separator
 * ========================================================================= */

void pottery_mold_separator(PotteryKiln *kiln, bool horizontal) {
    float bw = kiln->glaze.border_width;
    PotteryColor *bc = &kiln->glaze.border;

    Clay_Color cc = {
        (uint8_t)(bc->r * 255),
        (uint8_t)(bc->g * 255),
        (uint8_t)(bc->b * 255),
        (uint8_t)(bc->a * 255),
    };

    Clay_ElementDeclaration decl = {0};
    decl.layout = (Clay_LayoutConfig){
        .sizing = {
            .width  = horizontal ? CLAY_SIZING_GROW()    : CLAY_SIZING_FIXED(bw),
            .height = horizontal ? CLAY_SIZING_FIXED(bw) : CLAY_SIZING_GROW(),
        },
    };
    decl.backgroundColor = cc;

    Clay__OpenElementWithId(Clay_GetElementId(CLAY_STRING("__sep__")));
    Clay__ConfigureOpenElement(decl);
    Clay__CloseElement();
}

/* =========================================================================
 * pottery_mold_spacer
 * ========================================================================= */

void pottery_mold_spacer(PotteryKiln *kiln, float size) {
    (void)kiln;

    Clay_ElementDeclaration decl = {0};
    decl.layout = (Clay_LayoutConfig){
        .sizing = {
            .width  = (size > 0.0f) ? CLAY_SIZING_FIXED(size) : CLAY_SIZING_GROW(),
            .height = (size > 0.0f) ? CLAY_SIZING_FIXED(size) : CLAY_SIZING_GROW(),
        },
    };

    Clay__OpenElementWithId(Clay_GetElementId(CLAY_STRING("__spacer__")));
    Clay__ConfigureOpenElement(decl);
    Clay__CloseElement();
}
