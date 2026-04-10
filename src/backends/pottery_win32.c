/*
 * pottery_win32.c — Minimal Win32 backend for Pottery.
 *
 * Provides:
 *   - A Win32 window with a cairo_win32_surface
 *   - Event translation to PotteryEvent
 *   - Clipboard access (UTF-8 ↔ CF_UNICODETEXT)
 *
 * The first field of Win32BackendData is cairo_surface_t* so that
 * pottery_kiln.c can retrieve it via the *(cairo_surface_t**) convention.
 */

#include "../pottery_internal.h"

/* Cairo Win32 surface support */
#include <cairo-win32.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>  /* GET_X_LPARAM, GET_Y_LPARAM */

#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Backend data
 * ========================================================================= */

typedef struct {
    /* MUST be first field — pottery_kiln.c reads surface from here */
    cairo_surface_t *surface;

    HWND  hwnd;
    HDC     hdc;
    HBITMAP hbmp;   /* bitmap offscreen courant */
    int     width, height;
    bool  quit;

    /* Event queue (ring buffer) */
    PotteryEvent queue[256];
    int          queue_head;
    int          queue_tail;
} Win32BackendData;

/* =========================================================================
 * Event queue helpers
 * ========================================================================= */

static void enqueue(Win32BackendData *d, PotteryEvent evt) {
    int next = (d->queue_tail + 1) & 255;
    if (next != d->queue_head) { /* drop on overflow */
        d->queue[d->queue_tail] = evt;
        d->queue_tail = next;
    }
}

/* =========================================================================
 * Key translation
 * ========================================================================= */

static PotteryKey translate_vk(WPARAM vk) {
    switch (vk) {
        case VK_LEFT:   return POTTERY_KEY_LEFT;
        case VK_RIGHT:  return POTTERY_KEY_RIGHT;
        case VK_UP:     return POTTERY_KEY_UP;
        case VK_DOWN:   return POTTERY_KEY_DOWN;
        case VK_HOME:   return POTTERY_KEY_HOME;
        case VK_END:    return POTTERY_KEY_END;
        case VK_BACK:   return POTTERY_KEY_BACKSPACE;
        case VK_DELETE: return POTTERY_KEY_DELETE;
        case VK_RETURN: return POTTERY_KEY_RETURN;
        case VK_ESCAPE: return POTTERY_KEY_ESCAPE;
        case VK_TAB:    return POTTERY_KEY_TAB;
        case 'A':       return POTTERY_KEY_A;
        case 'C':       return POTTERY_KEY_C;
        case 'V':       return POTTERY_KEY_V;
        case 'X':       return POTTERY_KEY_X;
        case 'Z':       return POTTERY_KEY_Z;
        case 'Y':       return POTTERY_KEY_Y;
        default:        return POTTERY_KEY_UNKNOWN;
    }
}

static uint32_t get_mods(void) {
    uint32_t mods = 0;
    if (GetKeyState(VK_SHIFT)   & 0x8000) mods |= POTTERY_MOD_SHIFT;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= POTTERY_MOD_CTRL;
    if (GetKeyState(VK_MENU)    & 0x8000) mods |= POTTERY_MOD_ALT;
    return mods;
}

/* =========================================================================
 * Window procedure
 * ========================================================================= */

static LRESULT CALLBACK pottery_wndproc(HWND hwnd, UINT msg,
                                         WPARAM wp, LPARAM lp) {
    Win32BackendData *d = (Win32BackendData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return DefWindowProcW(hwnd, msg, wp, lp);

    PotteryEvent evt = {0};

    switch (msg) {
        case WM_CLOSE:
        case WM_DESTROY:
            evt.type = POTTERY_EVENT_QUIT;
            enqueue(d, evt);
            PostQuitMessage(0);
            return 0;

        case WM_SIZE: {
            int w = LOWORD(lp);
            int h = HIWORD(lp);
            if (w > 0 && h > 0 && (w != d->width || h != d->height)) {
                d->width  = w;
                d->height = h;
                evt.type     = POTTERY_EVENT_RESIZE;
                evt.resize.x = w;
                evt.resize.y = h;
                enqueue(d, evt);
            }
            return 0;
        }

        case WM_MOUSEMOVE:
            evt.type    = POTTERY_EVENT_MOUSE_MOVE;
            evt.mouse.x = GET_X_LPARAM(lp);
            evt.mouse.y = GET_Y_LPARAM(lp);
            evt.mods    = get_mods();
            enqueue(d, evt);
            return 0;

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            evt.type         = POTTERY_EVENT_MOUSE_DOWN;
            evt.mouse.x      = GET_X_LPARAM(lp);
            evt.mouse.y      = GET_Y_LPARAM(lp);
            evt.mouse.button = (msg == WM_LBUTTONDOWN) ? 0
                             : (msg == WM_RBUTTONDOWN) ? 1 : 2;
            evt.mods         = get_mods();
            SetCapture(hwnd);
            enqueue(d, evt);
            return 0;

        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            evt.type         = POTTERY_EVENT_MOUSE_UP;
            evt.mouse.x      = GET_X_LPARAM(lp);
            evt.mouse.y      = GET_Y_LPARAM(lp);
            evt.mouse.button = (msg == WM_LBUTTONUP) ? 0
                             : (msg == WM_RBUTTONUP) ? 1 : 2;
            evt.mods         = get_mods();
            ReleaseCapture();
            enqueue(d, evt);
            return 0;

        case WM_MOUSEWHEEL: {
            /* WHEEL_DELTA positif = molette vers le haut.
             * Clay scroll: positif = vers le bas → on inverse. */
            float delta = (float)GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
            evt.type     = POTTERY_EVENT_MOUSE_WHEEL;
            evt.wheel.dx = 0.0f;
            evt.wheel.dy = delta;
            evt.mods     = get_mods();
            enqueue(d, evt);
            return 0;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            PotteryKey key = translate_vk(wp);
            if (key != POTTERY_KEY_UNKNOWN) {
                evt.type         = POTTERY_EVENT_KEY_DOWN;
                evt.keyboard.key = key;
                evt.mods         = get_mods();
                enqueue(d, evt);
            }
            return 0;
        }

        case WM_CHAR:
            /* WM_CHAR delivers UTF-16 code units.
             * For BMP characters this is a single unit.
             * Surrogate pairs (> U+FFFF) need two messages — handle later.
             */
            if (wp >= 32 && wp != 127) { /* exclude control chars */
                evt.type           = POTTERY_EVENT_CHAR;
                evt.text.codepoint = (uint32_t)wp;
                evt.mods           = get_mods();
                enqueue(d, evt);
            }
            return 0;

        case WM_ERASEBKGND:
            /* On gère nous-mêmes l'effacement — évite le noir pendant le resize */
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            /* Blit from our offscreen Cairo surface */
            if (d->surface) {
                HDC src = cairo_win32_surface_get_dc(d->surface);
                BitBlt(hdc, 0, 0, d->width, d->height, src, 0, 0, SRCCOPY);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }

        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

/* =========================================================================
 * Backend callbacks
 * ========================================================================= */

static bool win32_init(void *data, int w, int h, const char *title) {
    Win32BackendData *d = (Win32BackendData *)data;

    d->width  = w;
    d->height = h;

    /* Register window class */
    WNDCLASSEXW wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = pottery_wndproc;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"PotteryWindow";
    RegisterClassExW(&wc);

    /* Convert title to wide */
    wchar_t wtitle[256] = {0};
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 255);

    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT  rect  = { 0, 0, w, h };
    AdjustWindowRect(&rect, style, FALSE);

    d->hwnd = CreateWindowExW(0, L"PotteryWindow", wtitle, style,
                               CW_USEDEFAULT, CW_USEDEFAULT,
                               rect.right - rect.left,
                               rect.bottom - rect.top,
                               NULL, NULL,
                               GetModuleHandleW(NULL), NULL);
    if (!d->hwnd) return false;

    SetWindowLongPtr(d->hwnd, GWLP_USERDATA, (LONG_PTR)d);

    /* Create offscreen DC and Cairo surface */
    { HDC screen = GetDC(d->hwnd);
      d->hdc = CreateCompatibleDC(screen);
      ReleaseDC(d->hwnd, screen); }

    { HDC tmp = GetDC(d->hwnd);
      d->hbmp = CreateCompatibleBitmap(tmp, w, h);
      ReleaseDC(d->hwnd, tmp); }
    SelectObject(d->hdc, d->hbmp);

    d->surface = cairo_win32_surface_create_with_dib(CAIRO_FORMAT_RGB24, w, h);

    ShowWindow(d->hwnd, SW_SHOW);
    UpdateWindow(d->hwnd);
    return true;
}

static bool win32_resize(void *data, int w, int h) {
    Win32BackendData *d = (Win32BackendData *)data;

    if (d->surface) {
        cairo_surface_destroy(d->surface);
        d->surface = NULL;
    }

    /* Supprimer l'ancien bitmap et en créer un nouveau */
    if (d->hbmp) {
        SelectObject(d->hdc, (HBITMAP)GetStockObject(NULL_BRUSH)); /* désélectionner le bitmap */
        DeleteObject(d->hbmp);
    }
    { HDC tmp = GetDC(d->hwnd);
      d->hbmp = CreateCompatibleBitmap(tmp, w, h);
      ReleaseDC(d->hwnd, tmp); }
    SelectObject(d->hdc, d->hbmp);

    d->surface = cairo_win32_surface_create_with_dib(CAIRO_FORMAT_RGB24, w, h);
    d->width   = w;
    d->height  = h;
    /* Forcer le repaint sur toute la fenêtre */
    InvalidateRect(d->hwnd, NULL, TRUE);
    return d->surface != NULL;
}

static bool win32_poll_event(void *data, PotteryEvent *evt) {
    Win32BackendData *d = (Win32BackendData *)data;

    /* Dispatch pending Windows messages first */
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    /* Return one event from our queue */
    if (d->queue_head != d->queue_tail) {
        *evt = d->queue[d->queue_head];
        d->queue_head = (d->queue_head + 1) & 255;
        return true;
    }
    return false;
}

static void win32_present(void *data) {
    Win32BackendData *d = (Win32BackendData *)data;
    /* Flush Cairo avant BitBlt */
    cairo_surface_flush(d->surface);
    /* Pour DIB surface, on récupère le DC depuis Cairo */
    HDC src_dc = cairo_win32_surface_get_dc(d->surface);
    HDC wnd_dc = GetDC(d->hwnd);
    BitBlt(wnd_dc, 0, 0, d->width, d->height, src_dc, 0, 0, SRCCOPY);
    ReleaseDC(d->hwnd, wnd_dc);
}

/* UTF-8 ↔ CF_UNICODETEXT clipboard helpers */
static char *win32_clipboard_get(void *data) {
    (void)data;
    if (!OpenClipboard(NULL)) return NULL;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (!h) { CloseClipboard(); return NULL; }
    wchar_t *ws = (wchar_t *)GlobalLock(h);
    int len = WideCharToMultiByte(CP_UTF8, 0, ws, -1, NULL, 0, NULL, NULL);
    char *utf8 = malloc(len);
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, utf8, len, NULL, NULL);
    GlobalUnlock(h);
    CloseClipboard();
    return utf8; /* caller must free() */
}

static void win32_clipboard_set(void *data, const char *utf8) {
    (void)data;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    HANDLE h = GlobalAlloc(GMEM_MOVEABLE, wlen * sizeof(wchar_t));
    wchar_t *ws = (wchar_t *)GlobalLock(h);
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, ws, wlen);
    GlobalUnlock(h);
    OpenClipboard(NULL);
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, h);
    CloseClipboard();
}

static void win32_destroy(void *data) {
    Win32BackendData *d = (Win32BackendData *)data;
    if (d->surface) cairo_surface_destroy(d->surface);
    if (d->hbmp)    DeleteObject(d->hbmp);
    if (d->hdc)     DeleteDC(d->hdc);
    if (d->hwnd)    DestroyWindow(d->hwnd);
}

/* =========================================================================
 * Public factory
 * ========================================================================= */

static Win32BackendData g_win32_data; /* single window for now */

PotteryBackend *pottery_backend_win32_create(void) {
    static PotteryBackend backend = {
        .init          = win32_init,
        .resize        = win32_resize,
        .poll_event    = win32_poll_event,
        .present       = win32_present,
        .clipboard_get = win32_clipboard_get,
        .clipboard_set = win32_clipboard_set,
        .destroy       = win32_destroy,
    };
    memset(&g_win32_data, 0, sizeof(g_win32_data));
    backend.data = &g_win32_data;
    return &backend;
}
