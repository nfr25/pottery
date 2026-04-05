/*
 * examples/hello/main.c — Minimal Pottery application.
 */

#include "pottery.h"
#include <stdio.h>
#include <string.h>

PotteryBackend *pottery_backend_win32_create(void);

typedef struct {
    char  name[128];
    int   click_count;
    bool  dark_mode;
} AppState;

static void build_ui(PotteryKiln *kiln, AppState *app) {

    /* Les compound literals multi-lignes ne passent pas dans les macros
     * (les virgules sont interprétées comme séparateurs d'arguments).
     * On déclare les desc en variables locales. */

    PotteryVesselDesc root_desc = {
        .id        = "root",
        .direction = POTTERY_DIRECTION_COLUMN,
        .width     = POTTERY_GROW(),
        .height    = POTTERY_GROW(),
        .padding_x = 16,
        .padding_y = 16,
        .gap       = 10,
    };

    POTTERY_VESSEL(kiln, &root_desc) {

        pottery_mold_label(kiln, "title", "Hello, Pottery!",
            &(PotteryLabelOpts){ .base.width = POTTERY_GROW() });

        pottery_mold_separator(kiln, true);

        pottery_mold_label(kiln, "name_lbl", "Your name:", NULL);

        /* pottery_mold_edit — sera activé quand pottery_edit.c sera écrit */
        pottery_mold_label(kiln, "name_placeholder", app->name[0] ? app->name : "( edit a venir )", NULL);

        PotteryVesselDesc row_desc = {
            .id        = "btn_row",
            .direction = POTTERY_DIRECTION_ROW,
            .width     = POTTERY_GROW(),
            .gap       = 8,
        };

        POTTERY_VESSEL(kiln, &row_desc) {

            PotteryButtonOpts greet_opts = {
                .base.width     = POTTERY_FIXED(80),
                .default_action = true,
            };
            if (pottery_mold_button(kiln, "greet_btn", "Greet", &greet_opts))
                app->click_count++;

            PotteryButtonOpts theme_opts = { .base.width = POTTERY_FIT() };
            if (pottery_mold_button(kiln, "theme_btn",
                    app->dark_mode ? "Light mode" : "Dark mode", &theme_opts))
                app->dark_mode = !app->dark_mode;

            pottery_mold_spacer(kiln, 0);

            PotteryButtonOpts quit_opts = { .base.width = POTTERY_FIXED(60) };
            pottery_mold_button(kiln, "quit_btn", "Quit", &quit_opts);
        }

        pottery_mold_separator(kiln, true);

        char greeting[160] = {0};
        if (app->click_count > 0 && app->name[0])
            snprintf(greeting, sizeof(greeting),
                "Hello, %s! (clicked %d time%s)",
                app->name, app->click_count,
                app->click_count == 1 ? "" : "s");
        else if (app->click_count > 0)
            snprintf(greeting, sizeof(greeting), "Hello, stranger! (clicked %d time%s)",
                app->click_count, app->click_count == 1 ? "" : "s");
        else
            snprintf(greeting, sizeof(greeting), "Enter your name and click Greet.");

        PotteryLabelOpts gl = { .base.width = POTTERY_GROW(), .wrap = true };
        pottery_mold_label(kiln, "greeting", greeting, &gl);
    }
}

int main(void) {
    PotteryBackend *backend = pottery_backend_win32_create();

    PotteryGlaze light = pottery_glaze_light();

    PotteryKiln *kiln = pottery_kiln_create(&(PotteryKilnDesc){
        .title   = "Hello - Pottery",
        .width   = 480,
        .height  = 320,
        .backend = backend,
        .glaze   = &light,
    });

    if (!kiln) return 1;

    AppState app = {0};
    PotteryGlaze glazes[2];
    glazes[0] = pottery_glaze_light();
    glazes[1] = pottery_glaze_dark();
    bool last_dark = false;

    while (pottery_kiln_begin_frame(kiln)) {
        if (app.dark_mode != last_dark) {
            pottery_kiln_set_glaze(kiln, &glazes[app.dark_mode ? 1 : 0]);
            last_dark = app.dark_mode;
        }
        build_ui(kiln, &app);
        pottery_kiln_end_frame(kiln);
    }

    pottery_kiln_destroy(kiln);
    return 0;
}
