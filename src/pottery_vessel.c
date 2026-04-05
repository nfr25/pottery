/*
 * pottery_vessel.c — Layout container helpers (thin Clay wrappers).
 */

#include "pottery_internal.h"
#include <string.h>

/* =========================================================================
 * Sizing translation  PotterySizing → Clay_SizingAxis
 * ========================================================================= */

static Clay_SizingAxis translate_axis(PotterySizing s) {
    switch (s.type) {
        case POTTERY_SIZING_GROW:
            return CLAY_SIZING_GROW();
        case POTTERY_SIZING_FIXED:
            return CLAY_SIZING_FIXED(s.value);
        case POTTERY_SIZING_PERCENT:
            return CLAY_SIZING_PERCENT(s.value);
        default: /* FIT */
            return CLAY_SIZING_FIT();
    }
}

/* =========================================================================
 * pottery_vessel_begin / end
 * ========================================================================= */

void pottery_vessel_begin(PotteryKiln *kiln, const PotteryVesselDesc *desc) {
    (void)kiln;

    Clay_ChildAlignment align = {
        .x = (desc->align_x == POTTERY_ALIGN_CENTER) ? CLAY_ALIGN_X_CENTER
           : (desc->align_x == POTTERY_ALIGN_END)    ? CLAY_ALIGN_X_RIGHT
           :                                            CLAY_ALIGN_X_LEFT,
        .y = (desc->align_y == POTTERY_ALIGN_CENTER) ? CLAY_ALIGN_Y_CENTER
           : (desc->align_y == POTTERY_ALIGN_END)    ? CLAY_ALIGN_Y_BOTTOM
           :                                            CLAY_ALIGN_Y_TOP,
    };

    Clay_ElementDeclaration decl = {0};

    decl.layout = (Clay_LayoutConfig){
        .sizing = {
            .width  = translate_axis(desc->width),
            .height = translate_axis(desc->height),
        },
        .padding = {
            .left   = (uint16_t)desc->padding_x,
            .right  = (uint16_t)desc->padding_x,
            .top    = (uint16_t)desc->padding_y,
            .bottom = (uint16_t)desc->padding_y,
        },
        .childGap        = (uint16_t)desc->gap,
        .childAlignment  = align,
        .layoutDirection = (desc->direction == POTTERY_DIRECTION_COLUMN)
                            ? CLAY_TOP_TO_BOTTOM
                            : CLAY_LEFT_TO_RIGHT,
    };

    /* Scroll + clip via Clay_ClipElementConfig */
    if (desc->scroll_x || desc->scroll_y || desc->clip) {
        decl.clip = (Clay_ClipElementConfig){
            .horizontal  = desc->scroll_x,
            .vertical    = desc->scroll_y,
            .childOffset = Clay_GetScrollOffset(),
        };
    }

    /* ID : on utilise la macro CLAY() qui accepte un string littéral,
     * mais pour un id dynamique on passe par Clay__OpenElementWithId. */
    const char *id = desc->id ? desc->id : "__vessel__";
    /* CLAY_STRING n'accepte que des littéraux — on construit Clay_String manuellement */
    Clay_String clay_id = { .length = (int)strlen(id), .chars = id };
    Clay__OpenElementWithId(Clay_GetElementId(clay_id));
    Clay__ConfigureOpenElement(decl);
}

void pottery_vessel_end(PotteryKiln *kiln) {
    (void)kiln;
    Clay__CloseElement();
}
