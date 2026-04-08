/*
 * molds/pottery_combo.c — Combo box (dropdown) widget.
 *
 * Structure :
 *   - Un bouton header (label + flèche ▼)
 *   - Un popup flottant (Clay floating element) avec la liste des items
 *   - Sélection au clic, fermeture sur Escape ou clic extérieur
 */

#include "../pottery_internal.h"
#include <string.h>
#include <math.h>

/* =========================================================================
 * pottery_mold_combo
 * ========================================================================= */

bool pottery_mold_combo(PotteryKiln *kiln, const char *id,
                        const char **items, int item_count,
                        int *selected, const PotteryComboOpts *opts) {
    static const PotteryComboOpts default_opts = {0};
    if (!opts) opts = &default_opts;

    /* ---- 1. Widget state ---- */
    uint64_t           wid   = pottery_hash_string(id);
    PotteryWidgetState *state = pottery_state_map_get_or_create(
        &kiln->state_map, wid, POTTERY_WIDGET_COMBO);
    if (!state) return false;
    state->alive = true;

    PotteryInput *in = &kiln->input;
    bool changed     = false;
    bool focused     = (kiln->input.focused_id == wid);
    state->focused   = focused;

    /* ---- 2. Sizing ---- */
    float h = kiln->glaze.padding_y * 2.0f + 18.0f;

    PotterySizing w = (opts->base.width.type == POTTERY_SIZING_FIT &&
                       opts->base.width.value == 0)
        ? POTTERY_GROW() : opts->base.width;

    /* ---- 3. Payload header ---- */
    PotteryCustomPayload *payload = pottery_payload_alloc(&kiln->payload_pool);
    if (!payload) return false;
    payload->type                = POTTERY_CUSTOM_COMBO_BUTTON;
    payload->state               = state;
    payload->glaze               = &kiln->glaze;
    payload->combo.items         = items;
    payload->combo.count         = item_count;
    payload->combo.selected      = selected ? *selected : 0;

    /* ---- 4. Clay element header ---- */
    Clay_ElementDeclaration decl = {0};
    decl.layout = (Clay_LayoutConfig){
        .sizing = {
            .width  = (w.type == POTTERY_SIZING_GROW)  ? CLAY_SIZING_GROW()
                    : (w.type == POTTERY_SIZING_FIXED)  ? CLAY_SIZING_FIXED(w.value)
                    :                                     CLAY_SIZING_GROW(),
            .height = CLAY_SIZING_FIXED(h),
        },
    };
    decl.custom.customData = payload;

    Clay__OpenElementWithId(POTTERY_ID(id));
    Clay__ConfigureOpenElement(decl);
    Clay__CloseElement();

    /* ---- 5. Hit test header ---- */
    Clay_ElementData data = Clay_GetElementData(POTTERY_ID(id));
    if (data.found) {
        Clay_BoundingBox bb = data.boundingBox;

        bool hovered = (in->mouse_x >= (int)bb.x &&
                        in->mouse_x <= (int)(bb.x + bb.width) &&
                        in->mouse_y >= (int)bb.y &&
                        in->mouse_y <= (int)(bb.y + bb.height));

        state->hovered = hovered;
        state->pressed = hovered && in->mouse_down[0];

        if (hovered && in->mouse_clicked[0]) {
            /* Toggle popup */
            state->combo.open = !state->combo.open;
            kiln->input.focused_id = wid;
        }
    }

    /* Escape ferme le popup */
    if (state->combo.open) {
        for (int i = 0; i < in->key_count; i++) {
            if (in->key_queue[i].key == POTTERY_KEY_ESCAPE) {
                state->combo.open = false;
                kiln->input.focused_id = 0;
            }
        }
    }

    /* ---- 6. Popup flottant ---- */
    if (state->combo.open && data.found) {
        Clay_BoundingBox bb  = data.boundingBox;
        float item_h         = h;
        float popup_h        = item_h * (float)item_count + 2.0f;
        float popup_w        = bb.width;

        /* Créer un id unique pour le popup */
        char popup_id[128];
        snprintf(popup_id, sizeof(popup_id), "%s__popup__", id);

        Clay_ElementDeclaration popup_decl = {0};
        popup_decl.layout = (Clay_LayoutConfig){
            .sizing = {
                .width  = CLAY_SIZING_FIXED(popup_w),
                .height = CLAY_SIZING_FIXED(popup_h),
            },
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
        };
        popup_decl.floating = (Clay_FloatingElementConfig){
            .attachTo     = CLAY_ATTACH_TO_ELEMENT_WITH_ID,
            .parentId     = Clay_GetElementId(
                (Clay_String){ .length = (int)strlen(id), .chars = id }).id,
            .offset       = { 0.0f, bb.height + 2.0f },
            .zIndex       = 10,
        };
        popup_decl.backgroundColor = (Clay_Color){
            (uint8_t)(kiln->glaze.surface.r * 255),
            (uint8_t)(kiln->glaze.surface.g * 255),
            (uint8_t)(kiln->glaze.surface.b * 255),
            255
        };
        popup_decl.border = (Clay_BorderElementConfig){
            .color = {
                (uint8_t)(kiln->glaze.border.r * 255),
                (uint8_t)(kiln->glaze.border.g * 255),
                (uint8_t)(kiln->glaze.border.b * 255),
                255
            },
            .width = { 1, 1, 1, 1 },
        };

        Clay__OpenElementWithId(POTTERY_ID(popup_id));
        Clay__ConfigureOpenElement(popup_decl);

        /* Items */
        for (int i = 0; i < item_count; i++) {
            char item_id[160];
            snprintf(item_id, sizeof(item_id), "%s__item__%d", id, i);

            bool item_hovered = (state->combo.hovered_item == i);
            bool item_selected = (selected && *selected == i);

            PotteryCustomPayload *ip = pottery_payload_alloc(&kiln->payload_pool);
            if (!ip) break;
            ip->type              = POTTERY_CUSTOM_LIST_ROW;
            ip->state             = state;
            ip->glaze             = &kiln->glaze;
            ip->list_row.text     = items[i];
            ip->list_row.selected = item_selected || item_hovered;
            ip->list_row.col      = i;

            Clay_ElementDeclaration id_decl = {0};
            id_decl.layout = (Clay_LayoutConfig){
                .sizing = {
                    .width  = CLAY_SIZING_GROW(),
                    .height = CLAY_SIZING_FIXED(item_h),
                },
            };
            id_decl.custom.customData = ip;

            Clay__OpenElementWithId(POTTERY_ID(item_id));
            Clay__ConfigureOpenElement(id_decl);
            Clay__CloseElement();

            /* Hit test item */
            Clay_ElementData id_data = Clay_GetElementData(POTTERY_ID(item_id));
            if (id_data.found) {
                Clay_BoundingBox ib = id_data.boundingBox;
                bool ih = (in->mouse_x >= (int)ib.x &&
                           in->mouse_x <= (int)(ib.x + ib.width) &&
                           in->mouse_y >= (int)ib.y &&
                           in->mouse_y <= (int)(ib.y + ib.height));
                if (ih) state->combo.hovered_item = i;

                if (ih && in->mouse_clicked[0]) {
                    if (selected) *selected = i;
                    state->combo.open = false;
                    kiln->input.focused_id = 0;
                    changed = true;
                }
            }
        }

        Clay__CloseElement(); /* popup */

        /* Clic en dehors du popup → fermer */
        if (in->mouse_clicked[0] && data.found) {
            Clay_BoundingBox bb2 = data.boundingBox;
            bool in_header = (in->mouse_x >= (int)bb2.x &&
                              in->mouse_x <= (int)(bb2.x + bb2.width) &&
                              in->mouse_y >= (int)bb2.y &&
                              in->mouse_y <= (int)(bb2.y + bb2.height));
            /* Le clic sur le header est géré au-dessus — ici on ferme si ailleurs */
            if (!in_header) {
                /* Vérifier si le clic est dans le popup (approximation) */
                bool in_popup = (in->mouse_y > (int)(bb2.y + bb2.height) &&
                                 in->mouse_y < (int)(bb2.y + bb2.height + popup_h) &&
                                 in->mouse_x >= (int)bb2.x &&
                                 in->mouse_x <= (int)(bb2.x + popup_w));
                if (!in_popup) {
                    state->combo.open = false;
                }
            }
        }
    }

    return changed;
}
