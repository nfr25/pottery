#include "../pottery_internal.h"
#include <string.h>
#include <stdbool.h>
#include <math.h>

bool pottery_mold_dropdown(PotteryKiln *kiln,
                           const char *id,
                           const char **items,
                           int count,
                           int *selected)
{
    uint64_t hash = POTTERY_ID(id);
    PotteryWidgetState *state = pottery_state_map_get_or_create(
        &kiln->state_map, hash, POTTERY_WIDGET_COMBO);
    PotteryComboState *combo = &state->combo;
    state->alive = true;

    /* Dummy rect for now */
    float x=10, y=50, w=150, h=24;

    PotteryInput *in = &kiln->input;
    int mx = in->mouse_x, my = in->mouse_y;
    bool click = in->mouse_clicked[0];

    /* Toggle open on click */
    if (mx>=x && mx<=x+w && my>=y && my<=y+h && click)
        combo->open = !combo->open;

    /* Draw box */
    cairo_t *cr = kiln->cr;
    cairo_set_source_rgb(cr, kiln->glaze.input_bg.r,
                              kiln->glaze.input_bg.g,
                              kiln->glaze.input_bg.b);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);

    /* Draw selected item */
    const char *label = (*selected>=0 && *selected<count) ? items[*selected] : "(none)";
    PotteryColor color = kiln->glaze.text_color;
    pottery_text_draw(cr, &kiln->text, label, strlen(label),
                      kiln->text.font_body, &color);

    /* Popup items */
    if (combo->open) {
        float item_h = 20;
        float popup_h = fmin(count*item_h, 120);
        cairo_set_source_rgb(cr, kiln->glaze.input_bg.r*0.9,
                                  kiln->glaze.input_bg.g*0.9,
                                  kiln->glaze.input_bg.b*0.9);
        cairo_rectangle(cr, x, y+h, w, popup_h);
        cairo_fill(cr);

        for (int i=0;i<count;i++) {
            float iy = y+h + i*item_h;
            if (mx>=x && mx<=x+w && my>=iy && my<=iy+item_h && click)
                *selected = i;

            PotteryColor item_color = (*selected==i) ?
                kiln->glaze.selection_bg : kiln->glaze.text_color;
            pottery_text_draw(cr, &kiln->text, items[i], strlen(items[i]),
                              kiln->text.font_body, &item_color);
        }
    }

    return false;
}