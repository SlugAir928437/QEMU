/*
 * QEMU SDL display driver
 *
 * Copyright (c) 2003 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
/* Ported SDL 1.2 code to 2.0 by Dave Airlie. */

#include "qemu/osdep.h"
#include "ui/console.h"
#include "ui/input.h"
#include "ui/sdl2.h"

#ifdef __LIMBO__
/* 定义在 ui/sdl2.c，0=拉伸至全屏 1=等比缩放 2=1:1，见那里的注释。 */
extern int limbo_sdl_scale_mode;

/*
 * 计算把客户机画面（surface 尺寸）画到渲染输出（窗口尺寸）上的目标矩形。
 * 注意：这里不再使用 SDL_RenderSetLogicalSize()，因为 SDL 的 logical size
 * 只能做等比缩放；三种模式统一由本函数算出矩形，交给 SDL_RenderCopy()。
 */
static void limbo_sdl_scale_dst_rect(struct sdl2_console *scon, SDL_Rect *dst)
{
    int sw = surface_width(scon->surface);
    int sh = surface_height(scon->surface);
    int ow = 0, oh = 0;
    int mode = limbo_sdl_scale_mode;

    SDL_GetRendererOutputSize(scon->real_renderer, &ow, &oh);
    if (ow <= 0 || oh <= 0 || sw <= 0 || sh <= 0) {
        /* 窗口还没就绪：原样输出，等下一帧再算 */
        dst->x = 0;
        dst->y = 0;
        dst->w = sw > 0 ? sw : ow;
        dst->h = sh > 0 ? sh : oh;
        return;
    }

    if (mode < 0) {
        mode = 1;
    }

    dst->x = 0;
    dst->y = 0;
    dst->w = ow;
    dst->h = oh;

    switch (mode) {
    case 0: /* 拉伸至全屏：忽略宽高比铺满 */
        break;
    case 2: { /* 1:1：原始像素居中，放不下就裁剪 */
        int w = MIN(sw, ow);
        int h = MIN(sh, oh);

        dst->x = (ow - w) / 2;
        dst->y = (oh - h) / 2;
        dst->w = w;
        dst->h = h;
        break;
    }
    case 1: /* 等比缩放（默认）：取较小比例，居中留黑边 */
    default: {
        double scale = MIN((double)ow / sw, (double)oh / sh);
        int w = (int)(sw * scale + 0.5);
        int h = (int)(sh * scale + 0.5);

        dst->x = (ow - w) / 2;
        dst->y = (oh - h) / 2;
        dst->w = w;
        dst->h = h;
        break;
    }
    }
}
#endif

void sdl2_2d_update(DisplayChangeListener *dcl,
                    int x, int y, int w, int h)
{
    struct sdl2_console *scon = container_of(dcl, struct sdl2_console, dcl);
    DisplaySurface *surf = scon->surface;
    SDL_Rect rect;
    size_t surface_data_offset;
    assert(!scon->opengl);

    if (!scon->texture) {
        return;
    }

    surface_data_offset = surface_bytes_per_pixel(surf) * x +
                          surface_stride(surf) * y;
    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;

    SDL_UpdateTexture(scon->texture, &rect,
                      surface_data(surf) + surface_data_offset,
                      surface_stride(surf));
#ifdef __LIMBO__
    {
        SDL_Rect dst;

        limbo_sdl_scale_dst_rect(scon, &dst);
        /* 先刷黑底，等比缩放 / 1:1 模式下四周留边才是黑的 */
        SDL_SetRenderDrawColor(scon->real_renderer, 0, 0, 0, 255);
        SDL_RenderClear(scon->real_renderer);
        SDL_RenderCopy(scon->real_renderer, scon->texture, NULL, &dst);
    }
#else
    SDL_RenderClear(scon->real_renderer);
    SDL_RenderCopy(scon->real_renderer, scon->texture, NULL, NULL);
#endif
    SDL_RenderPresent(scon->real_renderer);
}

void sdl2_2d_switch(DisplayChangeListener *dcl,
                    DisplaySurface *new_surface)
{
    struct sdl2_console *scon = container_of(dcl, struct sdl2_console, dcl);
    DisplaySurface *old_surface = scon->surface;
    int format = 0;

    assert(!scon->opengl);

    scon->surface = new_surface;

    if (scon->texture) {
        SDL_DestroyTexture(scon->texture);
        scon->texture = NULL;
    }

    if (surface_is_placeholder(new_surface) && qemu_console_get_index(dcl->con)) {
        sdl2_window_destroy(scon);
        return;
    }

    if (!scon->real_window) {
        sdl2_window_create(scon);
    } else if (old_surface &&
               ((surface_width(old_surface)  != surface_width(new_surface)) ||
                (surface_height(old_surface) != surface_height(new_surface)))) {
        sdl2_window_resize(scon);
    }

#ifdef __LIMBO__
    /* 缩放交给 limbo_sdl_scale_dst_rect()：关掉 SDL 的 logical size，
     * 否则 RenderCopy 的坐标还会被 SDL 再按 logical size 缩放一次。 */
    SDL_RenderSetLogicalSize(scon->real_renderer, 0, 0);
#else
    SDL_RenderSetLogicalSize(scon->real_renderer,
                             surface_width(new_surface),
                             surface_height(new_surface));
#endif

    switch (surface_format(scon->surface)) {
    case PIXMAN_x1r5g5b5:
        format = SDL_PIXELFORMAT_ARGB1555;
        break;
    case PIXMAN_r5g6b5:
        format = SDL_PIXELFORMAT_RGB565;
        break;
    case PIXMAN_a8r8g8b8:
    case PIXMAN_x8r8g8b8:
        format = SDL_PIXELFORMAT_ARGB8888;
        break;
    case PIXMAN_a8b8g8r8:
    case PIXMAN_x8b8g8r8:
        format = SDL_PIXELFORMAT_ABGR8888;
        break;
    case PIXMAN_r8g8b8a8:
    case PIXMAN_r8g8b8x8:
        format = SDL_PIXELFORMAT_RGBA8888;
        break;
    case PIXMAN_b8g8r8x8:
        format = SDL_PIXELFORMAT_BGRX8888;
        break;
    case PIXMAN_b8g8r8a8:
        format = SDL_PIXELFORMAT_BGRA8888;
        break;
    default:
        g_assert_not_reached();
    }
    scon->texture = SDL_CreateTexture(scon->real_renderer, format,
                                      SDL_TEXTUREACCESS_STREAMING,
                                      surface_width(new_surface),
                                      surface_height(new_surface));
    sdl2_2d_redraw(scon);
}

void sdl2_2d_refresh(DisplayChangeListener *dcl)
{
    struct sdl2_console *scon = container_of(dcl, struct sdl2_console, dcl);

    assert(!scon->opengl);
#ifdef __LIMBO__
    /* 用户在运行中切换缩放模式时，客户机不一定有脏区域，
     * 这里检测到模式变化就整屏重绘一次。 */
    {
        static int limbo_last_scale_mode = -2;

        if (limbo_last_scale_mode != limbo_sdl_scale_mode) {
            limbo_last_scale_mode = limbo_sdl_scale_mode;
            sdl2_2d_redraw(scon);
        }
    }
#endif
    graphic_hw_update(dcl->con);
    sdl2_poll_events(scon);
}

void sdl2_2d_redraw(struct sdl2_console *scon)
{
    assert(!scon->opengl);

    if (!scon->surface) {
        return;
    }
    sdl2_2d_update(&scon->dcl, 0, 0,
                   surface_width(scon->surface),
                   surface_height(scon->surface));
}

bool sdl2_2d_check_format(DisplayChangeListener *dcl,
                          pixman_format_code_t format)
{
    /*
     * We let SDL convert for us a few more formats than,
     * the native ones. These are the ones I have tested.
     */
    return (format == PIXMAN_x8r8g8b8 ||
            format == PIXMAN_a8r8g8b8 ||
            format == PIXMAN_a8b8g8r8 ||
            format == PIXMAN_x8b8g8r8 ||
            format == PIXMAN_b8g8r8x8 ||
            format == PIXMAN_b8g8r8a8 ||
            format == PIXMAN_r8g8b8x8 ||
            format == PIXMAN_r8g8b8a8 ||
            format == PIXMAN_x1r5g5b5 ||
            format == PIXMAN_r5g6b5);
}
