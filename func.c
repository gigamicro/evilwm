/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Bindable functions

// All functions present the same call interface, so they can be mapped
// reasonably arbitrarily to user inputs.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "bind.h"
#include "client.h"
#include "display.h"
#include "evilwm.h"
#include "func.h"
#include "list.h"
#include "log.h"
#include "screen.h"
#include "util.h"

static void do_client_move(struct client *c) {
	if (abs(c->x) == c->border && c->oldw != 0)
		c->x = 0;
	if (abs(c->y) == c->border && c->oldh != 0)
		c->y = 0;
	if (c->width < c->min_width) {
		c->width = c->min_width;
	}
	if (c->max_width && c->width > c->max_width) {
		c->width = c->max_width;
	}
	if (c->height < c->min_height) {
		c->height = c->min_height;
	}
	if (c->max_height && c->height > c->max_height) {
		c->height = c->max_height;
	}
	client_moveresizeraise(c);
#ifdef WARP_POINTER
	setmouse(c->window, c->width + c->border - 1, c->height + c->border - 1);
#endif
#ifdef MOVERESIZE_DISCARDENTERS
	discard_enter_events(c);
#endif
}

void func_delete(void *sptr, XEvent *e, unsigned flags) {
	if (!(flags & FL_CLIENT))
		return;
	struct client *c = sptr;
	(void)e;
	send_wm_delete(c, flags & FL_VALUEMASK);
}

void func_dock(void *sptr, XEvent *e, unsigned flags) {
	if (!(flags & FL_SCREEN))
		return;
	struct screen *current_screen = sptr;
	(void)e;
	if (flags & FL_TOGGLE) {
		set_docks_visible(current_screen, !current_screen->docks_visible);
	}
}

void func_info(void *sptr, XEvent *e, unsigned flags) {
	if (!(flags & FL_CLIENT))
		return;
	struct client *c = sptr;
	client_show_info(c, e);
}

void func_lower(void *sptr, XEvent *e, unsigned flags) {
	struct client *c = sptr;
	if (!(flags & FL_CLIENT) || !c)
		return;
	(void)e;
	client_lower(c);
}

void func_move(void *sptr, XEvent *e, unsigned flags) {
	struct client *c = sptr;
	if (!(flags & FL_CLIENT) || !c)
		return;

	struct monitor *monitor = client_monitor(c, NULL);
	int width_inc = (c->width_inc > 1) ? c->width_inc : option.kbpx;
	int height_inc = (c->height_inc > 1) ? c->height_inc : option.kbpx;

	if (e->type == ButtonPress) {
		XButtonEvent *xbutton = (XButtonEvent *)e;
		client_move_drag(c, xbutton->button);
		return;
	}

	if (flags & FL_RELATIVE) {
		if (flags & FL_RIGHT) c->x += width_inc;
		if (flags & FL_LEFT ) c->x -= width_inc;
		if (flags & FL_DOWN ) c->y += height_inc;
		if (flags & FL_UP   ) c->y -= height_inc;
	} else {
		if (flags & FL_RIGHT ) c->x = monitor->x + monitor->width  - c->width  - c->border;
		if (flags & FL_LEFT  ) c->x = monitor->x +                               c->border;
		if (flags & FL_BOTTOM) c->y = monitor->y + monitor->height - c->height - c->border;
		if (flags & FL_TOP   ) c->y = monitor->y +                               c->border;
	}

	do_client_move(c);
#if !defined(MOVERESIZE_DISCARDENTERS) && defined(KBMOVERESIZE_DISCARDENTERS)
	discard_enter_events(c);
#endif
#if !defined(WARP_POINTER) && defined(KBMOVERESIZE_WARP_POINTER)
	// setmouse(c->window, c->width + c->border - 1, c->height + c->border - 1);
	setmouse(c->window, c->width/2, c->height/2);
#endif
}

void func_next(void *sptr, XEvent *e, unsigned flags) {
	(void)sptr;
	(void)flags;
	if (e->type != KeyPress)
		return;
	XKeyEvent *xkey = (XKeyEvent *)e;
	client_select_next();
	if (XGrabKeyboard(display.dpy, xkey->root, False, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess) {
		XEvent ev;
		do {
			XMaskEvent(display.dpy, KeyPressMask|KeyReleaseMask, &ev);
			if (ev.type == KeyPress && ev.xkey.keycode == xkey->keycode)
				client_select_next();
		} while (ev.type == KeyPress || ev.xkey.keycode == xkey->keycode);
		XUngrabKeyboard(display.dpy, CurrentTime);
	}
	clients_tab_order = list_to_head(clients_tab_order, current);
}

void func_raise(void *sptr, XEvent *e, unsigned flags) {
	struct client *c = sptr;
	if (!(flags & FL_CLIENT) || !c)
		return;
	(void)e;
	client_raise(c);
}

void func_resize(void *sptr, XEvent *e, unsigned flags) {
	struct client *c = sptr;
	if (!(flags & FL_CLIENT) || !c)
		return;

	if (e->type == ButtonPress && !(flags & FL_TOGGLE)) {
		client_resize_sweep(c, e->xbutton.button);
		return;
	}

	int width_inc = (c->width_inc > 1) ? c->width_inc : option.kbpx;
	int height_inc = (c->height_inc > 1) ? c->height_inc : option.kbpx;

	if (flags & FL_RELATIVE) {
		if (flags & FL_RIGHT) {
			c->width += width_inc;
		}
		if (flags & FL_LEFT) {
			c->width -= width_inc;
		}
		if (flags & FL_DOWN) {
			c->height += height_inc;
		}
		if (flags & FL_UP) {
			c->height -= height_inc;
		}
	} else if (flags & FL_TOGGLE) {
		int hv = 0;
		if (flags & FL_HORZ) {
			hv |= MAXIMISE_HORZ;
		}
		if (flags & FL_VERT) {
			hv |= MAXIMISE_VERT;
		}
		client_maximise(c, NET_WM_STATE_TOGGLE, hv);
		return;
	}

	do_client_move(c);
#if !defined(MOVERESIZE_DISCARDENTERS) && defined(KBMOVERESIZE_DISCARDENTERS)
	discard_enter_events(c);
#endif
#if !defined(WARP_POINTER) && defined(KBMOVERESIZE_WARP_POINTER)
	// setmouse(c->window, c->width + c->border - 1, c->height + c->border - 1);
	setmouse(c->window, c->width/2, c->height/2);
#endif
}

void func_spawn(void *sptr, XEvent *e, unsigned flags) {
	(void)sptr;
	(void)e;
	(void)flags;
	spawn((const char *const *)option.term);
}

void func_vdesk(void *sptr, XEvent *e, unsigned flags) {
	(void)e;
	if (flags & FL_CLIENT) {
		struct client *c = sptr;
		if (flags & FL_TOGGLE) {
			if (is_fixed(c)) {
				client_to_vdesk(c, c->screen->vdesk);
			} else {
				client_to_vdesk(c, VDESK_FIXED);
			}
		}
	} else if (flags & FL_SCREEN) {
		struct screen *scr = sptr;
		if (flags & FL_TOGGLE) {
			switch_vdesk(scr, scr->old_vdesk);
		} else if (flags & FL_RELATIVE) {
			unsigned mod = option.modvdesks?option.modvdesks:option.vdesks;// + 1;
			unsigned v = scr->vdesk % mod;
			unsigned h = scr->vdesk / mod;
			unsigned v_max = mod;
			unsigned h_max = option.vdesks / mod;
			if (flags & FL_UP   ) v ++;
			if (flags & FL_DOWN ) v += v_max-1;
			v %= v_max;
			if (flags & FL_LEFT ) h += h_max-1;
			if (flags & FL_RIGHT) h ++;
			h %= h_max;
			switch_vdesk(scr, h * mod + v);
		} else {
			switch_vdesk(scr, flags & FL_VALUEMASK);
		}
	}
}

void func_binds(void *sptr, XEvent *e, unsigned flags) {
	(void)e;
	if (!(flags & (FL_TOGGLE | FL_UP | FL_DOWN)) || !(flags & FL_SCREEN)) {LOG_DEBUG("func_binds abort due to invalid flags\n");return;}
	// flags & ? for on/off bind?
	if (flags & FL_TOGGLE) togglebinds(sptr);
	else if (flags & FL_UP) unstashbinds(sptr);
	else if (flags & FL_DOWN) stashbinds(sptr);
	else LOG_DEBUG("func_binds invalid flags\n");
}
