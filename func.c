/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Keyboard and button function definitions
//
// Static lists of binds for now, but the point of having them like this is to
// enable configuration of key and button binds.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <X11/XKBlib.h>
#include <X11/keysymdef.h>

#include "client.h"
#include "display.h"
#include "evilwm.h"
#include "func.h"
#include "keymap.h"
#include "list.h"
#include "log.h"
#include "screen.h"
#include "util.h"

// Function flags
#define FL_VALUEMASK  (0xff)  // 8 bits of value
#define FL_UP       (1 << 8)
#define FL_DOWN     (1 << 9)
#define FL_LEFT     (1 << 10)
#define FL_RIGHT    (1 << 11)
#define FL_TOP      (1 << 12)
#define FL_BOTTOM   (1 << 13)
#define FL_RELATIVE (1 << 14)
#define FL_SCREEN   (1 << 15)
#define FL_CLIENT   (1 << 16)
#define FL_DRAG     (1 << 17)
#define FL_TOGGLE   (1 << 18)

#define FL_CORNERMASK  (FL_LEFT|FL_RIGHT|FL_TOP|FL_BOTTOM)
#define FL_TOPLEFT     (FL_LEFT|FL_TOP)
#define FL_TOPRIGHT    (FL_RIGHT|FL_TOP)
#define FL_BOTTOMLEFT  (FL_LEFT|FL_BOTTOM)
#define FL_BOTTOMRIGHT (FL_RIGHT|FL_BOTTOM)
#define FL_MAX         (FL_LEFT|FL_RIGHT|FL_TOP|FL_BOTTOM)
#define FL_VERT        (FL_TOP|FL_BOTTOM)
#define FL_HORZ        (FL_LEFT|FL_RIGHT)

// Modifier shorthand
#define MOD_C_A   (ControlMask|Mod1Mask)
#define MOD_C_A_S (ControlMask|Mod1Mask|ShiftMask)
#define MOD_A     (Mod1Mask)
#define MOD_S     (ShiftMask)

// Configurable modifier bits.  For client operations, grabmask2 is always
// allowed for button presses.
#define KEY_STATE_MASK ( ShiftMask | ControlMask | Mod1Mask | \
                         Mod2Mask | Mod3Mask | Mod4Mask | Mod5Mask )
#define BUTTON_STATE_MASK ( KEY_STATE_MASK & ~grabmask2 )

static void func_delete(void *, XEvent *, unsigned);
static void func_dock(void *, XEvent *, unsigned);
static void func_info(void *, XEvent *, unsigned);
static void func_lower(void *, XEvent *, unsigned);
static void func_move(void *, XEvent *, unsigned);
static void func_next(void *, XEvent *, unsigned);
static void func_resize(void *, XEvent *, unsigned);
static void func_spawn(void *, XEvent *, unsigned);
static void func_vdesk(void *, XEvent *, unsigned);

struct bind {
	// Bound key or button
	union {
		KeySym key;
		unsigned button;
	} control;
	// Modifier state
	unsigned state;
	// Dispatch function
	void (*func)(void *, XEvent *, unsigned);
	// Control flags
	unsigned flags;
};

// Lists of binds.  The first argument to the dispatch function is determined
// by the presence of FL_CLIENT or FL_SCREEN in the flags.  Other arguments are
// the XEvent that triggered the call, and the flags themselves.

static struct bind keyboard_controls[] = {
	// Move client
	{ { .key = KEY_UP },          MOD_C_A,   func_move,   FL_CLIENT|FL_RELATIVE|FL_UP },
	{ { .key = KEY_DOWN },        MOD_C_A,   func_move,   FL_CLIENT|FL_RELATIVE|FL_DOWN },
	{ { .key = KEY_LEFT },        MOD_C_A,   func_move,   FL_CLIENT|FL_RELATIVE|FL_LEFT },
	{ { .key = KEY_RIGHT },       MOD_C_A,   func_move,   FL_CLIENT|FL_RELATIVE|FL_RIGHT },
	{ { .key = KEY_TOPLEFT },     MOD_C_A,   func_move,   FL_CLIENT|FL_TOPLEFT },
	{ { .key = KEY_TOPRIGHT },    MOD_C_A,   func_move,   FL_CLIENT|FL_TOPRIGHT },
	{ { .key = KEY_BOTTOMLEFT },  MOD_C_A,   func_move,   FL_CLIENT|FL_BOTTOMLEFT },
	{ { .key = KEY_BOTTOMRIGHT }, MOD_C_A,   func_move,   FL_CLIENT|FL_BOTTOMRIGHT },

	// Resize client
	{ { .key = KEY_UP },          MOD_C_A_S, func_resize, FL_CLIENT|FL_RELATIVE|FL_UP },
	{ { .key = KEY_DOWN },        MOD_C_A_S, func_resize, FL_CLIENT|FL_RELATIVE|FL_DOWN },
	{ { .key = KEY_LEFT },        MOD_C_A_S, func_resize, FL_CLIENT|FL_RELATIVE|FL_LEFT },
	{ { .key = KEY_RIGHT },       MOD_C_A_S, func_resize, FL_CLIENT|FL_RELATIVE|FL_RIGHT },
	{ { .key = KEY_MAX },         MOD_C_A,   func_resize, FL_CLIENT|FL_TOGGLE|FL_MAX },
	{ { .key = KEY_MAXVERT },     MOD_C_A,   func_resize, FL_CLIENT|FL_TOGGLE|FL_VERT },
	{ { .key = KEY_MAXVERT },     MOD_C_A_S, func_resize, FL_CLIENT|FL_TOGGLE|FL_HORZ },

	// Client misc
	{ { .key = KEY_KILL },        MOD_C_A,   func_delete, FL_CLIENT|0 },  // 0 = delete
	{ { .key = KEY_KILL },        MOD_C_A_S, func_delete, FL_CLIENT|1 },  // 1 = kill
	{ { .key = KEY_INFO },        MOD_C_A,   func_info,   FL_CLIENT },
	{ { .key = KEY_LOWER },       MOD_C_A,   func_lower,  FL_CLIENT },
	{ { .key = KEY_ALTLOWER },    MOD_C_A,   func_lower,  FL_CLIENT },
	{ { .key = KEY_NEXT },        MOD_A,     func_next,   0 },
	{ { .key = KEY_NEW },         MOD_C_A,   func_spawn,  0 },
	{ { .key = KEY_FIX },         MOD_C_A,   func_vdesk,  FL_CLIENT|FL_TOGGLE },

	// Virtual desktops
	{ { .key = XK_1 },            MOD_C_A,   func_vdesk,  FL_SCREEN|0 },
	{ { .key = XK_2 },            MOD_C_A,   func_vdesk,  FL_SCREEN|1 },
	{ { .key = XK_3 },            MOD_C_A,   func_vdesk,  FL_SCREEN|2 },
	{ { .key = XK_4 },            MOD_C_A,   func_vdesk,  FL_SCREEN|3 },
	{ { .key = XK_5 },            MOD_C_A,   func_vdesk,  FL_SCREEN|4 },
	{ { .key = XK_6 },            MOD_C_A,   func_vdesk,  FL_SCREEN|5 },
	{ { .key = XK_7 },            MOD_C_A,   func_vdesk,  FL_SCREEN|6 },
	{ { .key = XK_8 },            MOD_C_A,   func_vdesk,  FL_SCREEN|7 },
	{ { .key = KEY_TOGGLEDESK },  MOD_C_A,   func_vdesk,  FL_SCREEN|FL_TOGGLE },
	{ { .key = KEY_PREVDESK },    MOD_C_A,   func_vdesk,  FL_SCREEN|FL_RELATIVE|FL_DOWN },
	{ { .key = KEY_NEXTDESK },    MOD_C_A,   func_vdesk,  FL_SCREEN|FL_RELATIVE|FL_UP },

	// Screen misc
	{ { .key = KEY_DOCK_TOGGLE }, MOD_C_A,   func_dock,   FL_SCREEN|FL_TOGGLE },
};
#define NUM_KEYBOARD_CONTROLS (int)(sizeof(keyboard_controls) / sizeof(keyboard_controls[0]))

static struct bind button_controls[] = {
	{ { .button = Button1 },      0,         func_move,   FL_CLIENT|FL_DRAG },
	{ { .button = Button2 },      0,         func_resize, FL_CLIENT|FL_DRAG },
	{ { .button = Button3 },      0,         func_lower,  FL_CLIENT },
};
#define NUM_BUTTON_CONTROLS (int)(sizeof(button_controls) / sizeof(button_controls[0]))

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Handle keyboard events.

void func_handle_key(XKeyEvent *e) {
	KeySym key = XkbKeycodeToKeysym(display.dpy, e->keycode, 0, 0);

	for (int i = 0; i < NUM_KEYBOARD_CONTROLS; i++) {
		struct bind *bind = &keyboard_controls[i];
		if (key == bind->control.key && (e->state & KEY_STATE_MASK) == bind->state) {
			void *sptr = NULL;
			if (bind->flags & FL_CLIENT) {
				if (!current)
					return;
				sptr = current;
			} else if (bind->flags & FL_SCREEN) {
				sptr = find_current_screen();
			}
			bind->func(sptr, (XEvent *)e, bind->flags);
			break;
		}
	}
}

// Handle mousebutton events.

void func_handle_button(XButtonEvent *e) {
	for (int i = 0; i < NUM_BUTTON_CONTROLS; i++) {
		struct bind *bind = &button_controls[i];
		if (e->button == bind->control.button && (e->state & BUTTON_STATE_MASK) == bind->state) {
			void *sptr = NULL;
			if (bind->flags & FL_CLIENT) {
				struct client *c = find_client(e->window);
				if (!c)
					return;
				sptr = c;
			} else if (bind->flags & FL_SCREEN) {
				struct screen *s = find_current_screen();
				if (!s)
					return;
				sptr = s;
			}
			bind->func(sptr, (XEvent *)e, bind->flags);
			break;
		}
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

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
	discard_enter_events(c);
}

static void func_delete(void *sptr, XEvent *e, unsigned flags) {
	if (!(flags & FL_CLIENT))
		return;
	struct client *c = sptr;
	(void)e;
	send_wm_delete(c, flags & FL_VALUEMASK);
}

static void func_dock(void *sptr, XEvent *e, unsigned flags) {
	if (!(flags & FL_SCREEN))
		return;
	struct screen *current_screen = sptr;
	(void)e;
	if (flags & FL_TOGGLE) {
		set_docks_visible(current_screen, !current_screen->docks_visible);
	}
}

static void func_info(void *sptr, XEvent *e, unsigned flags) {
	if (e->type != KeyPress)
		return;
	if (!(flags & FL_CLIENT))
		return;
	struct client *c = sptr;
	XKeyEvent *xkey = (XKeyEvent *)e;
	client_show_info(c, xkey->keycode);
}

static void func_lower(void *sptr, XEvent *e, unsigned flags) {
	if (!(flags & FL_CLIENT))
		return;
	struct client *c = sptr;
	(void)e;
	client_lower(c);
}

static void func_move(void *sptr, XEvent *e, unsigned flags) {
	struct client *c = sptr;
	if (!(flags & FL_CLIENT) || !c)
		return;

	struct monitor *monitor = client_monitor(c, NULL);
	int width_inc = (c->width_inc > 1) ? c->width_inc : 16;
	int height_inc = (c->height_inc > 1) ? c->height_inc : 16;

	if (flags & FL_DRAG) {
		XButtonEvent *xbutton = (XButtonEvent *)e;
		client_move_drag(c, xbutton->button);
		return;
	}

	if (flags & FL_RELATIVE) {
		if (flags & FL_RIGHT) {
			c->x += width_inc;
		}
		if (flags & FL_LEFT) {
			c->x -= width_inc;
		}
		if (flags & FL_DOWN) {
			c->y += height_inc;
		}
		if (flags & FL_UP) {
			c->y -= height_inc;
		}
	} else {
		if (flags & FL_RIGHT) {
			c->x = monitor->x + monitor->width - c->width-c->border;
		}
		if (flags & FL_LEFT) {
			c->x = monitor->x + c->border;
		}
		if (flags & FL_BOTTOM) {
			c->y = monitor->y + monitor->height - c->height-c->border;
		}
		if (flags & FL_TOP) {
			c->y = monitor->y + c->border;
		}
	}

	do_client_move(c);
}

static void func_next(void *sptr, XEvent *e, unsigned flags) {
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

static void func_resize(void *sptr, XEvent *e, unsigned flags) {
	struct client *c = sptr;
	if (!(flags & FL_CLIENT) || !c)
		return;

	int width_inc = (c->width_inc > 1) ? c->width_inc : 16;
	int height_inc = (c->height_inc > 1) ? c->height_inc : 16;

	if (flags & FL_DRAG) {
		XButtonEvent *xbutton = (XButtonEvent *)e;
		client_resize_sweep(c, xbutton->button);
		return;
	}

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
		if ((flags & FL_HORZ) == FL_HORZ) {
			hv |= MAXIMISE_HORZ;
		}
		if ((flags & FL_VERT) == FL_VERT) {
			hv |= MAXIMISE_VERT;
		}
		client_maximise(c, NET_WM_STATE_TOGGLE, hv);
		return;
	}

	do_client_move(c);
}

static void func_spawn(void *sptr, XEvent *e, unsigned flags) {
	// TODO: configurable array of commands to run indexed by FL_VALUEMASK?
	(void)sptr;
	(void)e;
	(void)flags;
	spawn((const char *const *)option.term);
}

static void func_vdesk(void *sptr, XEvent *e, unsigned flags) {
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
		struct screen *current_screen = sptr;
		if (flags & FL_TOGGLE) {
			switch_vdesk(current_screen, current_screen->old_vdesk);
		} else if (flags & FL_RELATIVE) {
			if (flags & FL_DOWN) {
				if (current_screen->vdesk > 0) {
					switch_vdesk(current_screen, current_screen->vdesk - 1);
				}
			}
			if (flags & FL_UP) {
				if (current_screen->vdesk < VDESK_MAX) {
					switch_vdesk(current_screen, current_screen->vdesk + 1);
				}
			}
		} else {
			int v = flags & FL_VALUEMASK;
			switch_vdesk(current_screen, v);
		}
	}
}
