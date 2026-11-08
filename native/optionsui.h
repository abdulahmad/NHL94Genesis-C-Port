/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 abdulahmad and contributors
 *
 * The rules screen (optionsui.c).
 */
#ifndef OPTIONSUI_H
#define OPTIONSUI_H
#include <SDL.h>
#include "mdrt/platform_sdl2.h"
#include "mdrt/settings.h"

void options_ui_init(const mdrt_sdl_options *o, int *values, mdrt_settings *set, void *user, void (*save)(void));
int options_ui_active(void);
int options_ui_pages(void);
int options_ui_open(void *ctx);
void options_ui_key(int k);
void options_ui_button(int slot, int b);
void options_ui_draw(const SDL_Rect *dst);
#endif
