/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Keyboard and button function definitions

#ifndef EVILWM_FUNC_H_
#define EVILWM_FUNC_H_

#include <X11/X.h>
#include <X11/Xlib.h>

void func_handle_key(XKeyEvent *e);
void func_handle_button(XButtonEvent *e);

#endif
