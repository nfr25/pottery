/*
 * pottery_toolbar.c — Toolbar widget.
 *
 * Rendu Cairo direct (pas Clay) — hauteur fixe en haut de fenêtre.
 * Géré par le kiln exactement comme la statusbar.
 *
 * Types de boutons :
 *   POTTERY_BTN_NORMAL   — bouton simple
 *   POTTERY_BTN_TOGGLE   — état pressed persistant (on/off)
 *   POTTERY_BTN_SPLIT    — partie principale + flèche ▼ dropdown
 *   POTTERY_BTN_DISABLED — grisé, non cliquable
 *
 * Types d'icônes :
 *   - Texte label
 *   - Fonction Cairo (ClIconFn) — dessin vectoriel custom
 *   - SVG (PotteryIcon / RsvgHandle)
 *   - Multimode : cycle parmi N états au clic
 */

#include "pottery_internal.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* =========================================================================
 * Constantes de rendu
 * ========================================================================= */

#define TB_H          36.0f   /* hauteur toolbar px                    */
#define TB_BTN_W      32.0f   /* largeur bouton icône                  */
#define TB_BTN_W_TXT  60.0f   /* largeur bouton texte                  */
#define TB_ARROW_W    14.0f   /* largeur zone flèche split             */
#define TB_SEP_W       8.0f   /* largeur séparateur                    */
#define TB_PAD_X       4.0f   /* marge gauche                          */
#define TB_BTN_PAD     3.0f   /* marge verticale bouton                */
#define TB_RADIUS      4.0f   /* arrondi bouton                        */

/* =========================================================================
 * Helpers Cairo locaux (couleurs depuis glaze)
 * ========================================================================= */

static void tb_set_color(cairo_t *cr, const PotteryColor *c, float alpha) {
    cairo_set_source_rgba(cr, c->r, c->g, c->b, c->a * alpha);
}

static void tb_rounded_rect(cairo_t *cr, float x, float y,
                              float w, float h, float r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x+w-r, y+r,   r, -G_PI_2,  0.0);
    cairo_arc(cr, x+w-r, y+h-r, r,  0.0,     G_PI_2);
    cairo_arc(cr, x+r,   y+h-r, r,  G_PI_2,  G_PI);
    cairo_arc(cr, x+r,   y+r,   r,  G_PI,    3.0*G_PI_2);
    cairo_close_path(cr);
}

static void tb_arrow_down(cairo_t *cr, float cx, float cy,
                           const PotteryColor *c) {
    float s = 3.5f;
    tb_set_color(cr, c, 0.8f);
    cairo_move_to(cr, cx - s, cy - s * 0.5f);
    cairo_line_to(cr, cx,     cy + s * 0.5f);
    cairo_line_to(cr, cx + s, cy - s * 0.5f);
    cairo_close_path(cr);
    cairo_fill(cr);
}

/* =========================================================================
 * API publique — ajout d'éléments
 * ========================================================================= */

void pottery_toolbar_enable(PotteryKiln *kiln) {
    if (!kiln) return;
    kiln->has_toolbar = true;
    /* Recalculer les dimensions Clay maintenant que toolbar est activée */
    Clay_SetLayoutDimensions((Clay_Dimensions){
        .width  = (float)kiln->width,
        .height = (float)kiln->height
                  - (kiln->has_toolbar  ? 36.0f : 0.0f)
                  - (kiln->has_statusbar? 22.0f : 0.0f)
    });
}

static PotteryToolbarElem *tb_alloc(PotteryKiln *kiln) {
    if (kiln->toolbar_count >= POTTERY_TOOLBAR_MAX_ELEMS) return NULL;
    PotteryToolbarElem *e = &kiln->toolbar[kiln->toolbar_count++];
    memset(e, 0, sizeof(*e));
    e->enabled = true;
    return e;
}

void pottery_toolbar_add_btn(PotteryKiln *kiln, int id,
                              const char *label, int flags) {
    PotteryToolbarElem *e = tb_alloc(kiln);
    if (!e) return;
    e->type  = POTTERY_TB_BTN;
    e->id    = id;
    e->flags = flags;
    strncpy(e->label, label ? label : "", sizeof(e->label) - 1);
}

void pottery_toolbar_add_icon(PotteryKiln *kiln, int id,
                               PotteryToolbarIconFn fn,
                               const char *tooltip, int flags) {
    PotteryToolbarElem *e = tb_alloc(kiln);
    if (!e) return;
    e->type    = POTTERY_TB_ICON;
    e->id      = id;
    e->flags   = flags;
    e->icon_fn = fn;
    strncpy(e->tooltip, tooltip ? tooltip : "", sizeof(e->tooltip) - 1);
}

void pottery_toolbar_add_icon_multimode(PotteryKiln *kiln, int id,
                                         PotteryToolbarIconFn fn,
                                         const char *tooltip,
                                         int mode_count) {
    PotteryToolbarElem *e = tb_alloc(kiln);
    if (!e) return;
    e->type       = POTTERY_TB_ICON;
    e->id         = id;
    e->flags      = 0;
    e->icon_fn    = fn;
    e->mode_count = mode_count;
    strncpy(e->tooltip, tooltip ? tooltip : "", sizeof(e->tooltip) - 1);
}

void pottery_toolbar_add_svg(PotteryKiln *kiln, int id,
                              PotteryIcon *icon,
                              const char *tooltip, int flags) {
    PotteryToolbarElem *e = tb_alloc(kiln);
    if (!e) return;
    e->type  = POTTERY_TB_SVG;
    e->id    = id;
    e->flags = flags;
    e->icon  = icon;
    strncpy(e->tooltip, tooltip ? tooltip : "", sizeof(e->tooltip) - 1);
}

void pottery_toolbar_add_sep(PotteryKiln *kiln) {
    PotteryToolbarElem *e = tb_alloc(kiln);
    if (!e) return;
    e->type = POTTERY_TB_SEP;
}

void pottery_toolbar_set_cb(PotteryKiln *kiln,
                             PotteryToolbarCb cb, void *userdata) {
    kiln->toolbar_cb       = cb;
    kiln->toolbar_userdata = userdata;
}

void pottery_toolbar_set_pressed(PotteryKiln *kiln, int id, bool pressed) {
    for (int i = 0; i < kiln->toolbar_count; i++)
        if (kiln->toolbar[i].id == id)
            kiln->toolbar[i].pressed = pressed;
}

void pottery_toolbar_set_enabled(PotteryKiln *kiln, int id, bool enabled) {
    for (int i = 0; i < kiln->toolbar_count; i++)
        if (kiln->toolbar[i].id == id)
            kiln->toolbar[i].enabled = enabled;
}

void pottery_toolbar_set_mode(PotteryKiln *kiln, int id, int mode) {
    for (int i = 0; i < kiln->toolbar_count; i++)
        if (kiln->toolbar[i].id == id)
            kiln->toolbar[i].mode = mode;
}

int pottery_toolbar_get_mode(PotteryKiln *kiln, int id) {
    for (int i = 0; i < kiln->toolbar_count; i++)
        if (kiln->toolbar[i].id == id)
            return kiln->toolbar[i].mode;
    return 0;
}

/* =========================================================================
 * Hit test — appelé depuis pottery_input_push_event (backend)
 * Retourne true si l'événement est consommé par la toolbar
 * ========================================================================= */

bool pottery_toolbar_handle_event(PotteryKiln *kiln,
                                   const PotteryEvent *evt) {
    if (!kiln->has_toolbar) return false;

    int mx = 0, my = 0;
    bool is_move  = (evt->type == POTTERY_EVENT_MOUSE_MOVE);
    bool is_click = (evt->type == POTTERY_EVENT_MOUSE_UP);

    if (evt->type == POTTERY_EVENT_MOUSE_MOVE ||
        evt->type == POTTERY_EVENT_MOUSE_DOWN ||
        evt->type == POTTERY_EVENT_MOUSE_UP) {
        mx = evt->mouse.x;
        my = evt->mouse.y;
    } else {
        return false;
    }

    /* La toolbar occupe [0, W] × [0, TB_H] */
    if (my < 0 || my > (int)TB_H) {
        /* Hors toolbar — reset hover */
        if (is_move) {
            for (int i = 0; i < kiln->toolbar_count; i++) {
                kiln->toolbar[i].hovered       = false;
                kiln->toolbar[i].arrow_hovered = false;
            }
        }
        return false;
    }

    bool consumed = false;

    for (int i = 0; i < kiln->toolbar_count; i++) {
        PotteryToolbarElem *e = &kiln->toolbar[i];
        if (e->type == POTTERY_TB_SEP) continue;

        bool in_elem  = (mx >= (int)e->x && mx < (int)(e->x + e->w));
        bool in_arrow = false;

        if ((e->flags & POTTERY_BTN_SPLIT) && in_elem) {
            float ax = e->x + e->w - TB_ARROW_W;
            in_arrow = (mx >= (int)ax);
            in_elem  = !in_arrow;
        }

        if (is_move) {
            e->hovered       = in_elem || in_arrow;
            e->arrow_hovered = in_arrow;
            consumed = true;
        }

        if (is_click && e->enabled) {
            if (in_arrow && (e->flags & POTTERY_BTN_SPLIT)) {
                /* Clic flèche → callback avec ID_SPLIT_BASE + id */
                if (kiln->toolbar_cb)
                    kiln->toolbar_cb(POTTERY_TB_SPLIT_BASE + e->id,
                                     kiln->toolbar_userdata);
                consumed = true;
            } else if (in_elem) {
                /* Clic principal */
                if (e->mode_count > 0) {
                    /* Multimode : cycle */
                    e->mode = (e->mode + 1) % e->mode_count;
                } else if (e->flags & POTTERY_BTN_TOGGLE) {
                    e->pressed = !e->pressed;
                } else {
                    e->pressed = false;
                }
                if (kiln->toolbar_cb)
                    kiln->toolbar_cb(e->id, kiln->toolbar_userdata);
                consumed = true;
            }
        }
    }

    return consumed;
}

/* =========================================================================
 * Rendu — appelé par pottery_kiln_end_frame()
 * ========================================================================= */

void pottery_kiln_render_toolbar(PotteryKiln *kiln) {
    cairo_t      *cr    = kiln->cr;
    PotteryGlaze *glaze = &kiln->glaze;
    float W = (float)kiln->width;
    float H = TB_H;

    /* ---- Calcul des positions X ---- */
    float bx = TB_PAD_X;
    for (int i = 0; i < kiln->toolbar_count; i++) {
        PotteryToolbarElem *e = &kiln->toolbar[i];
        e->x = bx;
        switch (e->type) {
            case POTTERY_TB_SEP:
                e->w = TB_SEP_W; break;
            case POTTERY_TB_BTN:
                e->w = (e->label[0] != '\0') ? TB_BTN_W_TXT : TB_BTN_W;
                if (e->flags & POTTERY_BTN_SPLIT) e->w += TB_ARROW_W;
                break;
            default: /* ICON, SVG */
                e->w = TB_BTN_W;
                if (e->flags & POTTERY_BTN_SPLIT) e->w += TB_ARROW_W;
                break;
        }
        bx += e->w + 2.0f;
    }

    /* ---- Fond ---- */
    tb_set_color(cr, &glaze->surface, 1.0f);
    cairo_rectangle(cr, 0, 0, W, H);
    cairo_fill(cr);

    /* ---- Ligne basse ---- */
    tb_set_color(cr, &glaze->border, 1.0f);
    cairo_set_line_width(cr, 1.0f);
    cairo_move_to(cr, 0,   H - 0.5f);
    cairo_line_to(cr, W,   H - 0.5f);
    cairo_stroke(cr);

    /* ---- Éléments ---- */
    float by = TB_BTN_PAD;
    float bh = H - TB_BTN_PAD * 2.0f;

    for (int i = 0; i < kiln->toolbar_count; i++) {
        PotteryToolbarElem *e = &kiln->toolbar[i];

        /* Séparateur */
        if (e->type == POTTERY_TB_SEP) {
            float sx = e->x + TB_SEP_W * 0.5f;
            tb_set_color(cr, &glaze->border, 0.8f);
            cairo_set_line_width(cr, 1.0f);
            cairo_move_to(cr, sx, by + 2);
            cairo_line_to(cr, sx, by + bh - 2);
            cairo_stroke(cr);
            continue;
        }

        float bcy = by + bh * 0.5f;

        /* Fond hover / pressed — groupe alpha si disabled */
        if (!e->enabled) cairo_push_group(cr);

        if (e->pressed) {
            tb_set_color(cr, &glaze->primary, 0.25f);
            tb_rounded_rect(cr, e->x, by, e->w, bh, TB_RADIUS);
            cairo_fill(cr);
            /* Bordure primary */
            tb_set_color(cr, &glaze->primary, 0.7f);
            cairo_set_line_width(cr, 1.0f);
            tb_rounded_rect(cr, e->x+0.5f, by+0.5f, e->w-1, bh-1, TB_RADIUS);
            cairo_stroke(cr);
        } else if (e->hovered) {
            tb_set_color(cr, &glaze->hover, 0.5f);
            tb_rounded_rect(cr, e->x, by, e->w, bh, TB_RADIUS);
            cairo_fill(cr);
        }

        /* Contenu : SVG, icône fn, ou texte */
        float content_w = (e->flags & POTTERY_BTN_SPLIT)
            ? e->w - TB_ARROW_W : e->w;
        float content_cx = e->x + content_w * 0.5f;

        if (e->type == POTTERY_TB_SVG && e->icon) {
            float isz = bh - 6.0f;
            pottery_icon_draw(cr, e->icon,
                content_cx - isz * 0.5f,
                bcy - isz * 0.5f,
                isz, NULL);

        } else if (e->type == POTTERY_TB_ICON && e->icon_fn) {
            cairo_save(cr);
            cairo_rectangle(cr, e->x + 2, by + 2, content_w - 4, bh - 4);
            cairo_clip(cr);
            e->icon_fn(cr, content_cx, bcy, e->mode);
            cairo_restore(cr);

        } else if (e->label[0]) {
            /* Texte centré */
            cairo_save(cr);
            pango_cairo_update_context(cr, kiln->text.context);
            PangoLayout *layout = pango_layout_new(kiln->text.context);
            pango_layout_set_font_description(layout, kiln->text.font_label);
            pango_layout_set_text(layout, e->label, -1);
            int tw, th;
            pango_layout_get_pixel_size(layout, &tw, &th);
            cairo_move_to(cr,
                content_cx - tw * 0.5f,
                bcy - th * 0.5f);
            const PotteryColor *tc = e->pressed
                ? &glaze->primary : &glaze->text_primary;
            tb_set_color(cr, tc, 1.0f);
            pango_cairo_show_layout(cr, layout);
            g_object_unref(layout);
            cairo_restore(cr);
        }

        /* Zone flèche split */
        if (e->flags & POTTERY_BTN_SPLIT) {
            float ax = e->x + e->w - TB_ARROW_W;
            /* Séparateur vertical */
            tb_set_color(cr, &glaze->border, 0.6f);
            cairo_set_line_width(cr, 1.0f);
            cairo_move_to(cr, ax + 0.5f, by + 4);
            cairo_line_to(cr, ax + 0.5f, by + bh - 4);
            cairo_stroke(cr);
            /* Hover flèche */
            if (e->arrow_hovered) {
                tb_set_color(cr, &glaze->hover, 0.5f);
                tb_rounded_rect(cr, ax + 1, by, TB_ARROW_W - 1, bh, TB_RADIUS);
                cairo_fill(cr);
            }
            /* Flèche ▼ */
            tb_arrow_down(cr, ax + TB_ARROW_W * 0.5f, bcy,
                          &glaze->text_secondary);
        }

        /* Tooltip */
        if (e->hovered && e->tooltip[0]) {
            float tx = e->x + e->w * 0.5f;
            float ty = H + 2.0f;

            PangoLayout *tl = pango_layout_new(kiln->text.context);
            pango_layout_set_font_description(tl, kiln->text.font_label);
            pango_layout_set_text(tl, e->tooltip, -1);
            int ttw, tth;
            pango_layout_get_pixel_size(tl, &ttw, &tth);

            float box_x = tx - ttw * 0.5f - 4;
            float box_w = ttw + 8;

            /* Fond tooltip */
            tb_set_color(cr, &glaze->surface, 0.95f);
            cairo_rectangle(cr, box_x, ty, box_w, tth + 4);
            cairo_fill(cr);
            tb_set_color(cr, &glaze->border, 1.0f);
            cairo_set_line_width(cr, 1.0f);
            cairo_rectangle(cr, box_x, ty, box_w, tth + 4);
            cairo_stroke(cr);

            /* Texte tooltip */
            tb_set_color(cr, &glaze->text_secondary, 1.0f);
            cairo_move_to(cr, box_x + 4, ty + 2);
            pango_cairo_show_layout(cr, tl);
            g_object_unref(tl);
        }

        /* Fin groupe disabled */
        if (!e->enabled) {
            cairo_pop_group_to_source(cr);
            cairo_paint_with_alpha(cr, 0.32f);
        }
    }
}
