/*
 * pottery_renderer.c — Translates Clay_RenderCommandArray to Cairo draw calls.
 *
 * Command mapping:
 *   RECTANGLE  → rounded rect fill
 *   TEXT       → Pango layout
 *   IMAGE      → librsvg (PotteryIcon)
 *   BORDER     → rounded rect stroke
 *   SCISSOR_START / SCISSOR_END → cairo_clip push/pop
 *   CUSTOM     → pottery_renderer_custom() dispatch
 */

#include "pottery_internal.h"
#include <math.h>  /* fminf, etc. — G_PI/G_PI_2 viennent de GLib via pottery_internal.h */
#include <string.h>

/* =========================================================================
 * Cairo helpers
 * ========================================================================= */

static void set_color(cairo_t *cr, const PotteryColor *c) {
    cairo_set_source_rgba(cr, c->r, c->g, c->b, c->a);
}

/*
 * Adds a rounded rectangle path to the current Cairo context.
 * r = corner radius (clamped to half the shortest side).
 */
static void rounded_rect(cairo_t *cr,
                          float x, float y, float w, float h, float r) {
    float max_r = fminf(w, h) * 0.5f;
    if (r > max_r) r = max_r;
    if (r <= 0.0f) {
        cairo_rectangle(cr, x, y, w, h);
        return;
    }
    double x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x1 - r, y0 + r, r, -G_PI_2,      0.0);
    cairo_arc(cr, x1 - r, y1 - r, r,  0.0,       G_PI_2);
    cairo_arc(cr, x0 + r, y1 - r, r,  G_PI_2,    G_PI);
    cairo_arc(cr, x0 + r, y0 + r, r,  G_PI,  3.0*G_PI_2);
    cairo_close_path(cr);
}

/* Blend color `overlay` on top of current fill using its alpha */
static void blend_overlay(cairo_t *cr,
                           float x, float y, float w, float h, float r,
                           const PotteryColor *overlay) {
    if (overlay->a <= 0.0f) return;
    cairo_save(cr);
    set_color(cr, overlay);
    rounded_rect(cr, x, y, w, h, r);
    cairo_fill(cr);
    cairo_restore(cr);
}

/* =========================================================================
 * Custom command dispatch — per-widget drawing
 * ========================================================================= */

/* Forward declarations needed from pottery_text.c */
float pottery_text_cursor_x(PotteryText *text, const char *str, int len,
                             int byte_index, PangoFontDescription *font);

/* Forward declarations of per-widget draw functions */
static void draw_button   (PotteryRenderer *rend, Clay_BoundingBox bb,
                            PotteryCustomPayload *p);
static void draw_edit     (PotteryRenderer *rend, Clay_BoundingBox bb,
                            PotteryCustomPayload *p);
static void draw_combo_button (PotteryRenderer *rend, Clay_BoundingBox bb,
                                PotteryCustomPayload *p);
static void draw_list_row (PotteryRenderer *rend, Clay_BoundingBox bb,
                            PotteryCustomPayload *p);
static void draw_tree_row (PotteryRenderer *rend, Clay_BoundingBox bb,
                            PotteryCustomPayload *p);
static void draw_icon     (PotteryRenderer *rend, Clay_BoundingBox bb,
                            PotteryCustomPayload *p);

static void pottery_renderer_custom(PotteryRenderer *rend,
                                     Clay_BoundingBox bb,
                                     void *customData) {
    if (!customData) return;
    PotteryCustomPayload *p = (PotteryCustomPayload *)customData;

    switch (p->type) {
        case POTTERY_CUSTOM_BUTTON:      draw_button   (rend, bb, p); break;
        case POTTERY_CUSTOM_EDIT:        draw_edit         (rend, bb, p); break;
        case POTTERY_CUSTOM_COMBO_BUTTON:  draw_combo_button (rend, bb, p); break;
        case POTTERY_CUSTOM_LIST_ROW:    draw_list_row (rend, bb, p); break;
        case POTTERY_CUSTOM_TREE_ROW:    draw_tree_row (rend, bb, p); break;
        case POTTERY_CUSTOM_ICON:        draw_icon     (rend, bb, p); break;
        default: break;
    }
}

/* =========================================================================
 * Main render loop
 * ========================================================================= */

void pottery_renderer_fire(PotteryRenderer *rend,
                            Clay_RenderCommandArray commands) {
    cairo_t      *cr    = rend->cr;
    PotteryGlaze *glaze = rend->glaze;

    for (int i = 0; i < (int)commands.length; i++) {
        Clay_RenderCommand *cmd = Clay_RenderCommandArray_Get(&commands, i);
        Clay_BoundingBox    bb  = cmd->boundingBox;

        switch (cmd->commandType) {

            /* ---- Filled rectangle ---- */
            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
                /*
                 * API Clay >= 0.13 :
                 *   cmd->renderData.rectangle  →  Clay_RectangleRenderData
                 *     .backgroundColor : Clay_Color  (r,g,b,a floats 0-255)
                 *     .cornerRadius    : Clay_CornerRadius
                 */
                Clay_RectangleRenderData *rd = &cmd->renderData.rectangle;
                PotteryColor c = {
                    rd->backgroundColor.r / 255.0f,
                    rd->backgroundColor.g / 255.0f,
                    rd->backgroundColor.b / 255.0f,
                    rd->backgroundColor.a / 255.0f,
                };
                /* Use Clay's per-element corner radius if provided,
                 * fall back to glaze default. */
                float r = rd->cornerRadius.topLeft > 0.0f
                    ? rd->cornerRadius.topLeft
                    : glaze->border_radius;
                set_color(cr, &c);
                rounded_rect(cr, bb.x, bb.y, bb.width, bb.height, r);
                cairo_fill(cr);
                break;
            }

            /* ---- Text ---- */
            case CLAY_RENDER_COMMAND_TYPE_TEXT: {
                /*
                 * cmd->renderData.text  →  Clay_TextRenderData
                 *   .stringContents : Clay_StringSlice
                 *   .textColor      : Clay_Color
                 *   .fontId         : uint16_t
                 */
                Clay_TextRenderData *td  = &cmd->renderData.text;
                Clay_StringSlice     sl  = td->stringContents;

                PangoFontDescription *font;
                switch (td->fontId) {
                    case 1:  font = rend->text->font_label; break;
                    case 2:  font = rend->text->font_title; break;
                    case 3:  font = rend->text->font_mono;  break;
                    default: font = rend->text->font_body;  break;
                }

                PotteryColor c = {
                    td->textColor.r / 255.0f,
                    td->textColor.g / 255.0f,
                    td->textColor.b / 255.0f,
                    td->textColor.a / 255.0f,
                };

                cairo_move_to(cr, bb.x, bb.y);
                pottery_text_draw(cr, rend->text,
                                  sl.chars, (int)sl.length,
                                  font, &c);
                break;
            }

            /* ---- Border (stroke) ---- */
            case CLAY_RENDER_COMMAND_TYPE_BORDER: {
                /*
                 * cmd->renderData.border  →  Clay_BorderRenderData
                 *   .color       : Clay_Color
                 *   .width       : Clay_BorderWidth  { .top .bottom .left .right }
                 *   .cornerRadius: Clay_CornerRadius
                 */
                Clay_BorderRenderData *bd = &cmd->renderData.border;
                float width = (float)(bd->width.top    ? bd->width.top    :
                                      bd->width.bottom ? bd->width.bottom :
                                      bd->width.left   ? bd->width.left   :
                                                         bd->width.right);
                if (width <= 0.0f) break;

                PotteryColor c = {
                    bd->color.r / 255.0f,
                    bd->color.g / 255.0f,
                    bd->color.b / 255.0f,
                    bd->color.a / 255.0f,
                };
                float r = bd->cornerRadius.topLeft > 0.0f
                    ? bd->cornerRadius.topLeft
                    : glaze->border_radius;
                set_color(cr, &c);
                cairo_set_line_width(cr, width);
                rounded_rect(cr, bb.x + width * 0.5f,
                                  bb.y + width * 0.5f,
                                  bb.width  - width,
                                  bb.height - width, r);
                cairo_stroke(cr);
                break;
            }

            /* ---- Scissor / clip ---- */
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
                cairo_save(cr);
                cairo_rectangle(cr, bb.x, bb.y, bb.width, bb.height);
                cairo_clip(cr);
                break;

            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
                cairo_restore(cr);
                break;

            /* ---- Custom (widgets) ---- */
            case CLAY_RENDER_COMMAND_TYPE_CUSTOM: {
                Clay_CustomRenderData cd = cmd->renderData.custom;
                pottery_renderer_custom(rend, bb, cd.customData);
                break;
            }

            default:
                break;
        }
    }
}

/* =========================================================================
 * Per-widget draw functions
 * ========================================================================= */

/* ---- Button ---- */

static void draw_button(PotteryRenderer *rend, Clay_BoundingBox bb,
                         PotteryCustomPayload *p) {
    cairo_t            *cr    = rend->cr;
    PotteryGlaze       *glaze = rend->glaze;
    PotteryWidgetState *state = p->state;
    float r = glaze->border_radius;

    /* Base fill */
    if (p->button.default_action) {
        set_color(cr, &glaze->primary);
    } else {
        set_color(cr, &glaze->surface);
    }
    rounded_rect(cr, bb.x, bb.y, bb.width, bb.height, r);
    cairo_fill(cr);

    /* State overlays */
    if (state->pressed) {
        blend_overlay(cr, bb.x, bb.y, bb.width, bb.height, r, &glaze->pressed);
    } else if (state->hovered) {
        blend_overlay(cr, bb.x, bb.y, bb.width, bb.height, r, &glaze->hover);
    }

    /* Focus ring */
    if (state->focused) {
        set_color(cr, &glaze->primary);
        cairo_set_line_width(cr, 2.0f);
        rounded_rect(cr, bb.x + 1, bb.y + 1, bb.width - 2, bb.height - 2, r);
        cairo_stroke(cr);
    }

    /* Border for non-primary buttons */
    if (!p->button.default_action) {
        set_color(cr, &glaze->border);
        cairo_set_line_width(cr, glaze->border_width);
        rounded_rect(cr,
            bb.x + glaze->border_width * 0.5f,
            bb.y + glaze->border_width * 0.5f,
            bb.width  - glaze->border_width,
            bb.height - glaze->border_width, r);
        cairo_stroke(cr);
    }

    /* Label */
    const char *label = p->button.label;
    if (label && label[0]) {
        const PotteryColor *text_color = p->button.default_action
            ? &glaze->primary_text
            : &glaze->text_primary;

        /* Measure label to center it */
        int tw, th;
        PangoLayout *layout = pango_layout_new(rend->text->context);
        pango_layout_set_font_description(layout, rend->text->font_body);
        pango_layout_set_text(layout, label, -1);
        pango_layout_get_pixel_size(layout, &tw, &th);
        g_object_unref(layout);

        /* Leading icon */
        float text_x = bb.x + (bb.width  - (float)tw) * 0.5f;
        float text_y = bb.y + (bb.height - (float)th) * 0.5f;

        if (p->button.icon) {
            float icon_sz = glaze->icon_size;
            float total_w = icon_sz + 4.0f + (float)tw;
            text_x = bb.x + (bb.width - total_w) * 0.5f;
            pottery_icon_draw(cr, p->button.icon,
                text_x, bb.y + (bb.height - icon_sz) * 0.5f,
                icon_sz, text_color);
            text_x += icon_sz + 4.0f;
        }

        cairo_move_to(cr, text_x, text_y);
        pottery_text_draw(cr, rend->text, label, (int)strlen(label),
                          rend->text->font_body, text_color);
    }

    /* Disabled overlay */
    if (state->focused == false && p->state->hovered == false) {
        /* nothing extra */ (void)0;
    }
}

/* ---- Edit field ---- */

static void draw_edit(PotteryRenderer *rend, Clay_BoundingBox bb,
                       PotteryCustomPayload *p) {
    cairo_t            *cr    = rend->cr;
    PotteryGlaze       *glaze = rend->glaze;
    PotteryWidgetState *state = p->state;
    PotteryEditState   *es    = p->edit.state;
    float r = glaze->border_radius;

    /* Background */
    set_color(cr, &glaze->surface);
    rounded_rect(cr, bb.x, bb.y, bb.width, bb.height, r);
    cairo_fill(cr);

    /* Border — thicker + primary color when focused */
    float bw = state->focused ? 2.0f : glaze->border_width;
    const PotteryColor *border_c = state->focused
        ? &glaze->primary : &glaze->border;
    set_color(cr, border_c);
    cairo_set_line_width(cr, bw);
    rounded_rect(cr, bb.x + bw * 0.5f, bb.y + bw * 0.5f,
                  bb.width - bw, bb.height - bw, r);
    cairo_stroke(cr);

    /* Clip to inner area for text + cursor */
    float pad = glaze->padding_x;
    cairo_save(cr);
    cairo_rectangle(cr, bb.x + pad * 0.5f, bb.y,
                     bb.width - pad, bb.height);
    cairo_clip(cr);

    float text_x = bb.x + pad - (float)es->scroll_offset;
    float text_y = bb.y + glaze->padding_y;

    const char *display = es->buf;
    int         displen = es->buf ? (int)strlen(es->buf) : 0;

    if (displen == 0 && p->edit.placeholder) {
        /* Placeholder */
        cairo_move_to(cr, text_x, text_y);
        pottery_text_draw(cr, rend->text,
                          p->edit.placeholder,
                          (int)strlen(p->edit.placeholder),
                          rend->text->font_body,
                          &glaze->text_secondary);
    } else if (displen > 0) {
        /* Sélection highlight */
        int ss = es->select_start;
        int se = es->select_end;
        if (ss > se) { int t = ss; ss = se; se = t; }
        if (se > ss) {
            float sx0 = text_x + pottery_text_cursor_x(
                rend->text, display, displen, ss, rend->text->font_body);
            float sx1 = text_x + pottery_text_cursor_x(
                rend->text, display, displen, se, rend->text->font_body);
            set_color(cr, &glaze->selection);
            cairo_rectangle(cr, sx0, bb.y + glaze->padding_y * 0.5f,
                sx1 - sx0, bb.height - glaze->padding_y);
            cairo_fill(cr);
        }

        /* Text */
        cairo_move_to(cr, text_x, text_y);
        pottery_text_draw(cr, rend->text,
                          display, displen,
                          rend->text->font_body,
                          &glaze->text_primary);

        /* Cursor — drawn only when focused */
        if (state->focused) {
            /* Utiliser la vraie position du curseur depuis es->cursor_pos */
            int   cur  = es->cursor_pos;
            if (cur > displen) cur = displen;
            float cx = text_x + pottery_text_cursor_x(
                rend->text, display, displen, cur,
                rend->text->font_body);

            float char_h = bb.height - glaze->padding_y * 2.0f;
            set_color(cr, &glaze->primary);
            cairo_set_line_width(cr, 1.5f);
            cairo_move_to(cr, cx, bb.y + glaze->padding_y);
            cairo_line_to(cr, cx, bb.y + glaze->padding_y + char_h);
            cairo_stroke(cr);
        }
    }

    cairo_restore(cr); /* remove text clip */
}

/* ---- Combo button (header) ---- */

static void draw_combo_button(PotteryRenderer *rend, Clay_BoundingBox bb,
                               PotteryCustomPayload *p) {
    cairo_t            *cr    = rend->cr;
    PotteryGlaze       *glaze = rend->glaze;
    PotteryWidgetState *state = p->state;
    float r = glaze->border_radius;

    /* Background */
    set_color(cr, &glaze->surface);
    rounded_rect(cr, bb.x, bb.y, bb.width, bb.height, r);
    cairo_fill(cr);

    /* Hover overlay */
    if (state->hovered)
        blend_overlay(cr, bb.x, bb.y, bb.width, bb.height, r, &glaze->hover);

    /* Border — bleu si ouvert */
    float bw = state->combo.open ? 2.0f : glaze->border_width;
    const PotteryColor *bc = state->combo.open ? &glaze->primary : &glaze->border;
    set_color(cr, bc);
    cairo_set_line_width(cr, bw);
    rounded_rect(cr, bb.x + bw * 0.5f, bb.y + bw * 0.5f,
                  bb.width - bw, bb.height - bw, r);
    cairo_stroke(cr);

    /* Label (item sélectionné) */
    const char *label = (p->combo.selected >= 0 &&
                         p->combo.selected < p->combo.count)
        ? p->combo.items[p->combo.selected] : "";

    float arrow_sz = 8.0f;
    float arrow_x  = bb.x + bb.width - glaze->padding_x - arrow_sz * 0.5f;
    float arrow_y  = bb.y + bb.height * 0.5f;
    float text_x   = bb.x + glaze->padding_x;
    float text_y   = bb.y + glaze->padding_y;

    if (label && label[0]) {
        cairo_move_to(cr, text_x, text_y);
        pottery_text_draw(cr, rend->text, label, (int)strlen(label),
                          rend->text->font_body, &glaze->text_primary);
    }

    /* Flèche ▼ ou ▲ */
    set_color(cr, &glaze->text_secondary);
    cairo_save(cr);
    cairo_translate(cr, arrow_x, arrow_y);
    if (state->combo.open) {
        /* ▲ */
        cairo_move_to(cr, -arrow_sz * 0.5f,  arrow_sz * 0.25f);
        cairo_line_to(cr,  0.0f,            -arrow_sz * 0.25f);
        cairo_line_to(cr,  arrow_sz * 0.5f,  arrow_sz * 0.25f);
    } else {
        /* ▼ */
        cairo_move_to(cr, -arrow_sz * 0.5f, -arrow_sz * 0.25f);
        cairo_line_to(cr,  0.0f,             arrow_sz * 0.25f);
        cairo_line_to(cr,  arrow_sz * 0.5f, -arrow_sz * 0.25f);
    }
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_restore(cr);
}

/* ---- List row ---- */

static void draw_list_row(PotteryRenderer *rend, Clay_BoundingBox bb,
                           PotteryCustomPayload *p) {
    cairo_t      *cr    = rend->cr;
    PotteryGlaze *glaze = rend->glaze;

    bool is_header = (p->list_row.col == -1);

    /* Fond : en-tête ou sélection.
     * is_cell=true → cellule multi-col, le fond est sur le container parent */
    if (!p->list_row.is_cell) {
        if (is_header) {
            set_color(cr, &glaze->surface_alt);
            cairo_rectangle(cr, bb.x, bb.y, bb.width, bb.height);
            cairo_fill(cr);
            set_color(cr, &glaze->border);
            cairo_set_line_width(cr, 1.0f);
            cairo_move_to(cr, bb.x,            bb.y + bb.height - 0.5f);
            cairo_line_to(cr, bb.x + bb.width, bb.y + bb.height - 0.5f);
            cairo_stroke(cr);
        } else if (p->list_row.selected) {
            set_color(cr, &glaze->primary);
            cairo_rectangle(cr, bb.x, bb.y, bb.width, bb.height);
            cairo_fill(cr);
        }
    }

    /* Couleur texte */
    const PotteryColor *tc = is_header
        ? &glaze->text_primary
        : (p->list_row.selected ? &glaze->primary_text : &glaze->text_primary);

    PangoFontDescription *font = is_header
        ? rend->text->font_title
        : rend->text->font_body;

    float tx = bb.x + glaze->padding_x;
    float ty = bb.y + (bb.height - 14.0f) * 0.5f;

    if (p->list_row.text) {
        cairo_move_to(cr, tx, ty);
        pottery_text_draw(cr, rend->text,
                          p->list_row.text,
                          (int)strlen(p->list_row.text),
                          font, tc);
    }
}

/* ---- Tree row ---- */

static void draw_tree_row(PotteryRenderer *rend, Clay_BoundingBox bb,
                           PotteryCustomPayload *p) {
    cairo_t      *cr    = rend->cr;
    PotteryGlaze *glaze = rend->glaze;

    if (p->tree_row.selected) {
        set_color(cr, &glaze->primary);
        cairo_rectangle(cr, bb.x, bb.y, bb.width, bb.height);
        cairo_fill(cr);
    }

    float indent = (float)p->tree_row.level * (glaze->icon_size + 4.0f);
    float cx     = bb.x + indent + glaze->padding_x;
    float cy     = bb.y + bb.height * 0.5f;

    /* Expand/collapse triangle */
    if (p->tree_row.has_children) {
        const PotteryColor *tc = p->tree_row.selected
            ? &glaze->primary_text : &glaze->text_secondary;
        set_color(cr, tc);
        cairo_save(cr);
        cairo_translate(cr, cx, cy);
        if (p->tree_row.expanded) {
            cairo_move_to(cr, -4, -3);
            cairo_line_to(cr,  4, -3);
            cairo_line_to(cr,  0,  4);
        } else {
            cairo_move_to(cr, -3, -4);
            cairo_line_to(cr,  4,  0);
            cairo_line_to(cr, -3,  4);
        }
        cairo_close_path(cr);
        cairo_fill(cr);
        cairo_restore(cr);
    }
    cx += glaze->icon_size;

    /* Icon */
    if (p->tree_row.icon) {
        float isz = glaze->icon_size;
        pottery_icon_draw(cr, p->tree_row.icon, cx,
                           cy - isz * 0.5f, isz, NULL);
        cx += isz + 4.0f;
    }

    /* Label */
    const PotteryColor *tc = p->tree_row.selected
        ? &glaze->primary_text : &glaze->text_primary;
    float ty = bb.y + (bb.height - 14.0f) * 0.5f;
    cairo_move_to(cr, cx, ty);
    if (p->tree_row.label) {
        pottery_text_draw(cr, rend->text,
                          p->tree_row.label,
                          (int)strlen(p->tree_row.label),
                          rend->text->font_body, tc);
    }
}

/* ---- Icon ---- */

static void draw_icon(PotteryRenderer *rend, Clay_BoundingBox bb,
                       PotteryCustomPayload *p) {
    (void)rend;
    pottery_icon_draw(rend->cr, p->icon.icon,
                      bb.x, bb.y, p->icon.size, NULL);
}
