#ifndef __LIST_NAV_PAD_H__
#define __LIST_NAV_PAD_H__

#include "list_nav.h"

// Decode this frame's platform D-pad state into navigation intents.
ListNavInput ListNavPad_read(void);

#endif
