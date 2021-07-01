/* evilwm - Minimalist Window Manager for X
 * Copyright (C) 1999-2021 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Screen management.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#ifdef RANDR
#include <X11/extensions/Xrandr.h>
#endif

#include "client.h"
#include "display.h"
#include "evilwm.h"
#include "ewmh.h"
#include "keymap.h"
#include "list.h"
#include "log.h"
#include "screen.h"
#include "util.h"
#include "xalloc.h"

// Set up DISPLAY environment variable to use

static char *screen_to_display_str(int i) {
	char *ds = DisplayString(display.dpy);
	char *dpy_str = xmalloc(20 + strlen(ds));
	strcpy(dpy_str, "DISPLAY=");
	strcat(dpy_str, ds);
	char *colon = strrchr(dpy_str, ':');
	if (!colon || display.nscreens < 2)
		return dpy_str;

	char *dot = strchr(colon, '.');
	if (!dot)
		dot = colon + strlen(colon);
	snprintf(dot, 12, ".%d", i);

	return dpy_str;
}

// Called once per screen when display is being initialised.

void screen_init(struct screen *s) {
	int i = s->screen;

	// Used to set the DISPLAY environment variable to something like
	// ":0.x" depending on which screen a terminal is launched from.
	s->display = screen_to_display_str(i);

	s->root = RootWindow(display.dpy, i);
#ifdef RANDR
	if (display.have_randr) {
		XRRSelectInput(display.dpy, s->root, RRScreenChangeNotifyMask);
	}
#endif

	// Default to first virtual desktop.  TODO: consider checking the
	// _NET_WM_DESKTOP property of the window with focus when we start to
	// change this default?
	s->vdesk = KEY_TO_VDESK(XK_1);

	// In case the visual for this screen uses a colourmap, ensure our
	// border colours are in it.
	XColor dummy;
	XAllocNamedColor(display.dpy, DefaultColormap(display.dpy, i), option.fg, &s->fg, &dummy);
	XAllocNamedColor(display.dpy, DefaultColormap(display.dpy, i), option.bg, &s->bg, &dummy);
	XAllocNamedColor(display.dpy, DefaultColormap(display.dpy, i), option.fc, &s->fc, &dummy);

	// When dragging an outline, we use an inverting graphics context
	// (GCFunction + GXinvert) so that simply drawing it again will erase
	// it from the screen.

	XGCValues gv;
	gv.function = GXinvert;
	gv.subwindow_mode = IncludeInferiors;
	gv.line_width = 1;  // option.bw
	gv.font = display.font->fid;
	s->invert_gc = XCreateGC(display.dpy, s->root,
				 GCFunction | GCSubwindowMode | GCLineWidth | GCFont, &gv);

	// We handle events to the root window:
	// SubstructureRedirectMask - create, destroy, configure window notifications
	// SubstructureNotifyMask - configure window requests
	// EnterWindowMask - enter events
	// ColormapChangeMask - when a new colourmap is needed

	XSetWindowAttributes attr;
	attr.event_mask = SubstructureRedirectMask | SubstructureNotifyMask
	                  | EnterWindowMask | ColormapChangeMask;
	XChangeWindowAttributes(display.dpy, s->root, CWEventMask, &attr);

	// Grab the various keyboard shortcuts
	grab_keys_for_screen(s);

	s->docks_visible = 1;

	// Scan all the windows on this screen
	LOG_XENTER("XQueryTree(screen=%d)", i);
	unsigned nwins;
	Window dw1, dw2, *wins;
	XQueryTree(display.dpy, s->root, &dw1, &dw2, &wins, &nwins);
	LOG_XDEBUG("%d windows\n", nwins);
	LOG_XLEAVE();

	// Manage all relevant windows
	for (unsigned j = 0; j < nwins; j++) {
		XWindowAttributes winattr;
		XGetWindowAttributes(display.dpy, wins[j], &winattr);
		// Override redirect implies a pop-up that we should ignore.
		// If map_state is not IsViewable, it shouldn't be shown right
		// now, so don't try to manage it.
		if (!winattr.override_redirect && winattr.map_state == IsViewable)
			client_manage_new(wins[j], s);
	}
	XFree(wins);

	Atom supported[] = {
		X_ATOM(_NET_CLIENT_LIST),
		X_ATOM(_NET_CLIENT_LIST_STACKING),
		X_ATOM(_NET_NUMBER_OF_DESKTOPS),
		X_ATOM(_NET_DESKTOP_GEOMETRY),
		X_ATOM(_NET_DESKTOP_VIEWPORT),
		X_ATOM(_NET_CURRENT_DESKTOP),
		X_ATOM(_NET_ACTIVE_WINDOW),
		X_ATOM(_NET_WORKAREA),
		X_ATOM(_NET_SUPPORTING_WM_CHECK),

		X_ATOM(_NET_CLOSE_WINDOW),
		X_ATOM(_NET_MOVERESIZE_WINDOW),
		X_ATOM(_NET_RESTACK_WINDOW),
		X_ATOM(_NET_REQUEST_FRAME_EXTENTS),

		X_ATOM(_NET_WM_DESKTOP),
		X_ATOM(_NET_WM_WINDOW_TYPE),
		X_ATOM(_NET_WM_WINDOW_TYPE_DESKTOP),
		X_ATOM(_NET_WM_WINDOW_TYPE_DOCK),
		X_ATOM(_NET_WM_STATE),
		X_ATOM(_NET_WM_STATE_MAXIMIZED_VERT),
		X_ATOM(_NET_WM_STATE_MAXIMIZED_HORZ),
		X_ATOM(_NET_WM_STATE_FULLSCREEN),
		X_ATOM(_NET_WM_STATE_HIDDEN),
		X_ATOM(_NET_WM_ALLOWED_ACTIONS),

		// Not sure if it makes any sense including every action here
		// as they'll already be listed per-client in the
		// _NET_WM_ALLOWED_ACTIONS property, but EWMH spec is unclear.
		X_ATOM(_NET_WM_ACTION_MOVE),
		X_ATOM(_NET_WM_ACTION_RESIZE),
		X_ATOM(_NET_WM_ACTION_MAXIMIZE_HORZ),
		X_ATOM(_NET_WM_ACTION_MAXIMIZE_VERT),
		X_ATOM(_NET_WM_ACTION_FULLSCREEN),
		X_ATOM(_NET_WM_ACTION_CHANGE_DESKTOP),
		X_ATOM(_NET_WM_ACTION_CLOSE),
		X_ATOM(_NET_FRAME_EXTENTS),
	};

	unsigned long num_desktops = option.vdesks;
	unsigned long vdesk = s->vdesk;
	unsigned long pid = getpid();

	s->supporting = XCreateSimpleWindow(display.dpy, s->root, 0, 0, 1, 1, 0, 0, 0);
	XChangeProperty(display.dpy, s->root, X_ATOM(_NET_SUPPORTED),
			XA_ATOM, 32, PropModeReplace,
			(unsigned char *)&supported,
			sizeof(supported) / sizeof(Atom));
	XChangeProperty(display.dpy, s->root, X_ATOM(_NET_NUMBER_OF_DESKTOPS),
			XA_CARDINAL, 32, PropModeReplace,
			(unsigned char *)&num_desktops, 1);
	XChangeProperty(display.dpy, s->root, X_ATOM(_NET_CURRENT_DESKTOP),
			XA_CARDINAL, 32, PropModeReplace,
			(unsigned char *)&vdesk, 1);
	XChangeProperty(display.dpy, s->root, X_ATOM(_NET_SUPPORTING_WM_CHECK),
			XA_WINDOW, 32, PropModeReplace,
			(unsigned char *)&s->supporting, 1);
	XChangeProperty(display.dpy, s->supporting, X_ATOM(_NET_SUPPORTING_WM_CHECK),
			XA_WINDOW, 32, PropModeReplace,
			(unsigned char *)&s->supporting, 1);
	XChangeProperty(display.dpy, s->supporting, X_ATOM(_NET_WM_NAME),
			XA_STRING, 8, PropModeReplace,
			(const unsigned char *)"evilwm", 6);
	XChangeProperty(display.dpy, s->supporting, X_ATOM(_NET_WM_PID),
			XA_CARDINAL, 32, PropModeReplace,
			(unsigned char *)&pid, 1);
	ewmh_set_screen_workarea(s);

}

void screen_deinit(struct screen *s) {
	XDeleteProperty(display.dpy, s->root, X_ATOM(_NET_SUPPORTED));
	XDeleteProperty(display.dpy, s->root, X_ATOM(_NET_CLIENT_LIST));
	XDeleteProperty(display.dpy, s->root, X_ATOM(_NET_CLIENT_LIST_STACKING));
	XDeleteProperty(display.dpy, s->root, X_ATOM(_NET_NUMBER_OF_DESKTOPS));
	XDeleteProperty(display.dpy, s->root, X_ATOM(_NET_DESKTOP_GEOMETRY));
	XDeleteProperty(display.dpy, s->root, X_ATOM(_NET_DESKTOP_VIEWPORT));
	XDeleteProperty(display.dpy, s->root, X_ATOM(_NET_CURRENT_DESKTOP));
	XDeleteProperty(display.dpy, s->root, X_ATOM(_NET_ACTIVE_WINDOW));
	XDeleteProperty(display.dpy, s->root, X_ATOM(_NET_WORKAREA));
	XDeleteProperty(display.dpy, s->root, X_ATOM(_NET_SUPPORTING_WM_CHECK));
	XDestroyWindow(display.dpy, s->supporting);
}

// Switch virtual desktop.  Hides clients on different vdesks, shows clients on
// the selected one.  Docks are always shown (unless user has hidden them
// explicitly).  Fixed clients are always shown.

void switch_vdesk(struct screen *s, unsigned v) {
#ifdef DEBUG
	int nhidden = 0, nraised = 0;
#endif

	// Sanity check vdesk number.
	if (!valid_vdesk(v))
		return;

	// Selected == current?  Do nothing.
	if (v == s->vdesk)
		return;

	LOG_ENTER("switch_vdesk(screen=%d, from=%d, to=%d)", s->screen, s->vdesk, v);

	// If current client is not fixed, deselect it.  An enter event from
	// mapping clients may select a new one.
	if (current && !is_fixed(current)) {
		select_client(NULL);
	}

	for (struct list *iter = clients_tab_order; iter; iter = iter->next) {
		struct client *c = iter->data;
		if (c->screen != s)
			continue;
		if (c->vdesk == s->vdesk) {
			client_hide(c);
#ifdef DEBUG
			nhidden++;
#endif
		} else if (c->vdesk == v) {
			if (!c->is_dock || s->docks_visible)
				client_show(c);
#ifdef DEBUG
			nraised++;
#endif
		}
	}

	// Store previous vdesk, so that user may toggle back to it
	s->old_vdesk = s->vdesk;

	// Update current vdesk (including EWMH properties)
	s->vdesk = v;
	ewmh_set_net_current_desktop(s);

	LOG_DEBUG("%d hidden, %d raised\n", nhidden, nraised);
	LOG_LEAVE();
}

// Set whether docks are visible on the current screen.

void set_docks_visible(struct screen *s, int is_visible) {
	LOG_ENTER("set_docks_visible(screen=%d, is_visible=%d)", s->screen, is_visible);

	s->docks_visible = is_visible;

	// Traverse client list and hide or show any docks on this screen as
	// appropriate.

	for (struct list *iter = clients_tab_order; iter; iter = iter->next) {
		struct client *c = iter->data;
		if (c->screen != s)
			continue;
		if (c->is_dock) {
			if (is_visible) {
				// XXX I've assumed that if you want to see
				// them, you also want them raised...
				if (is_fixed(c) || (c->vdesk == s->vdesk)) {
					client_show(c);
					client_raise(c);
				}
			} else {
				client_hide(c);
			}
		}
	}

	LOG_LEAVE();
}

// Scale a coordinate and size between old screen size and new.

static int scale_pos(int new_screen_size, int old_screen_size, int cli_pos, int cli_size) {
	if (old_screen_size != cli_size && new_screen_size != cli_size) {
		new_screen_size -= cli_size;
		old_screen_size -= cli_size;
	}
	return new_screen_size * cli_pos / old_screen_size;
}

// If a screen has been resized, eg, due to xrandr, some windows have the
// possibility of:
//
//   a) not being visible
//
//   b) being vertically/horizontally maximised to the wrong extent
//
// Currently, i can't think of a good policy for doing this, but the minimal
// modification is to fix (b), and ensure (a) is visible.  NB, maximised
// windows will need their old* values updating according to (a).

void fix_screen_after_resize(struct screen *s, int oldw, int oldh) {
	int neww = DisplayWidth(display.dpy, s->screen);
	int newh = DisplayHeight(display.dpy, s->screen);

	for (struct list *iter = clients_tab_order; iter; iter = iter->next) {
		struct client *c = iter->data;
		// only handle clients on the screen being resized
		if (c->screen != s)
			continue;

		if (c->oldw) {
			// horiz maximised: update width, update old x pos
			c->width = neww;
			c->oldx = scale_pos(neww, oldw, c->oldx, c->oldw + c->border);
		} else {
			// horiz normal: update x pos
			c->x = scale_pos(neww, oldw, c->x, c->width + c->border);
		}

		if (c->oldh) {
			// vert maximised: update height, update old y pos
			c->height = newh;
			c->oldy = scale_pos(newh, oldh, c->oldy, c->oldh + c->border);
		} else {
			// vert normal: update y pos
			c->y = scale_pos(newh, oldh, c->y, c->height + c->border);
		}
		client_moveresize(c);
	}
}

// Find screen corresponding to specified root window.

struct screen *find_screen(Window root) {
	for (int i = 0; i < display.nscreens; i++) {
		if (display.screens[i].root == root)
			return &display.screens[i];
	}
	return NULL;
}

// Find screen corresponding to the root window the pointer is currently on.

struct screen *find_current_screen(void) {
	Window cur_root;
	Window dw;  // dummy
	int di;  // dummy
	unsigned dui;  // dummy

	// XQueryPointer is useful for getting the current pointer root
	XQueryPointer(display.dpy, display.screens[0].root, &cur_root, &dw, &di, &di, &di, &di, &dui);
	return find_screen(cur_root);
}

// Grab a key with the specified mask, and additionally with CapsLock or
// NumLock on.

static void grab_keysym(Window w, unsigned mask, KeySym keysym) {
	KeyCode keycode = XKeysymToKeycode(display.dpy, keysym);
	XGrabKey(display.dpy, keycode, mask, w, True,
			GrabModeAsync, GrabModeAsync);
	XGrabKey(display.dpy, keycode, mask|LockMask, w, True,
			GrabModeAsync, GrabModeAsync);
	if (numlockmask) {
		XGrabKey(display.dpy, keycode, mask|numlockmask, w, True,
				GrabModeAsync, GrabModeAsync);
		XGrabKey(display.dpy, keycode, mask|numlockmask|LockMask, w, True,
				GrabModeAsync, GrabModeAsync);
	}
}

// List of keys to grab with Control+Alt (mask1):

static KeySym keys_to_grab[] = {
#ifdef VWM
	KEY_FIX, KEY_PREVDESK, KEY_NEXTDESK, KEY_TOGGLEDESK,
	XK_1, XK_2, XK_3, XK_4, XK_5, XK_6, XK_7, XK_8,
#endif
	KEY_NEW, KEY_KILL,
	KEY_TOPLEFT, KEY_TOPRIGHT, KEY_BOTTOMLEFT, KEY_BOTTOMRIGHT,
	KEY_LEFT, KEY_RIGHT, KEY_DOWN, KEY_UP,
	KEY_LOWER, KEY_ALTLOWER, KEY_INFO, KEY_MAXVERT, KEY_MAX,
	KEY_DOCK_TOGGLE
};
#define NUM_GRABS (int)(sizeof(keys_to_grab) / sizeof(KeySym))

// List of keys to grab with Control+Alt+Shift (mask1+altmask)

static KeySym alt_keys_to_grab[] = {
	KEY_KILL, KEY_LEFT, KEY_RIGHT, KEY_DOWN, KEY_UP,
	KEY_MAXVERT,
};
#define NUM_ALT_GRABS (int)(sizeof(alt_keys_to_grab) / sizeof(KeySym))

// Grab all the keys we're interested in for the specified screen.

void grab_keys_for_screen(struct screen *s) {
	// Release any previous grabs
	XUngrabKey(display.dpy, AnyKey, AnyModifier, s->root);

	// Grab with Control+Alt (mask1 option):
	for (int i = 0; i < NUM_GRABS; i++) {
		grab_keysym(s->root, grabmask1, keys_to_grab[i]);
	}

	// Grab with Control+Alt+Shift (mask1+altmask options):
	for (int i = 0; i < NUM_ALT_GRABS; i++) {
		grab_keysym(s->root, grabmask1 | altmask, alt_keys_to_grab[i]);
	}

	// Only one grab made with only Alt (mask2 option) pressed:
	grab_keysym(s->root, grabmask2, KEY_NEXT);
}
