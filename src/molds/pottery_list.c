/*
 * molds/pottery_list.c — List view widget avec virtualisation.
 *
 * Seules les lignes visibles sont rendues — O(visible) et non O(total).
 * Utilise PotteryModel pour accéder aux données sans les posséder.
 *
 * Architecture :
 *   - Un vessel Clay scrollable contient les lignes visibles
 *   - La virtualisation est gérée via le scroll offset du state
 *   - Chaque ligne visible est un CUSTOM element (POTTERY_CUSTOM_LIST_ROW)
 */

#include "../pottery_internal.h"
#include <string.h>
#include <math.h>

/* =========================================================================
 * PotteryTableModel — implémentation du modèle simple
 * ========================================================================= */

static int table_row_count(PotteryModel *m) {
    return ((PotteryTableModel *)m)->rows;
}

static int table_col_count(PotteryModel *m) {
    return ((PotteryTableModel *)m)->cols;
}

static const char *table_get_cell(PotteryModel *m, int row, int col) {
    PotteryTableModel *tm = (PotteryTableModel *)m;
    int idx = row * tm->cols + col;
    return tm->data[idx];
}

static const char *table_get_header(PotteryModel *m, int col) {
    PotteryTableModel *tm = (PotteryTableModel *)m;
    return tm->headers ? tm->headers[col] : NULL;
}

void pottery_table_model_init(PotteryTableModel *m,
                               const char * const *data,
                               const char * const *headers,
                               int rows, int cols) {
    memset(m, 0, sizeof(*m));
    m->base.row_count  = table_row_count;
    m->base.col_count  = table_col_count;
    m->base.get_cell   = table_get_cell;
    m->base.get_header = table_get_header;
    m->data    = data;
    m->headers = headers;
    m->rows    = rows;
    m->cols    = cols;
}

/* =========================================================================
 * pottery_mold_list
 * ========================================================================= */

bool pottery_mold_list(PotteryKiln *kiln, const char *id,
                        int *selected_row, const PotteryListOpts *opts) {
    if (!opts || !opts->model) return false;

    PotteryModel *model = opts->model;

    /* ---- 1. Widget state ---- */
    uint64_t           wid   = pottery_hash_string(id);
    PotteryWidgetState *state = pottery_state_map_get_or_create(
        &kiln->state_map, wid, POTTERY_WIDGET_LIST);
    if (!state) return false;
    state->alive = true;

    bool changed = false;
    PotteryInput *in = &kiln->input;

    /* ---- 2. Métriques ---- */
    float row_h = opts->row_height > 0.0f
        ? opts->row_height
        : kiln->glaze.padding_y * 2.0f + 16.0f;  /* ~26px par défaut */

    int total_rows = model->row_count(model);
    int total_cols = (opts->column_count > 0)
        ? opts->column_count
        : model->col_count(model);

    /* Sizing du container */
    PotterySizing w = (opts->base.width.type == POTTERY_SIZING_FIT &&
                       opts->base.width.value == 0)
        ? POTTERY_GROW() : opts->base.width;
    PotterySizing h = (opts->base.height.type == POTTERY_SIZING_FIT &&
                       opts->base.height.value == 0)
        ? POTTERY_GROW() : opts->base.height;

    /* ---- 3. Vessel externe (header fixe + scroll container) ---- */
    char outer_id[128];
    snprintf(outer_id, sizeof(outer_id), "%s__outer__", id);

    Clay_ElementDeclaration outer_decl = {0};
    outer_decl.layout = (Clay_LayoutConfig){
        .sizing = {
            .width  = (w.type == POTTERY_SIZING_GROW)  ? CLAY_SIZING_GROW()
                    : (w.type == POTTERY_SIZING_FIXED)  ? CLAY_SIZING_FIXED(w.value)
                    :                                     CLAY_SIZING_GROW(),
            .height = (h.type == POTTERY_SIZING_GROW)  ? CLAY_SIZING_GROW()
                    : (h.type == POTTERY_SIZING_FIXED)  ? CLAY_SIZING_FIXED(h.value)
                    :                                     CLAY_SIZING_FIXED(200.0f),
        },
        .layoutDirection = CLAY_TOP_TO_BOTTOM,
    };
    Clay__OpenElementWithId(POTTERY_ID(outer_id));
    Clay__ConfigureOpenElement(outer_decl);

    /* ---- 4. En-têtes (optionnel) ---- */
    if (opts->show_header && model->get_header) {
        char header_id[128];
        snprintf(header_id, sizeof(header_id), "%s__header__", id);

        if (total_cols == 1) {
            /* Mono-colonne : un seul élément */
            PotteryCustomPayload *hp = pottery_payload_alloc(&kiln->payload_pool);
            if (hp) {
                hp->type              = POTTERY_CUSTOM_LIST_ROW;
                hp->state             = state;
                hp->glaze             = &kiln->glaze;
                hp->list_row.text     = model->get_header(model, 0);
                hp->list_row.selected = false;
                hp->list_row.col      = -1;
                hp->list_row.is_cell  = false;

                Clay_ElementDeclaration hd = {0};
                hd.layout = (Clay_LayoutConfig){
                    .sizing = { .width = CLAY_SIZING_GROW(),
                                .height = CLAY_SIZING_FIXED(row_h) },
                };
                hd.custom.customData = hp;
                Clay__OpenElementWithId(POTTERY_ID(header_id));
                Clay__ConfigureOpenElement(hd);
                Clay__CloseElement();
            }
        } else {
            /* Multi-colonnes : vessel horizontal */
            Clay_ElementDeclaration hrow = {0};
            hrow.layout = (Clay_LayoutConfig){
                .sizing = { .width  = CLAY_SIZING_GROW(),
                             .height = CLAY_SIZING_FIXED(row_h) },
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
            };
            hrow.backgroundColor = (Clay_Color){
                (uint8_t)(kiln->glaze.surface_alt.r * 255),
                (uint8_t)(kiln->glaze.surface_alt.g * 255),
                (uint8_t)(kiln->glaze.surface_alt.b * 255),
                255,
            };
            Clay__OpenElementWithId(POTTERY_ID(header_id));
            Clay__ConfigureOpenElement(hrow);

            for (int col = 0; col < total_cols; col++) {
                char hcell_id[160];
                snprintf(hcell_id, sizeof(hcell_id), "%s__hcell__%d", id, col);

                const char *htext = model->get_header(model, col);
                float col_w = 0.0f;
                if (opts->columns && col < opts->column_count)
                    col_w = opts->columns[col].width;

                PotteryCustomPayload *hp = pottery_payload_alloc(&kiln->payload_pool);
                if (!hp) break;
                hp->type              = POTTERY_CUSTOM_LIST_ROW;
                hp->state             = state;
                hp->glaze             = &kiln->glaze;
                hp->list_row.text     = htext;
                hp->list_row.selected = false;
                hp->list_row.col      = -1;   /* header */
                hp->list_row.is_cell  = false; /* dessiner fond header */

                Clay_ElementDeclaration hd = {0};
                hd.layout = (Clay_LayoutConfig){
                    .sizing = {
                        .width  = col_w > 0.0f
                                    ? CLAY_SIZING_FIXED(col_w)
                                    : CLAY_SIZING_GROW(),
                        .height = CLAY_SIZING_FIXED(row_h),
                    },
                };
                hd.custom.customData = hp;
                Clay__OpenElementWithId(POTTERY_ID(hcell_id));
                Clay__ConfigureOpenElement(hd);
                Clay__CloseElement();
            }

            Clay__CloseElement(); /* header row */
        }
    }

    /* ---- 3b. Container scrollable Clay ---- */
    char scroll_id[128];
    snprintf(scroll_id, sizeof(scroll_id), "%s__scroll__", id);

    Clay_ElementDeclaration scroll_decl = {0};
    scroll_decl.layout = (Clay_LayoutConfig){
        .sizing = {
            .width  = CLAY_SIZING_GROW(),
            .height = CLAY_SIZING_GROW(),  /* prend l'espace restant après header */
        },
        .layoutDirection = CLAY_TOP_TO_BOTTOM,
    };
    /* childOffset géré par Clay_UpdateScrollContainers automatiquement */
    scroll_decl.clip = (Clay_ClipElementConfig){
        .vertical   = true,
        .horizontal = false,
        /* childOffset sera mis à jour après l'ouverture */
    };

    Clay__OpenElementWithId(POTTERY_ID(scroll_id));
    /* Maintenant que l'élément est ouvert, Clay_GetScrollOffset()
     * retourne l'offset correct pour CE container */
    scroll_decl.clip.childOffset = Clay_GetScrollOffset();
    Clay__ConfigureOpenElement(scroll_decl);

    /* ---- 5. Virtualisation ----
     * On lit la position de scroll depuis Clay pour calculer
     * quelles lignes sont visibles dans le viewport.
     */
    Clay_ElementData scroll_data = Clay_GetElementData(POTTERY_ID(scroll_id));
    Clay_ElementId scroll_eid = Clay_GetElementId(
        (Clay_String){ .length=(int)strlen(scroll_id), .chars=scroll_id });
    Clay_ScrollContainerData scd = Clay_GetScrollContainerData(scroll_eid);

    float scroll_y = (scd.found && scd.scrollPosition)
        ? -scd.scrollPosition->y : 0.0f;
    if (scroll_y < 0.0f) scroll_y = 0.0f;

    /* Hauteur visible du viewport */
    float visible_h = scroll_data.found
        ? scroll_data.boundingBox.height : 200.0f;

    int first_row = (int)(scroll_y / row_h);
    int last_row  = first_row + (int)ceilf(visible_h / row_h) + 2;
    if (first_row < 0)          first_row = 0;
    if (last_row  > total_rows) last_row  = total_rows;

    /* ---- 6. Spacer haut (lignes au-dessus du viewport) ---- */
    if (first_row > 0) {
        char top_id[128];
        snprintf(top_id, sizeof(top_id), "%s__top__", id);
        Clay_ElementDeclaration sp = {0};
        sp.layout.sizing.width  = CLAY_SIZING_GROW();
        sp.layout.sizing.height = CLAY_SIZING_FIXED(first_row * row_h);
        Clay__OpenElementWithId(POTTERY_ID(top_id));
        Clay__ConfigureOpenElement(sp);
        Clay__CloseElement();
    }

    /* ---- 7. Lignes visibles ---- */
    for (int row = first_row; row < last_row; row++) {
        char row_id[128];
        snprintf(row_id, sizeof(row_id), "%s__row__%d", id, row);

        bool is_selected = (selected_row && *selected_row == row);

        if (total_cols == 1) {
            /* ---- Cas simple : une seule colonne ---- */
            const char *cell_text = model->get_cell(model, row, 0);

            PotteryCustomPayload *rp = pottery_payload_alloc(&kiln->payload_pool);
            if (!rp) break;
            rp->type              = POTTERY_CUSTOM_LIST_ROW;
            rp->state             = state;
            rp->glaze             = &kiln->glaze;
            rp->list_row.text     = cell_text;
            rp->list_row.selected = is_selected;
            rp->list_row.col      = row;

            Clay_ElementDeclaration rd = {0};
            rd.layout = (Clay_LayoutConfig){
                .sizing = { .width = CLAY_SIZING_GROW(),
                             .height = CLAY_SIZING_FIXED(row_h) },
            };
            rd.custom.customData = rp;

            Clay__OpenElementWithId(POTTERY_ID(row_id));
            Clay__ConfigureOpenElement(rd);
            Clay__CloseElement();

        } else {
            /* ---- Multi-colonnes : vessel horizontal ---- */
            /* Fond de sélection sur le container de ligne */
            Clay_ElementDeclaration row_decl = {0};
            row_decl.layout = (Clay_LayoutConfig){
                .sizing = { .width  = CLAY_SIZING_GROW(),
                             .height = CLAY_SIZING_FIXED(row_h) },
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
            };
            if (is_selected) {
                row_decl.backgroundColor = (Clay_Color){
                    (uint8_t)(kiln->glaze.primary.r * 255),
                    (uint8_t)(kiln->glaze.primary.g * 255),
                    (uint8_t)(kiln->glaze.primary.b * 255),
                    255,
                };
            }

            Clay__OpenElementWithId(POTTERY_ID(row_id));
            Clay__ConfigureOpenElement(row_decl);

            for (int col = 0; col < total_cols; col++) {
                char cell_id[160];
                snprintf(cell_id, sizeof(cell_id), "%s__cell__%d_%d", id, row, col);

                const char *cell_text = model->get_cell(model, row, col);

                /* Largeur de colonne : depuis opts->columns si défini */
                float col_w = 0.0f;
                if (opts->columns && col < opts->column_count)
                    col_w = opts->columns[col].width;

                PotteryCustomPayload *cp = pottery_payload_alloc(&kiln->payload_pool);
                if (!cp) break;
                cp->type              = POTTERY_CUSTOM_LIST_ROW;
                cp->state             = state;
                cp->glaze             = &kiln->glaze;
                cp->list_row.text     = cell_text;
                cp->list_row.selected = is_selected;
                cp->list_row.col      = col;
                cp->list_row.is_cell  = true;

                Clay_ElementDeclaration cd = {0};
                cd.layout = (Clay_LayoutConfig){
                    .sizing = {
                        .width  = col_w > 0.0f
                                    ? CLAY_SIZING_FIXED(col_w)
                                    : CLAY_SIZING_GROW(),
                        .height = CLAY_SIZING_FIXED(row_h),
                    },
                };
                cd.custom.customData = cp;

                Clay__OpenElementWithId(POTTERY_ID(cell_id));
                Clay__ConfigureOpenElement(cd);
                Clay__CloseElement();
            }

            Clay__CloseElement(); /* row vessel */
        }

        /* Hit test sur le container de ligne */
        Clay_ElementData rd_data = Clay_GetElementData(POTTERY_ID(row_id));
        if (rd_data.found) {
            Clay_BoundingBox rb = rd_data.boundingBox;
            bool hovered = (in->mouse_x >= (int)rb.x &&
                            in->mouse_x <= (int)(rb.x + rb.width) &&
                            in->mouse_y >= (int)rb.y &&
                            in->mouse_y <= (int)(rb.y + rb.height));
            if (hovered && in->mouse_clicked[0]) {
                if (selected_row) *selected_row = row;
                changed = true;
            }
        }
    }

    /* ---- 8. Spacer bas (lignes sous le viewport) ---- */
    if (last_row < total_rows) {
        char bot_id[128];
        snprintf(bot_id, sizeof(bot_id), "%s__bot__", id);
        Clay_ElementDeclaration sp = {0};
        sp.layout.sizing.width  = CLAY_SIZING_GROW();
        sp.layout.sizing.height = CLAY_SIZING_FIXED((total_rows - last_row) * row_h);
        Clay__OpenElementWithId(POTTERY_ID(bot_id));
        Clay__ConfigureOpenElement(sp);
        Clay__CloseElement();
    }

    Clay__CloseElement(); /* scroll container */
    Clay__CloseElement(); /* outer vessel */

    /* Scroll géré par Clay via Clay_UpdateScrollContainers dans pottery_kiln.c */

    return changed;
}
