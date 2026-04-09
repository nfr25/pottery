/*
 * molds/pottery_tree.c — Tree view widget avec virtualisation.
 *
 * Architecture :
 *   - PotteryModel fournit child_count() et parent_row()
 *   - Chaque frame : on recalcule la liste des nœuds visibles
 *     (DFS partiel selon l'état expanded)
 *   - Seules les lignes visibles dans le viewport sont rendues
 *   - L'indentation indique le niveau (level * indent_px)
 *   - Clic sur le triangle ▶/▼ toggle expand/collapse
 *   - Clic sur le label sélectionne le nœud
 */

#include "../pottery_internal.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* =========================================================================
 * Helpers expand/collapse
 * ========================================================================= */

static bool tree_is_expanded(PotteryTreeState *ts, int model_row) {
    for (int i = 0; i < ts->expanded_count; i++)
        if (ts->expanded_rows[i] == model_row) return true;
    return false;
}

static void tree_toggle_expanded(PotteryTreeState *ts, int model_row) {
    /* Chercher et retirer si présent */
    for (int i = 0; i < ts->expanded_count; i++) {
        if (ts->expanded_rows[i] == model_row) {
            ts->expanded_rows[i] =
                ts->expanded_rows[--ts->expanded_count];
            return;
        }
    }
    /* Ajouter */
    if (ts->expanded_count >= ts->expanded_cap) {
        int new_cap = ts->expanded_cap ? ts->expanded_cap * 2 : 16;
        ts->expanded_rows = realloc(ts->expanded_rows,
                                     new_cap * sizeof(int));
        ts->expanded_cap  = new_cap;
    }
    ts->expanded_rows[ts->expanded_count++] = model_row;
}

/* =========================================================================
 * Calcul des nœuds visibles (DFS)
 *
 * On parcourt le modèle en profondeur d'abord.
 * Un nœud est visible si tous ses ancêtres sont expandés.
 * On s'arrête à POTTERY_TREE_MAX_VISIBLE lignes.
 * =========================================================================*/

typedef struct {
    int model_row;
    int level;
} StackEntry;

static void tree_build_visible(PotteryTreeState *ts, PotteryModel *model) {
    if (!ts->visible) {
        ts->visible = malloc(POTTERY_TREE_MAX_VISIBLE *
                             sizeof(PotteryTreeVisibleNode));
    }
    ts->visible_count = 0;

    int total = model->row_count(model);

    /* Stack pour le DFS */
    StackEntry *stack = malloc(POTTERY_TREE_MAX_VISIBLE * sizeof(StackEntry));
    int         stack_top = 0;

    /* Pousser les racines (level 0) en ordre inverse pour DFS correct */
    for (int r = total - 1; r >= 0; r--) {
        int parent = model->parent_row ? model->parent_row(model, r) : -1;
        if (parent == -1) {
            stack[stack_top].model_row = r;
            stack[stack_top].level     = 0;
            stack_top++;
        }
    }

    while (stack_top > 0 &&
           ts->visible_count < POTTERY_TREE_MAX_VISIBLE) {
        StackEntry e = stack[--stack_top];
        int row   = e.model_row;
        int level = e.level;

        int nchildren = model->child_count
            ? model->child_count(model, row) : 0;
        bool expanded = tree_is_expanded(ts, row);

        PotteryTreeVisibleNode *vn = &ts->visible[ts->visible_count++];
        vn->model_row    = row;
        vn->level        = level;
        vn->has_children = (nchildren > 0);
        vn->expanded     = expanded;

        /* Si expandé, pousser les enfants en ordre inverse */
        if (expanded && nchildren > 0) {
            /* Trouver les enfants : les rows dont parent_row == row */
            for (int r = total - 1; r >= 0; r--) {
                if (model->parent_row && model->parent_row(model, r) == row) {
                    if (stack_top < POTTERY_TREE_MAX_VISIBLE) {
                        stack[stack_top].model_row = r;
                        stack[stack_top].level     = level + 1;
                        stack_top++;
                    }
                }
            }
        }
    }

    free(stack);
}

/* =========================================================================
 * pottery_mold_tree
 * ========================================================================= */

bool pottery_mold_tree(PotteryKiln *kiln, const char *id,
                        int *selected_row, const PotteryTreeOpts *opts) {
    if (!opts || !opts->model) return false;

    PotteryModel *model = opts->model;
    bool changed        = false;
    PotteryInput *in    = &kiln->input;

    /* ---- 1. Widget state ---- */
    uint64_t           wid   = pottery_hash_string(id);
    PotteryWidgetState *state = pottery_state_map_get_or_create(
        &kiln->state_map, wid, POTTERY_WIDGET_TREE);
    if (!state) return false;
    state->alive = true;

    PotteryTreeState *ts = &state->tree;

    /* ---- 2. Métriques ---- */
    float row_h    = opts->row_height > 0.0f
        ? opts->row_height
        : kiln->glaze.padding_y * 2.0f + 16.0f;
    float indent   = opts->indent > 0.0f ? opts->indent : 16.0f;

    /* ---- 3. Calcul des nœuds visibles ---- */
    tree_build_visible(ts, model);
    int total_visible = ts->visible_count;

    /* ---- 4. Sizing ---- */
    PotterySizing w = (opts->base.width.type == POTTERY_SIZING_FIT &&
                       opts->base.width.value == 0)
        ? POTTERY_GROW() : opts->base.width;
    PotterySizing h = (opts->base.height.type == POTTERY_SIZING_FIT &&
                       opts->base.height.value == 0)
        ? POTTERY_GROW() : opts->base.height;

    /* ---- 5. Container scrollable ---- */
    char scroll_id[128];
    snprintf(scroll_id, sizeof(scroll_id), "%s__scroll__", id);

    Clay_ElementDeclaration scroll_decl = {0};
    scroll_decl.layout = (Clay_LayoutConfig){
        .sizing = {
            .width  = (w.type == POTTERY_SIZING_GROW) ? CLAY_SIZING_GROW()
                    : (w.type == POTTERY_SIZING_FIXED) ? CLAY_SIZING_FIXED(w.value)
                    :                                    CLAY_SIZING_GROW(),
            .height = (h.type == POTTERY_SIZING_GROW) ? CLAY_SIZING_GROW()
                    : (h.type == POTTERY_SIZING_FIXED) ? CLAY_SIZING_FIXED(h.value)
                    :                                    CLAY_SIZING_FIXED(200.0f),
        },
        .layoutDirection = CLAY_TOP_TO_BOTTOM,
    };
    scroll_decl.clip = (Clay_ClipElementConfig){
        .vertical    = true,
        .horizontal  = false,
        .childOffset = Clay_GetScrollOffset(),
    };

    Clay__OpenElementWithId(POTTERY_ID(scroll_id));
    Clay__ConfigureOpenElement(scroll_decl);

    /* ---- 6. Virtualisation ---- */
    Clay_ElementData scroll_data = Clay_GetElementData(POTTERY_ID(scroll_id));
    float visible_h = scroll_data.found
        ? scroll_data.boundingBox.height : 200.0f;

    int first_vis = (int)(ts->scroll_y / row_h);
    int vis_n     = (int)ceilf(visible_h / row_h) + 1;
    int last_vis  = first_vis + vis_n;
    if (first_vis < 0)            first_vis = 0;
    if (last_vis  > total_visible) last_vis  = total_visible;

    /* Spacer haut */
    if (first_vis > 0) {
        char top_id[128];
        snprintf(top_id, sizeof(top_id), "%s__top__", id);
        Clay_ElementDeclaration sp = {0};
        sp.layout.sizing.width  = CLAY_SIZING_GROW();
        sp.layout.sizing.height = CLAY_SIZING_FIXED(first_vis * row_h);
        Clay__OpenElementWithId(POTTERY_ID(top_id));
        Clay__ConfigureOpenElement(sp);
        Clay__CloseElement();
    }

    /* ---- 7. Lignes visibles ---- */
    for (int vi = first_vis; vi < last_vis; vi++) {
        PotteryTreeVisibleNode *vn = &ts->visible[vi];
        int   row      = vn->model_row;
        bool  selected = (selected_row && *selected_row == row);

        char row_id[128];
        snprintf(row_id, sizeof(row_id), "%s__trow__%d", id, vi);

        /* Récupérer le texte depuis le modèle */
        const char *label = model->get_cell
            ? model->get_cell(model, row, 0) : "";
        PotteryIcon *icon = model->get_icon
            ? model->get_icon(model, row) : NULL;

        /* Payload pour le renderer */
        PotteryCustomPayload *rp = pottery_payload_alloc(&kiln->payload_pool);
        if (!rp) break;
        rp->type                 = POTTERY_CUSTOM_TREE_ROW;
        rp->state                = state;
        rp->glaze                = &kiln->glaze;
        rp->tree_row.label       = label;
        rp->tree_row.icon        = icon;
        rp->tree_row.level       = vn->level;
        rp->tree_row.has_children= vn->has_children;
        rp->tree_row.expanded    = vn->expanded;
        rp->tree_row.selected    = selected;

        /* Padding gauche selon le niveau */
        float left_pad = (float)vn->level * indent + kiln->glaze.padding_x;

        Clay_ElementDeclaration rd = {0};
        rd.layout = (Clay_LayoutConfig){
            .sizing = {
                .width  = CLAY_SIZING_GROW(),
                .height = CLAY_SIZING_FIXED(row_h),
            },
            .padding = { .left = (uint16_t)left_pad },
        };
        rd.custom.customData = rp;

        Clay__OpenElementWithId(POTTERY_ID(row_id));
        Clay__ConfigureOpenElement(rd);
        Clay__CloseElement();

        /* ---- Hit test ---- */
        Clay_ElementData rd_data = Clay_GetElementData(POTTERY_ID(row_id));
        if (rd_data.found) {
            Clay_BoundingBox rb = rd_data.boundingBox;
            bool hovered = (in->mouse_x >= (int)rb.x &&
                            in->mouse_x <= (int)(rb.x + rb.width) &&
                            in->mouse_y >= (int)rb.y &&
                            in->mouse_y <= (int)(rb.y + rb.height));

            if (hovered && in->mouse_clicked[0]) {
                /* Clic sur le triangle (zone gauche ~16px) ? */
                float triangle_x = rb.x + left_pad;
                bool on_triangle = vn->has_children &&
                    in->mouse_x >= (int)(triangle_x - 4) &&
                    in->mouse_x <= (int)(triangle_x + 20);

                if (on_triangle) {
                    tree_toggle_expanded(ts, row);
                } else {
                    if (selected_row) *selected_row = row;
                    changed = true;
                }
            }
        }
    }

    /* Spacer bas */
    int remaining = total_visible - last_vis;
    if (remaining > 0) {
        char bot_id[128];
        snprintf(bot_id, sizeof(bot_id), "%s__bot__", id);
        Clay_ElementDeclaration sp = {0};
        sp.layout.sizing.width  = CLAY_SIZING_GROW();
        sp.layout.sizing.height = CLAY_SIZING_FIXED(remaining * row_h);
        Clay__OpenElementWithId(POTTERY_ID(bot_id));
        Clay__ConfigureOpenElement(sp);
        Clay__CloseElement();
    }

    Clay__CloseElement(); /* scroll container */

    /* ---- 8. Scroll molette ---- */
    if (scroll_data.found) {
        Clay_BoundingBox sb = scroll_data.boundingBox;
        bool over = (in->mouse_x >= (int)sb.x &&
                     in->mouse_x <= (int)(sb.x + sb.width) &&
                     in->mouse_y >= (int)sb.y &&
                     in->mouse_y <= (int)(sb.y + sb.height));
        if (over && in->wheel_dy != 0.0f) {
            float max_scroll = fmaxf(0.0f,
                total_visible * row_h - visible_h);
            ts->scroll_y -= in->wheel_dy * row_h * 3.0f;
            if (ts->scroll_y < 0.0f)          ts->scroll_y = 0.0f;
            if (ts->scroll_y > max_scroll)     ts->scroll_y = max_scroll;
        }
    }

    return changed;
}
