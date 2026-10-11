/*
 * QEMU SDL display driver -- opengl support
 *
 * Copyright (c) 2014 Red Hat
 *
 * Authors:
 *     Gerd Hoffmann <kraxel@redhat.com>
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

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/error-report.h"
#include "ui/console.h"
#include "ui/input.h"
#include "ui/sdl2.h"

#ifdef __LIMBO__
/* 定义在 ui/sdl2.c，0=拉伸至全屏 1=等比缩放 2=1:1，见那里的注释。 */
extern int limbo_sdl_scale_mode;

/* GL 后端直接控制 glViewport 来实现三种缩放模式。 */
static void limbo_sdl_gl_apply_viewport(struct sdl2_console *scon, int ww, int wh)
{
    int mode = limbo_sdl_scale_mode;
    int sw = surface_width(scon->surface);
    int sh = surface_height(scon->surface);

    if (mode < 0) {
        mode = 1;
    }
    if (sw <= 0 || sh <= 0 || ww <= 0 || wh <= 0) {
        return;
    }

    switch (mode) {
    case 0: /* 拉伸至全屏：忽略宽高比铺满 */
        glViewport(0, 0, ww, wh);
        break;
    case 2: { /* 1:1：原始像素居中，放不下就裁剪 */
        int w = MIN(sw, ww);
        int h = MIN(sh, wh);

        glViewport((ww - w) / 2, (wh - h) / 2, w, h);
        break;
    }
    case 1: /* 等比缩放（默认）：surface_gl_setup_viewport() 本身就是等比居中 */
    default:
        surface_gl_setup_viewport(scon->gls, scon->surface, ww, wh);
        break;
    }
}

/* 扫描输出（dmabuf 直通）路径的目标矩形，交给 egl_fb_blit() 使用。 */
static void limbo_sdl_gl_scanout_rect(struct sdl2_console *scon,
                                      int ww, int wh,
                                      int *x, int *y, int *w, int *h)
{
    int mode = limbo_sdl_scale_mode;
    int gw = scon->guest_fb.width;
    int gh = scon->guest_fb.height;

    *x = 0;
    *y = 0;
    *w = ww;
    *h = wh;

    if (mode < 0) {
        mode = 1;
    }
    if (gw <= 0 || gh <= 0 || ww <= 0 || wh <= 0) {
        return;
    }

    if (mode == 2) {
        *w = MIN(gw, ww);
        *h = MIN(gh, wh);
    } else if (mode != 0) {
        double scale = MIN((double)ww / gw, (double)wh / gh);

        *w = (int)(gw * scale + 0.5);
        *h = (int)(gh * scale + 0.5);
    }
    *x = (ww - *w) / 2;
    *y = (wh - *h) / 2;
}
#endif

static void sdl2_set_scanout_mode(struct sdl2_console *scon, bool scanout)
{
    if (scon->scanout_mode == scanout) {
        return;
    }

    scon->scanout_mode = scanout;
    if (!scon->scanout_mode) {
        egl_fb_destroy(&scon->guest_fb);
        if (scon->surface) {
            surface_gl_destroy_texture(scon->gls, scon->surface);
            surface_gl_create_texture(scon->gls, scon->surface);
        }
    }
}

static void sdl2_gl_render_surface(struct sdl2_console *scon)
{
    int ww, wh;

    SDL_GL_MakeCurrent(scon->real_window, scon->winctx);

    SDL_GetWindowSize(scon->real_window, &ww, &wh);
#ifdef __LIMBO__
    limbo_sdl_gl_apply_viewport(scon, ww, wh);
#else
    surface_gl_setup_viewport(scon->gls, scon->surface, ww, wh);
#endif

    surface_gl_render_texture(scon->gls, scon->surface);
    SDL_GL_SwapWindow(scon->real_window);
}

void sdl2_gl_update(DisplayChangeListener *dcl,
                    int x, int y, int w, int h)
{
    struct sdl2_console *scon = container_of(dcl, struct sdl2_console, dcl);

    assert(scon->opengl);

    if (!scon->real_window) {
        return;
    }

    SDL_GL_MakeCurrent(scon->real_window, scon->winctx);
    surface_gl_update_texture(scon->gls, scon->surface, x, y, w, h);
    scon->updates++;
}

void sdl2_gl_switch(DisplayChangeListener *dcl,
                    DisplaySurface *new_surface)
{
    struct sdl2_console *scon = container_of(dcl, struct sdl2_console, dcl);
    DisplaySurface *old_surface = scon->surface;

    assert(scon->opengl);

    SDL_GL_MakeCurrent(scon->real_window, scon->winctx);
    surface_gl_destroy_texture(scon->gls, scon->surface);

    scon->surface = new_surface;

    if (surface_is_placeholder(new_surface) && qemu_console_get_index(dcl->con)) {
        qemu_gl_fini_shader(scon->gls);
        scon->gls = NULL;
        sdl2_window_destroy(scon);
        return;
    }

    if (!scon->real_window) {
        sdl2_window_create(scon);
        scon->gls = qemu_gl_init_shader();
    } else if (old_surface &&
               ((surface_width(old_surface)  != surface_width(new_surface)) ||
                (surface_height(old_surface) != surface_height(new_surface)))) {
        sdl2_window_resize(scon);
    }

    surface_gl_create_texture(scon->gls, scon->surface);
}

void sdl2_gl_refresh(DisplayChangeListener *dcl)
{
    struct sdl2_console *scon = container_of(dcl, struct sdl2_console, dcl);

    assert(scon->opengl);

#ifdef __LIMBO__
    /* 运行中切换缩放模式时强制整屏重绘一次（客户机不一定有脏区域）。 */
    {
        static int limbo_last_scale_mode = -2;

        if (limbo_last_scale_mode != limbo_sdl_scale_mode) {
            limbo_last_scale_mode = limbo_sdl_scale_mode;
            scon->updates = 1;
            if (scon->scanout_mode && scon->real_window) {
                sdl2_gl_redraw(scon);
            }
        }
    }
#endif

    graphic_hw_update(dcl->con);
    if (scon->updates && scon->real_window) {
        scon->updates = 0;
        sdl2_gl_render_surface(scon);
    }
    sdl2_poll_events(scon);
}

void sdl2_gl_redraw(struct sdl2_console *scon)
{
    assert(scon->opengl);

    if (scon->scanout_mode) {
        /* sdl2_gl_scanout_flush actually only care about
         * the first argument. */
        return sdl2_gl_scanout_flush(&scon->dcl, 0, 0, 0, 0);
    }
    if (scon->surface) {
        sdl2_gl_render_surface(scon);
    }
}

QEMUGLContext sdl2_gl_create_context(DisplayGLCtx *dgc,
                                     QEMUGLParams *params)
{
    struct sdl2_console *scon = container_of(dgc, struct sdl2_console, dgc);
    SDL_GLContext ctx, current_ctx;

    assert(scon->opengl);

    current_ctx = SDL_GL_GetCurrentContext();

    SDL_GL_MakeCurrent(scon->real_window, scon->winctx);

    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    if (scon->opts->gl == DISPLAY_GL_MODE_ON ||
        scon->opts->gl == DISPLAY_GL_MODE_CORE) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_CORE);
    } else if (scon->opts->gl == DISPLAY_GL_MODE_ES) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_ES);
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, params->major_ver);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, params->minor_ver);

    ctx = SDL_GL_CreateContext(scon->real_window);

    /* If SDL fail to create a GL context and we use the "on" flag,
     * then try to fallback to GLES.
     */
    if (!ctx && scon->opts->gl == DISPLAY_GL_MODE_ON) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_ES);
        ctx = SDL_GL_CreateContext(scon->real_window);
    }

    SDL_GL_MakeCurrent(scon->real_window, current_ctx);

    return (QEMUGLContext)ctx;
}

void sdl2_gl_destroy_context(DisplayGLCtx *dgc, QEMUGLContext ctx)
{
    SDL_GLContext sdlctx = (SDL_GLContext)ctx;

    SDL_GL_DeleteContext(sdlctx);
}

int sdl2_gl_make_context_current(DisplayGLCtx *dgc,
                                 QEMUGLContext ctx)
{
    struct sdl2_console *scon = container_of(dgc, struct sdl2_console, dgc);
    SDL_GLContext sdlctx = (SDL_GLContext)ctx;

    assert(scon->opengl);

    return SDL_GL_MakeCurrent(scon->real_window, sdlctx);
}

void sdl2_gl_scanout_disable(DisplayChangeListener *dcl)
{
    struct sdl2_console *scon = container_of(dcl, struct sdl2_console, dcl);

    assert(scon->opengl);
    scon->w = 0;
    scon->h = 0;
    sdl2_set_scanout_mode(scon, false);
}

void sdl2_gl_scanout_texture(DisplayChangeListener *dcl,
                             uint32_t backing_id,
                             bool backing_y_0_top,
                             uint32_t backing_width,
                             uint32_t backing_height,
                             uint32_t x, uint32_t y,
                             uint32_t w, uint32_t h,
                             void *d3d_tex2d)
{
    struct sdl2_console *scon = container_of(dcl, struct sdl2_console, dcl);

    assert(scon->opengl);
    scon->x = x;
    scon->y = y;
    scon->w = w;
    scon->h = h;
    scon->y0_top = backing_y_0_top;

    SDL_GL_MakeCurrent(scon->real_window, scon->winctx);

    sdl2_set_scanout_mode(scon, true);
    egl_fb_setup_for_tex(&scon->guest_fb, backing_width, backing_height,
                         backing_id, false);
}

void sdl2_gl_scanout_flush(DisplayChangeListener *dcl,
                           uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    struct sdl2_console *scon = container_of(dcl, struct sdl2_console, dcl);
    int ww, wh;

    assert(scon->opengl);
    if (!scon->scanout_mode) {
        return;
    }
    if (!scon->guest_fb.framebuffer) {
        return;
    }

    SDL_GL_MakeCurrent(scon->real_window, scon->winctx);

    SDL_GetWindowSize(scon->real_window, &ww, &wh);
#ifdef __LIMBO__
    /* egl_fb_blit() 按 dst 的 x/y/width/height 做 blit，所以这里直接给出
     * 目标矩形即可实现拉伸 / 等比 / 1:1 三种留边方式。 */
    {
        int x, y, w, h;

        limbo_sdl_gl_scanout_rect(scon, ww, wh, &x, &y, &w, &h);
        egl_fb_setup_default(&scon->win_fb, w, h, x, y);
    }
#else
    egl_fb_setup_default(&scon->win_fb, ww, wh, 0, 0);
#endif
    egl_fb_blit(&scon->win_fb, &scon->guest_fb, !scon->y0_top);

    SDL_GL_SwapWindow(scon->real_window);
}

#ifdef CONFIG_GBM
void sdl2_gl_scanout_dmabuf(DisplayChangeListener *dcl,
                            QemuDmaBuf *dmabuf)
{
    struct sdl2_console *scon = container_of(dcl, struct sdl2_console, dcl);
    const int *fds;

    assert(scon->opengl);
    SDL_GL_MakeCurrent(scon->real_window, scon->winctx);

    egl_dmabuf_import_texture(dmabuf);
    if (!qemu_dmabuf_get_texture(dmabuf)) {
        fds = qemu_dmabuf_get_fds(dmabuf, NULL);
        error_report("%s: failed fd=%d", __func__, fds ? fds[0] : -1);
        return;
    }

    sdl2_gl_scanout_texture(dcl, qemu_dmabuf_get_texture(dmabuf), false,
                            qemu_dmabuf_get_width(dmabuf),
                            qemu_dmabuf_get_height(dmabuf),
                            0, 0,
                            qemu_dmabuf_get_width(dmabuf),
                            qemu_dmabuf_get_height(dmabuf),
                            NULL);

    if (qemu_dmabuf_get_allow_fences(dmabuf)) {
        scon->guest_fb.dmabuf = dmabuf;
    }
}

void sdl2_gl_release_dmabuf(DisplayChangeListener *dcl,
                            QemuDmaBuf *dmabuf)
{
    egl_dmabuf_release_texture(dmabuf);
}

bool sdl2_gl_has_dmabuf(DisplayChangeListener *dcl)
{
    struct sdl2_console *scon = container_of(dcl, struct sdl2_console, dcl);

    return scon->has_dmabuf;
}
#endif

void sdl2_gl_console_init(struct sdl2_console *scon)
{
    bool hidden = scon->hidden;

    scon->hidden = true;
    scon->surface = qemu_create_displaysurface(1, 1);
    sdl2_window_create(scon);

    /*
     * QEMU checks whether console supports dma-buf before switching
     * to the console.  To break this chicken-egg problem we pre-check
     * dma-buf availability beforehand using a dummy SDL window.
     */
    scon->has_dmabuf = qemu_egl_has_dmabuf();

    sdl2_window_destroy(scon);
    qemu_free_displaysurface(scon->surface);

    scon->surface = NULL;
    scon->hidden = hidden;
}
