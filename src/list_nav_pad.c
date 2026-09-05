#include "defines.h"
#include "api.h"

#include "list_nav.h"

// Note:
// - Up/Down use PAD_justRepeated (hold-to-repeat),
// - Left/Right use PAD_justPressed (no repeat)
ListNavInput ListNavPad_read(void) {
    ListNavInput in = LIST_NAV_NONE;
    if (PAD_justRepeated(BTN_UP))    in |= LIST_NAV_UP;
    if (PAD_justRepeated(BTN_DOWN))  in |= LIST_NAV_DOWN;
    if (PAD_justPressed(BTN_LEFT))   in |= LIST_NAV_LEFT;
    if (PAD_justPressed(BTN_RIGHT))  in |= LIST_NAV_RIGHT;
    return in;
}
