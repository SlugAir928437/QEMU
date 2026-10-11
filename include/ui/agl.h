#ifndef QEMU_UI_AGL_H
#define QEMU_UI_AGL_H

#ifdef __ANDROID__
#include <android/native_window.h>

/* ALS Graphics Layer: display presentation only. */
void agl_set_window(ANativeWindow *window, uint32_t refresh_rate);
bool agl_map(float x, float y, int *px, int *py, int *width, int *height);
void agl_cleanup(void);

/* Input path (ui/agl-input.c): called by liblimbo.so through dlsym(), since
 * the AGL display has no SDL/GTK window to source events from. */
void limbo_agl_pointer(float x, float y, int buttons);
void limbo_agl_scroll(float x, float y);
void limbo_agl_key(int scan_code, bool down);
void limbo_agl_input_clear(void);
#endif

#endif
