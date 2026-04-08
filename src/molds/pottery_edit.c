/*
 * molds/pottery_edit.c — Single-line text edit widget.
 */

#include "../pottery_internal.h"
#include <string.h>
#include <stdlib.h>

/* =========================================================================
 * Modèle de string pour stb_textedit
 * ========================================================================= */

typedef struct {
    char *buf;
    int   buf_size;
    int   len;
} PotteryEditCtx;

/* =========================================================================
 * État interne persistant par widget
 * ========================================================================= */

typedef struct {
    PotteryEditCtx        ctx;
    PangoFontDescription *font;
    PotteryText          *text_sys;
} PotteryEditInternal;

/* Pointeur global utilisé par les callbacks stb pendant le traitement */
static PotteryEditInternal *g_edit = NULL;

/* =========================================================================
 * Configuration stb_textedit
 * Premier include SANS IMPLEMENTATION pour obtenir les types (StbTexteditRow)
 * ========================================================================= */

#define STB_TEXTEDIT_STRING             PotteryEditCtx
#define STB_TEXTEDIT_STRINGLEN(ctx)     ((ctx)->len)
#define STB_TEXTEDIT_GETCHAR(ctx,i)     ((unsigned char)(ctx)->buf[i])
#define STB_TEXTEDIT_NEWLINE            '\n'
#define STB_TEXTEDIT_IS_SPACE(ch)       ((ch) == ' ')

#define STB_TEXTEDIT_GETWIDTH(ctx,n,i)         pottery_edit_char_width(ctx,n,i)
#define STB_TEXTEDIT_LAYOUTROW(r,ctx,n)        pottery_edit_layout_row(r,ctx,n)
#define STB_TEXTEDIT_INSERTCHARS(ctx,pos,c,n)  pottery_edit_insert(ctx,pos,c,n)
#define STB_TEXTEDIT_DELETECHARS(ctx,pos,n)    pottery_edit_delete(ctx,pos,n)

/* Touches */
#define STB_TEXTEDIT_K_LEFT         0x10001
#define STB_TEXTEDIT_K_RIGHT        0x10002
#define STB_TEXTEDIT_K_UP           0x10003
#define STB_TEXTEDIT_K_DOWN         0x10004
#define STB_TEXTEDIT_K_LINESTART    0x10005
#define STB_TEXTEDIT_K_LINEEND      0x10006
#define STB_TEXTEDIT_K_BACKSPACE    0x10007
#define STB_TEXTEDIT_K_DELETE       0x10008
#define STB_TEXTEDIT_K_UNDO         0x10009
#define STB_TEXTEDIT_K_REDO         0x1000a
#define STB_TEXTEDIT_K_WORDLEFT     0x1000b
#define STB_TEXTEDIT_K_WORDRIGHT    0x1000c
#define STB_TEXTEDIT_K_PGUP         0x1000d
#define STB_TEXTEDIT_K_PGDOWN       0x1000e
#define STB_TEXTEDIT_K_TEXTSTART    0x1000f
#define STB_TEXTEDIT_K_TEXTEND      0x10010
#define STB_TEXTEDIT_K_SHIFT        0x20000

/* stb attend une fonction pour convertir une touche en caractère */
#define STB_TEXTEDIT_KEYTOTEXT(k) \
    (((k) & ~STB_TEXTEDIT_K_SHIFT) >= 0x20 && \
     ((k) & ~STB_TEXTEDIT_K_SHIFT) < 0x10000 ? \
     (int)((k) & ~STB_TEXTEDIT_K_SHIFT) : -1)

/* Premier include : types seulement (pas d'implémentation) */
#include <stb_textedit.h>

/* =========================================================================
 * Forward declarations — StbTexteditRow est maintenant connu
 * ========================================================================= */

static float pottery_edit_char_width(PotteryEditCtx *ctx, int n, int i);
static void  pottery_edit_layout_row(StbTexteditRow *r, PotteryEditCtx *ctx, int n);
static int   pottery_edit_insert(PotteryEditCtx *ctx, int pos, const int *chars, int num);
static void  pottery_edit_delete(PotteryEditCtx *ctx, int pos, int num);

/* =========================================================================
 * Callbacks — implémentation
 * ========================================================================= */

static float pottery_edit_char_width(PotteryEditCtx *ctx, int n, int i) {
    (void)n;
    if (!g_edit || !g_edit->font) return 8.0f;
    Clay_StringSlice sl = { .chars = ctx->buf + i, .length = 1 };
    Clay_TextElementConfig cfg = { .fontId = 0 };
    return pottery_text_measure(sl, &cfg, g_edit->text_sys).width;
}

static void pottery_edit_layout_row(StbTexteditRow *r,
                                     PotteryEditCtx *ctx, int n) {
    float total_w = 0.0f;
    for (int i = n; i < ctx->len; i++)
        total_w += pottery_edit_char_width(ctx, n, i);
    r->x0                = 0.0f;
    r->x1                = total_w;
    r->baseline_y_delta  = 18.0f;
    r->ymin              = 0.0f;
    r->ymax              = 18.0f;
    r->num_chars         = ctx->len - n;
}

static int pottery_edit_insert(PotteryEditCtx *ctx, int pos,
                                const int *chars, int num) {
    if (ctx->len + num >= ctx->buf_size) return 0;
    memmove(ctx->buf + pos + num, ctx->buf + pos,
            (size_t)(ctx->len - pos));
    for (int i = 0; i < num; i++)
        ctx->buf[pos + i] = (char)chars[i];
    ctx->len += num;
    ctx->buf[ctx->len] = '\0';
    return 1;
}

static void pottery_edit_delete(PotteryEditCtx *ctx, int pos, int num) {
    memmove(ctx->buf + pos, ctx->buf + pos + num,
            (size_t)(ctx->len - pos - num));
    ctx->len -= num;
    ctx->buf[ctx->len] = '\0';
}

/* Second include : implémentation effective */
#define STB_TEXTEDIT_IMPLEMENTATION
#include <stb_textedit.h>

/* =========================================================================
 * pottery_mold_edit
 * ========================================================================= */

bool pottery_mold_edit(PotteryKiln *kiln, const char *id,
                        char *buf, int buf_size,
                        const PotteryEditOpts *opts) {
    static const PotteryEditOpts default_opts = {0};
    if (!opts) opts = &default_opts;

    /* ---- 1. Widget state ---- */
    uint64_t           wid   = pottery_hash_string(id);
    PotteryWidgetState *state = pottery_state_map_get_or_create(
        &kiln->state_map, wid, POTTERY_WIDGET_EDIT);
    if (!state) return false;
    state->alive = true;

    /* ---- 2. Init interne (premier frame) ---- */
    /* EditFull = PotteryEditInternal + STB_TexteditState dans un seul alloc */
    typedef struct {
        PotteryEditInternal base;
        STB_TexteditState   stb;
    } EditFull;

    EditFull *full = (EditFull *)state->edit.stb_state;
    if (!full) {
        full = calloc(1, sizeof(EditFull));
        stb_textedit_initialize_state(&full->stb, 1 /* single line */);
        state->edit.stb_state = full;
    }

    /* Sync */
    full->base.ctx.buf      = buf;
    full->base.ctx.buf_size = buf_size;
    full->base.ctx.len      = (int)strlen(buf);
    full->base.font         = kiln->text.font_body;
    full->base.text_sys     = &kiln->text;
    state->edit.buf         = buf;
    state->edit.buf_size    = buf_size;

    bool changed = false;
    bool focused = (kiln->input.focused_id == wid);
    state->focused = focused;

    /* ---- 3. Événements (seulement si focused) ---- */
    if (focused) {
        g_edit = &full->base;
        PotteryInput *in = &kiln->input;

        /* Caractères */
        for (int i = 0; i < in->char_count; i++) {
            uint32_t cp = in->char_queue[i];
            if (cp >= 32 && cp != 127) {
                if (!opts->max_length || full->base.ctx.len < opts->max_length) {
                    int ch = (int)cp;
                    stb_textedit_paste(&full->base.ctx, &full->stb, &ch, 1);
                    changed = true;
                }
            }
        }

        /* Touches */
        for (int i = 0; i < in->key_count; i++) {
            PotteryKey k    = in->key_queue[i].key;
            uint32_t   mods = in->key_queue[i].mods;
            bool ctrl  = (mods & POTTERY_MOD_CTRL)  != 0;
            bool shift = (mods & POTTERY_MOD_SHIFT) != 0;
            int  stb_key = 0;

            switch (k) {
                case POTTERY_KEY_LEFT:
                    stb_key = ctrl ? STB_TEXTEDIT_K_WORDLEFT : STB_TEXTEDIT_K_LEFT;
                    break;
                case POTTERY_KEY_RIGHT:
                    stb_key = ctrl ? STB_TEXTEDIT_K_WORDRIGHT : STB_TEXTEDIT_K_RIGHT;
                    break;
                case POTTERY_KEY_HOME:
                    stb_key = STB_TEXTEDIT_K_LINESTART; break;
                case POTTERY_KEY_END:
                    stb_key = STB_TEXTEDIT_K_LINEEND; break;
                case POTTERY_KEY_BACKSPACE:
                    stb_key = STB_TEXTEDIT_K_BACKSPACE; changed = true; break;
                case POTTERY_KEY_DELETE:
                    stb_key = STB_TEXTEDIT_K_DELETE; changed = true; break;
                case POTTERY_KEY_Z:
                    if (ctrl) { stb_key = STB_TEXTEDIT_K_UNDO; }
                    break;
                case POTTERY_KEY_Y:
                    if (ctrl) { stb_key = STB_TEXTEDIT_K_REDO; }
                    break;
                case POTTERY_KEY_A:
                    if (ctrl) {
                        full->stb.select_start = 0;
                        full->stb.select_end   = full->base.ctx.len;
                    }
                    break;
                case POTTERY_KEY_C:
                    if (ctrl) {
                        int s = full->stb.select_start;
                        int e = full->stb.select_end;
                        if (s > e) { int t = s; s = e; e = t; }
                        if (e > s) {
                            char tmp[1024] = {0};
                            int len = e - s < 1023 ? e - s : 1023;
                            memcpy(tmp, buf + s, (size_t)len);
                            kiln->backend->clipboard_set(kiln->backend->data, tmp);
                        }
                    }
                    break;
                case POTTERY_KEY_X:
                    if (ctrl) {
                        int s = full->stb.select_start;
                        int e = full->stb.select_end;
                        if (s > e) { int t = s; s = e; e = t; }
                        if (e > s) {
                            char tmp[1024] = {0};
                            int len = e - s < 1023 ? e - s : 1023;
                            memcpy(tmp, buf + s, (size_t)len);
                            kiln->backend->clipboard_set(kiln->backend->data, tmp);
                            stb_textedit_cut(&full->base.ctx, &full->stb);
                            changed = true;
                        }
                    }
                    break;
                case POTTERY_KEY_V:
                    if (ctrl) {
                        char *clip = kiln->backend->clipboard_get(kiln->backend->data);
                        if (clip) {
                            int len = (int)strlen(clip);
                            int *tmp = malloc((size_t)len * sizeof(int));
                            if (tmp) {
                                for (int j = 0; j < len; j++)
                                    tmp[j] = (unsigned char)clip[j];
                                stb_textedit_paste(&full->base.ctx, &full->stb, tmp, len);
                                free(tmp);
                                changed = true;
                            }
                            free(clip);
                        }
                    }
                    break;
                case POTTERY_KEY_ESCAPE:
                    kiln->input.focused_id = 0;
                    state->focused = false;
                    focused = false;
                    break;
                default: break;
            }

            if (stb_key) {
                if (shift) stb_key |= STB_TEXTEDIT_K_SHIFT;
                stb_textedit_key(&full->base.ctx, &full->stb, stb_key);
            }
        }
        g_edit = NULL;
    }

    /* ---- 4. Clic souris ---- */
    Clay_ElementData data = Clay_GetElementData(POTTERY_ID(id));
    if (data.found) {
        Clay_BoundingBox bb = data.boundingBox;
        PotteryInput    *in = &kiln->input;

        bool hovered = (in->mouse_x >= (int)bb.x &&
                        in->mouse_x <= (int)(bb.x + bb.width) &&
                        in->mouse_y >= (int)bb.y &&
                        in->mouse_y <= (int)(bb.y + bb.height));
        state->hovered = hovered;

        if (hovered && in->mouse_clicked[0]) {
            kiln->input.focused_id = wid;
            state->focused = true;
            focused = true;
            float rel_x = (float)in->mouse_x - bb.x
                          - kiln->glaze.padding_x
                          + (float)state->edit.scroll_offset;
            g_edit = &full->base;
            stb_textedit_click(&full->base.ctx, &full->stb, rel_x, 0.0f);
            g_edit = NULL;
        }

        /* Scroll horizontal pour garder le curseur visible */
        if (focused) {
            g_edit = &full->base;
            float cursor_x = pottery_text_cursor_x(
                &kiln->text, buf, full->base.ctx.len,
                full->stb.cursor, kiln->text.font_body);
            g_edit = NULL;

            float inner_w = bb.width - kiln->glaze.padding_x * 2.0f;
            float scroll  = (float)state->edit.scroll_offset;
            if (cursor_x - scroll > inner_w - 4.0f)
                scroll = cursor_x - inner_w + 4.0f;
            if (cursor_x - scroll < 0.0f)
                scroll = cursor_x;
            if (scroll < 0.0f) scroll = 0.0f;
            state->edit.scroll_offset = (int)scroll;
        }
    }

    /* ---- 4b. Sync cursor_pos et select vers state (pour le renderer) ---- */
    state->edit.cursor_pos   = full->stb.cursor;
    state->edit.select_start = full->stb.select_start;
    state->edit.select_end   = full->stb.select_end;

    /* ---- 5. Clay element ---- */
    PotterySizing w = (opts->base.width.type == POTTERY_SIZING_FIT &&
                       opts->base.width.value == 0)
        ? POTTERY_GROW() : opts->base.width;

    float h = (opts->base.height.type == POTTERY_SIZING_FIXED)
        ? opts->base.height.value
        : kiln->glaze.padding_y * 2.0f + 18.0f;

    PotteryCustomPayload *payload = pottery_payload_alloc(&kiln->payload_pool);
    if (!payload) return false;
    payload->type             = POTTERY_CUSTOM_EDIT;
    payload->state            = state;
    payload->glaze            = &kiln->glaze;
    payload->edit.state       = &state->edit;
    payload->edit.placeholder = opts->placeholder;
    payload->edit.password    = opts->password;

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

    return changed;
}
