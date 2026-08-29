#ifndef __DISPLAY_HELPER_H__
#define __DISPLAY_HELPER_H__

struct SDL_Surface;

typedef struct DisplayContext DisplayContext;
typedef void (*DisplayRecreatedCallback)(void);

DisplayContext* DisplayHelper_init(struct SDL_Surface* screen);

// The context created by DisplayHelper_init(), for code that runs its own frame
// loop without being handed one (such as the on-screen keyboard).
DisplayContext* DisplayHelper_current(void);

// The current screen surface, for drawing code.
// Could be NULL while the display is released for an external binary (see DisplayHelper_recoverDisplay).
struct SDL_Surface* DisplayHelper_getSurface(const DisplayContext* display);

// Screen dimensions. Valid at any time, including while the display is released.
int DisplayHelper_getWidth(const DisplayContext* display);
int DisplayHelper_getHeight(const DisplayContext* display);

int DisplayHelper_addRecreatedCallback(DisplayRecreatedCallback callback);
void DisplayHelper_removeRecreatedCallback(DisplayRecreatedCallback callback);

// Release the display before launching an external UI-driving program.
// Some platforms like TG5050 need the release to avoid a DRM master conflict.
// Must be paired with DisplayHelper_recoverDisplay() to rebuild the SDL layer.
void DisplayHelper_prepareForExternal(void);

// Restore the display after the external binary execution.
void DisplayHelper_recoverDisplay(void);

#endif
