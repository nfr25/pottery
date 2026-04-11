/*
 * pottery_statusbar.c — Statusbar widget.
 *
 * La statusbar est gérée directement par le kiln — pas un mold appelé
 * par l'application. Elle est rendue automatiquement dans end_frame
 * si has_statusbar == true.
 *
 * Layout :
 *   ┌──────────────────────────────────────────────────────┐
 *   │ Section 0 (GROW) │ Section 1 (fixed) │ Section 2 ... │
 *   └──────────────────────────────────────────────────────┘
 *
 * L'application appelle pottery_statusbar_set() quand elle veut
 * mettre à jour une section — le texte est stocké dans le kiln.
 */

#include "pottery_internal.h"
#include <string.h>
#include <stdio.h>

/* =========================================================================
 * API publique
 * ========================================================================= */

void pottery_statusbar_set(PotteryKiln *kiln, int section,
                            const char *text, float width_hint) {
    if (!kiln || section < 0 || section >= POTTERY_STATUSBAR_MAX_SECTIONS)
        return;

    PotteryStatusbarSection *s = &kiln->statusbar[section];
    strncpy(s->text, text ? text : "", sizeof(s->text) - 1);
    s->text[sizeof(s->text) - 1] = '\0';
    s->width_hint = width_hint;
    s->used       = true;
}

void pottery_statusbar_clear(PotteryKiln *kiln) {
    if (!kiln) return;
    memset(kiln->statusbar, 0, sizeof(kiln->statusbar));
}

/* =========================================================================
 * Rendu interne — appelé par pottery_kiln_end_frame()
 *
 * La statusbar est rendue directement avec Cairo, sans passer par Clay.
 * Elle est toujours en bas de la fenêtre, hauteur fixe.
 * ========================================================================= */

#define STATUSBAR_HEIGHT 22.0f
#define STATUSBAR_PAD_X   8.0f
#define STATUSBAR_SEP_W   1.0f

void pottery_kiln_render_statusbar(PotteryKiln *kiln) {
    cairo_t      *cr    = kiln->cr;
    PotteryGlaze *glaze = &kiln->glaze;
    float W = (float)kiln->width;
    float H = (float)kiln->height;
    float y = H - STATUSBAR_HEIGHT;

    /* ---- Fond ---- */
    cairo_set_source_rgba(cr,
        glaze->surface_alt.r,
        glaze->surface_alt.g,
        glaze->surface_alt.b,
        glaze->surface_alt.a);
    cairo_rectangle(cr, 0, y, W, STATUSBAR_HEIGHT);
    cairo_fill(cr);

    /* ---- Ligne de séparation en haut ---- */
    cairo_set_source_rgba(cr,
        glaze->border.r, glaze->border.g,
        glaze->border.b, glaze->border.a);
    cairo_set_line_width(cr, 1.0f);
    cairo_move_to(cr, 0,  y + 0.5f);
    cairo_line_to(cr, W,  y + 0.5f);
    cairo_stroke(cr);

    /* ---- Compter les sections utilisées et calculer les largeurs ---- */
    int   n_sections = 0;
    float fixed_total = 0.0f;
    int   grow_count  = 0;

    for (int i = 0; i < POTTERY_STATUSBAR_MAX_SECTIONS; i++) {
        if (!kiln->statusbar[i].used) continue;
        n_sections++;
        if (kiln->statusbar[i].width_hint > 0.0f)
            fixed_total += kiln->statusbar[i].width_hint;
        else
            grow_count++;
    }

    if (n_sections == 0) return;

    /* Largeur disponible pour les sections GROW */
    float sep_total  = (n_sections - 1) * STATUSBAR_SEP_W;
    float grow_total = W - fixed_total - sep_total;
    float grow_w     = grow_count > 0 ? grow_total / (float)grow_count : 0.0f;
    if (grow_w < 0.0f) grow_w = 0.0f;

    /* ---- Dessiner chaque section ---- */
    float cx = 0.0f;
    float text_y = y + (STATUSBAR_HEIGHT - 14.0f) * 0.5f;

    for (int i = 0; i < POTTERY_STATUSBAR_MAX_SECTIONS; i++) {
        if (!kiln->statusbar[i].used) continue;

        PotteryStatusbarSection *s = &kiln->statusbar[i];
        float sw = s->width_hint > 0.0f ? s->width_hint : grow_w;

        /* Séparateur vertical (sauf pour la première section) */
        if (cx > 0.0f) {
            cairo_set_source_rgba(cr,
                glaze->border.r, glaze->border.g,
                glaze->border.b, glaze->border.a);
            cairo_set_line_width(cr, STATUSBAR_SEP_W);
            cairo_move_to(cr, cx + 0.5f, y + 3.0f);
            cairo_line_to(cr, cx + 0.5f, y + STATUSBAR_HEIGHT - 3.0f);
            cairo_stroke(cr);
            cx += STATUSBAR_SEP_W;
        }

        /* Clip pour cette section */
        cairo_save(cr);
        cairo_rectangle(cr, cx, y, sw, STATUSBAR_HEIGHT);
        cairo_clip(cr);

        /* Texte */
        if (s->text[0]) {
            cairo_move_to(cr, cx + STATUSBAR_PAD_X, text_y);
            pottery_text_draw(cr, &kiln->text,
                              s->text, (int)strlen(s->text),
                              kiln->text.font_label,
                              &glaze->text_secondary);
        }

        cairo_restore(cr);
        cx += sw;
    }
}
