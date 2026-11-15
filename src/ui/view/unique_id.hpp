#ifndef __UI_VIEW_UNIQUE_ID_HPP__
#define __UI_VIEW_UNIQUE_ID_HPP__

namespace ui {

/** Returns an unique id on every call. The first id is 1. */
inline int generateUniqueId() {
    static int lastId = 0;
    return ++lastId;
}

}  // namespace ui

#endif
