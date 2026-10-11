/*
 * AGL input path.
 *
 * The AGL display backend (ui/agl.c) has no SDL/GTK window, so there is no
 * event source to poll: the app pushes touch, scroll and key events here and
 * they are queued onto the QEMU main loop, which then feeds them into the
 * input subsystem exactly like the other display backends do.
 *
 * Adapted from the ALS reference implementation (AnyLaySys/qemu-gunyah,
 * system/jni.c).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"

#ifdef __ANDROID__

#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "ui/agl.h"
#include "ui/console.h"
#include "ui/input.h"

enum {
    AGL_INPUT_POINTER,
    AGL_INPUT_SCROLL,
    AGL_INPUT_KEY,
};

typedef struct AglInput {
    int type;
    float x, y;
    int value;
    bool down;
    QTAILQ_ENTRY(AglInput) next;
} AglInput;

static struct {
    pthread_mutex_t lock;
    QTAILQ_HEAD(, AglInput) queue;
    bool scheduled;
    uint32_t buttons;
    int x, y;
} agl_input = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .queue = QTAILQ_HEAD_INITIALIZER(agl_input.queue),
};

static void agl_input_send(AglInput *event)
{
    static uint32_t map[INPUT_BUTTON__MAX] = {
        [INPUT_BUTTON_LEFT] = 1, [INPUT_BUTTON_RIGHT] = 2,
        [INPUT_BUTTON_MIDDLE] = 4, [INPUT_BUTTON_SIDE] = 8,
        [INPUT_BUTTON_EXTRA] = 16,
    };
    QemuConsole *console = qemu_console_lookup_default();

    if (!console) {
        return;
    }
    if (event->type == AGL_INPUT_POINTER) {
        int x, y, width, height;

        if (!agl_map(event->x, event->y, &x, &y, &width, &height)) {
            return;
        }
        qemu_input_update_buttons(console, map, agl_input.buttons, event->value);
        if (qemu_input_is_absolute(console)) {
            qemu_input_queue_abs(console, INPUT_AXIS_X, x, 0, width);
            qemu_input_queue_abs(console, INPUT_AXIS_Y, y, 0, height);
        } else {
            qemu_input_queue_rel(console, INPUT_AXIS_X, x - agl_input.x);
            qemu_input_queue_rel(console, INPUT_AXIS_Y, y - agl_input.y);
        }
        agl_input.buttons = event->value;
        agl_input.x = x;
        agl_input.y = y;
        qemu_input_event_sync();
    } else if (event->type == AGL_INPUT_SCROLL) {
        InputButton v = event->y < 0 ? INPUT_BUTTON_WHEEL_UP :
                                       INPUT_BUTTON_WHEEL_DOWN;
        InputButton h = event->x < 0 ? INPUT_BUTTON_WHEEL_LEFT :
                                       INPUT_BUTTON_WHEEL_RIGHT;

        if (event->y) {
            qemu_input_queue_btn(console, v, true);
            qemu_input_event_sync();
            qemu_input_queue_btn(console, v, false);
            qemu_input_event_sync();
        }
        if (event->x) {
            qemu_input_queue_btn(console, h, true);
            qemu_input_event_sync();
            qemu_input_queue_btn(console, h, false);
            qemu_input_event_sync();
        }
    } else {
        int qcode = qemu_input_linux_to_qcode(event->value);

        if (qcode != Q_KEY_CODE_UNMAPPED) {
            qemu_input_event_send_key_qcode(console, qcode, event->down);
        }
    }
}

static void agl_input_bh(void *opaque)
{
    AglInput *event;

    (void)opaque;
    for (;;) {
        pthread_mutex_lock(&agl_input.lock);
        event = QTAILQ_FIRST(&agl_input.queue);
        if (event) {
            QTAILQ_REMOVE(&agl_input.queue, event, next);
        } else {
            agl_input.scheduled = false;
        }
        pthread_mutex_unlock(&agl_input.lock);
        if (!event) {
            return;
        }
        agl_input_send(event);
        g_free(event);
    }
}

static void agl_input_push(int type, float x, float y, int value, bool down)
{
    AglInput *event = g_new(AglInput, 1);
    bool schedule;

    *event = (AglInput) { type, x, y, value, down };
    pthread_mutex_lock(&agl_input.lock);
    QTAILQ_INSERT_TAIL(&agl_input.queue, event, next);
    schedule = !agl_input.scheduled;
    agl_input.scheduled = true;
    pthread_mutex_unlock(&agl_input.lock);
    if (schedule) {
        aio_bh_schedule_oneshot(qemu_get_aio_context(), agl_input_bh, NULL);
    }
}

/* Entry points used by liblimbo.so (see jni/limbo/vm-executor-jni.c). */

void limbo_agl_pointer(float x, float y, int buttons)
{
    agl_input_push(AGL_INPUT_POINTER, x, y, buttons, false);
}

void limbo_agl_scroll(float x, float y)
{
    agl_input_push(AGL_INPUT_SCROLL, x, y, 0, false);
}

void limbo_agl_key(int scan_code, bool down)
{
    agl_input_push(AGL_INPUT_KEY, 0, 0, scan_code, down);
}

void limbo_agl_input_clear(void)
{
    AglInput *event;

    pthread_mutex_lock(&agl_input.lock);
    while ((event = QTAILQ_FIRST(&agl_input.queue))) {
        QTAILQ_REMOVE(&agl_input.queue, event, next);
        g_free(event);
    }
    agl_input.scheduled = false;
    agl_input.buttons = agl_input.x = agl_input.y = 0;
    pthread_mutex_unlock(&agl_input.lock);
}

#endif /* __ANDROID__ */
