/*
 * pottery_input.c — Per-frame input state management.
 *
 * pottery_input_reset_frame() clears transient state (clicked, wheel, queues).
 * pottery_input_push_event()  translates a PotteryEvent into input state.
 *
 * The hot_id / focused_id are managed by the molds themselves:
 *   - hot_id    is set by pottery_mold_* when the pointer is inside a widget
 *   - focused_id is set on click, and cleared on Escape or click outside
 */

#include "pottery_internal.h"
#include <string.h>

void pottery_input_reset_frame(PotteryInput *input) {
    /* Clear single-frame flags */
    memset(input->mouse_clicked, 0, sizeof(input->mouse_clicked));
    input->wheel_dx   = 0.0f;
    input->wheel_dy   = 0.0f;
    input->char_count = 0;
    input->key_count  = 0;
}

void pottery_input_push_event(PotteryInput *input, const PotteryEvent *evt) {
    switch (evt->type) {

        case POTTERY_EVENT_MOUSE_MOVE:
            input->mouse_x = evt->mouse.x;
            input->mouse_y = evt->mouse.y;
            input->mods    = evt->mods;
            break;

        case POTTERY_EVENT_MOUSE_DOWN:
            if (evt->mouse.button >= 0 && evt->mouse.button < 3) {
                input->mouse_x                    = evt->mouse.x;
                input->mouse_y                    = evt->mouse.y;
                input->mouse_down[evt->mouse.button] = true;
                input->mods                       = evt->mods;
            }
            break;

        case POTTERY_EVENT_MOUSE_UP:
            if (evt->mouse.button >= 0 && evt->mouse.button < 3) {
                input->mouse_x                       = evt->mouse.x;
                input->mouse_y                       = evt->mouse.y;
                /* A click = press + release while still inside widget.
                 * We record the release; hit-testing in the mold decides
                 * if it counts as a click. */
                input->mouse_down[evt->mouse.button]    = false;
                input->mouse_clicked[evt->mouse.button] = true;
                input->mods                          = evt->mods;
            }
            break;

        case POTTERY_EVENT_MOUSE_WHEEL:
            input->wheel_dx += evt->wheel.dx;
            input->wheel_dy += evt->wheel.dy;
            input->mods      = evt->mods;
            break;

        case POTTERY_EVENT_KEY_DOWN:
            if (input->key_count < 16) {
                input->key_queue[input->key_count].key  = evt->keyboard.key;
                input->key_queue[input->key_count].mods = evt->mods;
                input->key_count++;
            }
            input->mods = evt->mods;
            break;

        case POTTERY_EVENT_CHAR:
            if (input->char_count < 16) {
                input->char_queue[input->char_count++] = evt->text.codepoint;
            }
            break;

        default:
            break;
    }
}
