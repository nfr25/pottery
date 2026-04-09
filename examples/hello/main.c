/*
 * examples/hello/main.c — Minimal Pottery application.
 */

#include "pottery.h"
#include <stdio.h>
#include <string.h>

PotteryBackend *pottery_backend_win32_create(void);

/* =========================================================================
 * Icônes SVG embarquées
 * ========================================================================= */

static const char SVG_CHECK[] =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'"
    " width='16' height='16'>"
    "<path d='M2 8 L6 12 L14 4'"
    " stroke='#ffffffff' stroke-width='2' fill='none'"
    " stroke-linecap='round' stroke-linejoin='round'/>"
    "</svg>";

static const char SVG_MOON[] =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
    "<path d='M12 10 A6 6 0 1 1 6 4 A4 4 0 1 0 12 10 Z' "
    "fill='#555'/>"
    "</svg>";

static const char SVG_SUN[] =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
    "<circle cx='8' cy='8' r='3' fill='#f59e0b'/>"
    "<line x1='8' y1='1' x2='8' y2='3' stroke='#f59e0b' stroke-width='1.5' stroke-linecap='round'/>"
    "<line x1='8' y1='13' x2='8' y2='15' stroke='#f59e0b' stroke-width='1.5' stroke-linecap='round'/>"
    "<line x1='1' y1='8' x2='3' y2='8' stroke='#f59e0b' stroke-width='1.5' stroke-linecap='round'/>"
    "<line x1='13' y1='8' x2='15' y2='8' stroke='#f59e0b' stroke-width='1.5' stroke-linecap='round'/>"
    "<line x1='3' y1='3' x2='4.5' y2='4.5' stroke='#f59e0b' stroke-width='1.5' stroke-linecap='round'/>"
    "<line x1='11.5' y1='11.5' x2='13' y2='13' stroke='#f59e0b' stroke-width='1.5' stroke-linecap='round'/>"
    "<line x1='13' y1='3' x2='11.5' y2='4.5' stroke='#f59e0b' stroke-width='1.5' stroke-linecap='round'/>"
    "<line x1='4.5' y1='11.5' x2='3' y2='13' stroke='#f59e0b' stroke-width='1.5' stroke-linecap='round'/>"
    "</svg>";

static const char SVG_QUIT[] =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
    "<line x1='3' y1='3' x2='13' y2='13' stroke='#888' stroke-width='2' stroke-linecap='round'/>"
    "<line x1='13' y1='3' x2='3' y2='13' stroke='#888' stroke-width='2' stroke-linecap='round'/>"
    "</svg>";

/* =========================================================================
 * Application state
 * ========================================================================= */

typedef struct {
    char  name[128];
    int   click_count;
    bool  dark_mode;
    int   language;
    int   selected_file;
} AppState;

/* =========================================================================
 * UI
 * ========================================================================= */

static void build_ui(PotteryKiln *kiln, AppState *app,
                     PotteryIcon *icon_check,
                     PotteryIcon *icon_moon,
                     PotteryIcon *icon_sun,
                     PotteryIcon *icon_quit) {

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
        pottery_mold_edit(kiln, "name_edit", app->name, sizeof(app->name),
            &(PotteryEditOpts){
                .base.width  = POTTERY_GROW(),
                .placeholder = "Tapez votre nom...",
            });

        PotteryVesselDesc row_desc = {
            .id        = "btn_row",
            .direction = POTTERY_DIRECTION_ROW,
            .width     = POTTERY_GROW(),
            .gap       = 8,
        };

        POTTERY_VESSEL(kiln, &row_desc) {

            /* Bouton Greet avec icône check */
            PotteryButtonOpts greet_opts = {
                .base.width     = POTTERY_FIXED(100),
                .base.icon      = icon_check,
                .default_action = true,
            };
            if (pottery_mold_button(kiln, "greet_btn", "Greet", &greet_opts))
                app->click_count++;

            /* Bouton theme avec icône lune/soleil */
            PotteryButtonOpts theme_opts = {
                .base.width = POTTERY_FIXED(120),
                .base.icon  = app->dark_mode ? icon_sun : icon_moon,
            };
            if (pottery_mold_button(kiln, "theme_btn",
                    app->dark_mode ? "Light mode" : "Dark mode", &theme_opts))
                app->dark_mode = !app->dark_mode;

            pottery_mold_spacer(kiln, 0);

            /* Bouton Quit avec icône X */
            PotteryButtonOpts quit_opts = {
                .base.width = POTTERY_FIXED(70),
                .base.icon  = icon_quit,
            };
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
            snprintf(greeting, sizeof(greeting),
                "Hello, stranger! (clicked %d time%s)",
                app->click_count, app->click_count == 1 ? "" : "s");
        else
            snprintf(greeting, sizeof(greeting),
                "Enter your name and click Greet.");

        PotteryLabelOpts gl = { .base.width = POTTERY_GROW(), .wrap = true };
        pottery_mold_label(kiln, "greeting", greeting, &gl);

        /* Combo */
        static const char *languages[] = { "C", "C++", "Rust", "Zig", "Odin" };
        PotteryComboOpts combo_opts = { .base.width = POTTERY_FIXED(120) };
        pottery_mold_combo(kiln, "lang_combo",
            languages, 5, &app->language, &combo_opts);

        pottery_mold_separator(kiln, true);

        /* List view avec PotteryTableModel */
        /* List view multi-colonnes */
        static const char *files[] = {
            "pottery_kiln.c",    "Core",   "Contexte principal",
            "pottery_state.c",   "Core",   "State map widgets",
            "pottery_renderer.c","Core",   "Cairo render commands",
            "pottery_text.c",    "Core",   "Pango bridge",
            "pottery_svg.c",     "Core",   "librsvg icons",
            "pottery_input.c",   "Core",   "Input par frame",
            "pottery_vessel.c",  "Layout", "Clay containers",
            "pottery_glaze.c",   "Theme",  "Light et dark",
            "pottery_button.c",  "Mold",   "Bouton cliquable",
            "pottery_label.c",   "Mold",   "Texte + utilitaires",
            "pottery_edit.c",    "Mold",   "Champ de saisie",
            "pottery_combo.c",   "Mold",   "Dropdown",
            "pottery_list.c",    "Mold",   "Liste virtualisee",
        };
        static const char *headers[] = { "Fichier", "Categorie", "Description" };
        static PotteryTableModel table_model;
        static bool model_init = false;
        if (!model_init) {
            pottery_table_model_init(&table_model, files, headers, 13, 3);
            model_init = true;
        }

        static PotteryListColumn cols[] = {
            { "Fichier",     150.0f, POTTERY_ALIGN_START },
            { "Categorie",    80.0f, POTTERY_ALIGN_START },
            { "Description",   0.0f, POTTERY_ALIGN_START }, /* 0 = GROW */
        };

        PotteryListOpts list_opts = {
            .base.width    = POTTERY_GROW(),
            .base.height   = POTTERY_GROW(),
            .model         = &table_model.base,
            .columns       = cols,
            .column_count  = 3,
            .show_header   = true,
        };
        pottery_mold_list(kiln, "file_list", &app->selected_file, &list_opts);
    }
}

/* =========================================================================
 * Entry point
 * ========================================================================= */

int main(void) {
    PotteryBackend *backend = pottery_backend_win32_create();
    PotteryGlaze light = pottery_glaze_light();

    PotteryKiln *kiln = pottery_kiln_create(&(PotteryKilnDesc){
        .title   = "Hello - Pottery",
        .width   = 520,
        .height  = 320,
        .backend = backend,
        .glaze   = &light,
    });
    if (!kiln) return 1;

    /* Charger les icônes depuis les SVG embarqués */
    PotteryIcon *icon_check = pottery_kiln_load_icon_data(
        kiln, "check", SVG_CHECK, sizeof(SVG_CHECK) - 1);
    PotteryIcon *icon_moon  = pottery_kiln_load_icon_data(
        kiln, "moon",  SVG_MOON,  sizeof(SVG_MOON)  - 1);
    PotteryIcon *icon_sun   = pottery_kiln_load_icon_data(
        kiln, "sun",   SVG_SUN,   sizeof(SVG_SUN)   - 1);
    PotteryIcon *icon_quit  = pottery_kiln_load_icon_data(
        kiln, "quit",  SVG_QUIT,  sizeof(SVG_QUIT)  - 1);

    fprintf(stderr, "Icons: check=%p moon=%p sun=%p quit=%p\n",
        (void*)icon_check, (void*)icon_moon,
        (void*)icon_sun,   (void*)icon_quit);

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
        build_ui(kiln, &app, icon_check, icon_moon, icon_sun, icon_quit);
        pottery_kiln_end_frame(kiln);
    }

    pottery_kiln_destroy(kiln);
    return 0;
}
