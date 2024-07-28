/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Client management: user window manipulation

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/X.h>
#include <X11/Xlib.h>

#include "bind.h"
#include "client.h"
#include "display.h"
#include "evilwm.h"
#include "ewmh.h"
#include "list.h"
#include "screen.h"
#include "util.h"

////////////////////////////////
#ifdef SHAPE_OUTLINE
#include <X11/extensions/shape.h>

static void do_outline(struct client *c, _Bool set) {
	if (!c) {
		// LOG_DEBUG("Tried to SHAPE outline a null client\n");
		return;
	}
	if (!display.have_shape) {
		LOG_ERROR("No SHAPE extension\n");
		return;
	}

	XRectangle rect = {
		.x = 0, .y = 0,
		.width = c->width,
		.height = c->height,
	};
	Region region = XCreateRegion(); XUnionRectWithRegion(&rect, region, region);
	Region holed = XCreateRegion(); XUnionRectWithRegion(&rect, holed, holed);
	// XShrinkRegion(region, -c->border, -c->border);
	XShrinkRegion(region, -0x4000, -0x4000);
	XSubtractRegion(region, holed, holed);
	if (set) {
		XMoveResizeWindow(display.dpy, c->parent, c->x - c->border, c->y - c->border, c->width, c->height);
		XConfigureWindow(display.dpy, c->parent, CWBorderWidth, &(XWindowChanges){ .border_width = c->normal_border?c->normal_border:option.bw });
		XShapeCombineRegion(display.dpy, c->parent, ShapeClip, 0, 0, holed, ShapeSet);
		XShapeCombineRegion(display.dpy, c->parent, ShapeBounding, 0, 0, holed, ShapeSet);
		XUnmapWindow(display.dpy,c->window);
	} else {
		XConfigureWindow(display.dpy, c->parent, CWBorderWidth, &(XWindowChanges){ .border_width = c->border });
		XShapeCombineRegion(display.dpy, c->parent, ShapeClip, 0, 0, region, ShapeSet);
		XShapeCombineRegion(display.dpy, c->parent, ShapeBounding, 0, 0, region, ShapeSet);
		set_shape(c);//ShapeBounding
		XMapWindow(display.dpy,c->window);
		c->ignore_unmap++;// here because consecutive unmaps don't reissue the notification
	}
	XDestroyRegion(region);
	XDestroyRegion(holed);
}
# define init_outline(...)
# define set_outline(c) do_outline(c, 1)
# define clear_outline(c) { if (c != NULL) do_outline(c, 0); }

////////////////////////////////
#elif defined(GC_INVERT)
#include "xalloc.h"
// Use the inverting graphics context to draw an outline for the client.
// Drawing it a second time will erase it.  If INFOBANNER_MOVERESIZE is
// defined, the information window is shown for the duration (but this can be
// slow on old X servers).
#define SPACE 3

static void draw_outline(struct client *c) {
#if defined(FONT) && !defined(INFOBANNER_MOVERESIZE)
	char buf[27];
	int width_inc = c->width_inc, height_inc = c->height_inc;
#endif

	XDrawRectangle(display.dpy, c->screen->root, c->screen->invert_gc,
		c->x - c->border, c->y - c->border,
		c->width + 2*c->border-1, c->height + 2*c->border-1);

#if defined(FONT) && !defined(INFOBANNER_MOVERESIZE)
	snprintf(buf, sizeof(buf), "%dx%d+%d+%d", (c->width-c->base_width)/width_inc,
			(c->height-c->base_height)/height_inc, c->x, c->y);
	XDrawString(display.dpy, c->screen->root, c->screen->invert_gc,
		c->x + c->width - XTextWidth(display.font, buf, strlen(buf)) - SPACE,
		c->y + c->height - SPACE,
		buf, strlen(buf));
#endif
}

static void clear_outline(struct client *current_outline){
	if (current_outline == NULL)
		return;
	draw_outline(current_outline);
	XUngrabServer(display.dpy);
	free(current_outline);
}

static struct client *set_outline(struct client *c, struct client *current_outline){
	clear_outline(current_outline);
	XSync(display.dpy, False);
	XGrabServer(display.dpy);
	draw_outline(c);
	return xmemdup(c, sizeof(*c));
}

# define init_outline(c) struct client *outline_c = NULL
# define set_outline(c) { outline_c = set_outline(c, outline_c); }
# define clear_outline(c) { clear_outline(outline_c); outline_c = NULL; }

////////////////////////////////
#else
# define init_outline(...)
# define set_outline(...)
# define clear_outline(...)
////////////////////////////////
#endif

static int absmin(int a, int b) {
	if (abs(a) < abs(b))
		return a;
	return b;
}
static _Bool bound(int a, int b, int min, int max) {
	return !( (a<min&&b<min) || (a>max&&b>max) );
	// false only when
	//  b a   |------------------|
	//  a b   |------------------|
	//        |------------------|   a b
	//        |------------------|   b a
}

// Snap a client to the edges of other clients (if on same screen, and visible)
// or to the screen border.
// Typically skipped when altmask is held (default shift)

static void snap_client(struct client *c, struct monitor *monitor) {
	int dx, dy;
	dx = dy = option.snap;
	for (struct list *iter = &(struct list){ // insert monitor as client
		.next=clients_tab_order,
		.data=&(struct client){
			.x=monitor->x+c->border,
			.y=monitor->y+c->border,
			.width=monitor->width-c->border*2,
			.height=monitor->height-c->border*2,
			.screen=c->screen,
			.vdesk=VDESK_FIXED,
	0}}; iter; iter = iter->next) {
		struct client *ci = iter->data;
		if (ci == c) continue;
		if (ci->screen != c->screen) continue;
		if (!is_visible(ci)) continue;
		if (ci->y - ci->border - c->border - c->height - c->y <= option.snap && c->y - c->border - ci->border - ci->height - ci->y <= option.snap) {
			dx = absmin(dx, ci->x + ci->width - c->x + c->border + ci->border);
			dx = absmin(dx, ci->x + ci->width - c->x - c->width);
			dx = absmin(dx, ci->x - c->x - c->width - c->border - ci->border);
			dx = absmin(dx, ci->x - c->x);
		}
		if (ci->x - ci->border - c->border - c->width - c->x <= option.snap && c->x - c->border - ci->border - ci->width - ci->x <= option.snap) {
			dy = absmin(dy, ci->y + ci->height - c->y + c->border + ci->border);
			dy = absmin(dy, ci->y + ci->height - c->y - c->height);
			dy = absmin(dy, ci->y - c->y - c->height - c->border - ci->border);
			dy = absmin(dy, ci->y - c->y);
		}
	}
	if (abs(dx) < option.snap)
		c->x += dx;
	if (abs(dy) < option.snap)
		c->y += dy;
}

// During a sweep (resize interaction), recalculate new dimensions for a window
// based on mouse position relative to top-left corner.

static void recalculate_sweep(struct client *c, int x1, int y1, int x2, int y2, _Bool force, struct monitor *monitor) {
	if (!force && option.snap) {
		// Snap cursor position to nearest border
		int dx = option.snap;
		int dy = option.snap;
		for (struct list *iter = &(struct list){ // insert monitor as client
			.next=clients_tab_order,
			.data=&(struct client){
				.x=monitor->x+c->border,
				.y=monitor->y+c->border,
				.width=monitor->width-c->border*2,
				.height=monitor->height-c->border*2,
				.screen=c->screen,
				.vdesk=VDESK_FIXED,
		0}}; iter; iter = iter->next) {
			struct client *ci = iter->data;
			if (ci == c) continue;
			if (ci->screen != c->screen) continue;
			if (!is_visible(ci)) continue;
			if (bound(y1, y2, ci->y, ci->y+ci->height)) {
				dx=absmin(dx, ci->x              - x2);
				dx=absmin(dx, ci->x + ci->width  - x2);
			}
			if (bound(x1, x2, ci->x, ci->x+ci->width )) {
				dy=absmin(dy, ci->y              - y2);
				dy=absmin(dy, ci->y + ci->height - y2);
			}
		}
		if (abs(dx) < option.snap) x2 += dx;
		if (abs(dy) < option.snap) y2 += dy;
	}

	if (force || c->oldw == 0) {
		c->oldw = 0;
		c->width = abs(x1 - x2);
		c->width -= (c->width - c->base_width) % c->width_inc;
		if (c->width_inc!=1) c->width += c->width_inc;
		if (c->min_width && c->width < c->min_width) c->width = c->min_width;
		if (c->max_width && c->width > c->max_width) c->width = c->max_width;
		c->x = (x1 <= x2) ? x1 : x1 - c->width;
	}
	if (force || c->oldh == 0)  {
		c->oldh = 0;
		c->height = abs(y1 - y2);
		c->height -= (c->height - c->base_height) % c->height_inc;
		if (c->height_inc!=1) c->height += c->height_inc;
		if (c->min_height && c->height < c->min_height) c->height = c->min_height;
		if (c->max_height && c->height > c->max_height) c->height = c->max_height;
		c->y = (y1 <= y2) ? y1 : y1 - c->height;
	}
}

#ifndef INFOBANNER_MOVERESIZE
# define create_info_window(...)
# define update_info_window(...)
# define remove_info_window(...)
#endif

static int motion_predicate(Display *d, XEvent *ev, XPointer arg){
	(void)d;
	(void)arg;
	LOG_XDEBUG("there exists a %s\n", xevent_string(ev->type));
	if (ev->type == MotionNotify) return 1;
	return 0;
}

// Handle user resizing a window with the mouse.
void client_resize_sweep(struct client *c, unsigned button) {
	// Ensure we can grab pointer events.
	if (!grab_pointer(c->screen->root, display.resize_curs))
		return;

	// Sweeping always raises.
	client_raise(c);

	int old_cx = c->x;
	int old_cy = c->y;

	struct monitor *monitor = client_monitor(c, NULL);

	create_info_window(c);
	init_outline(c);
	if (!option.solid_sweep)
		set_outline(c);
#ifdef RESIZE_WARP_POINTER
	// Warp pointer to the bottom-right of the client for resizing
	setmouse(c->window, c->width, c->height);
#else
	// do initial resize to pointer
#endif

	for (;;) {
		XEvent ev;
		XMaskEvent(display.dpy, ButtonPressMask|ButtonReleaseMask|PointerMotionMask, &ev);
		switch (ev.type) {
			case MotionNotify:
				if (ev.xmotion.root != c->screen->root)
					break;
				XUngrabServer(display.dpy);// outline
				recalculate_sweep(c, old_cx, old_cy, ev.xmotion.x, ev.xmotion.y, ev.xmotion.state & altmask, monitor);

				XEvent evc;
				if (!XCheckIfEvent(display.dpy,&evc,motion_predicate,NULL)) {
					if (option.solid_sweep)
						client_moveresize(c);
					else
						set_outline(c);
				}

				update_info_window(c);
				break;

			case ButtonRelease:
				if (ev.xbutton.button != button)
					break;
				if (!option.solid_sweep)
					clear_outline(c);
				remove_info_window();
				XUngrabPointer(display.dpy, CurrentTime);

				recalculate_sweep(c, old_cx, old_cy, ev.xmotion.x, ev.xmotion.y, ev.xmotion.state & altmask, monitor);
				client_moveresizeraise(c);
				// In case maximise state has changed:
				ewmh_set_net_wm_state(c);
				return;

			default:
				break;
		}
	}
}

// Handle user moving a window with the mouse.  Takes over processing X motion
// events until the mouse button is released.
//
// If solid drag is disabled, an outline is drawn, which leads to the same
// limitations as in the sweep() function.

void client_move_drag(struct client *c, unsigned button) {
	// Ensure we can grab pointer events.
	if (!grab_pointer(c->screen->root, display.move_curs))
		return;

	// Dragging always raises.
	client_raise(c);

	// Initial pointer and window positions; new coordinates calculated
	// relative to these.
	int x1, y1;
	int old_cx = c->x;
	int old_cy = c->y;
	get_pointer_root_xy(c->screen->root, &x1, &y1);

	struct monitor *monitor = client_monitor(c, NULL);

	create_info_window(c);
	init_outline(c);
	if (!option.solid_drag) {
		set_outline(c);
	}

	for (;;) {
		XEvent ev;
		XMaskEvent(display.dpy, ButtonPressMask|ButtonReleaseMask|PointerMotionMask, &ev);
		switch (ev.type) {
			case MotionNotify:
				if (ev.xmotion.root != c->screen->root)
					break;
				c->x = old_cx + (ev.xmotion.x - x1);
				c->y = old_cy + (ev.xmotion.y - y1);
				if (option.snap && !(ev.xmotion.state & altmask))
					snap_client(c, monitor);

				XEvent evc;
				if (!XCheckIfEvent(display.dpy,&evc,motion_predicate,NULL)) {
					update_info_window(c);
					if (option.solid_drag) {
						XMoveWindow(display.dpy, c->parent,
								c->x - c->border,
								c->y - c->border);
						send_config(c);
					} else {
#ifdef SHAPE_OUTLINE
						XMoveWindow(display.dpy, c->parent,
								c->x - c->border,
								c->y - c->border);
#else
						set_outline(c);
#endif
					}
				}
				break;

			case ButtonRelease:
				if (ev.xbutton.button != button)
					continue;
				if (!option.solid_drag) {
					clear_outline(c);
				}
				remove_info_window();
				XUngrabPointer(display.dpy, CurrentTime);
				if (!option.solid_drag) {
					// For solid drags, the client was
					// moved with the mouse.  For non-solid
					// drags, we need a final move/raise:
					client_moveresizeraise(c);
				}
				return;

			default:
				break;
		}
	}
}

#undef create_info_window
#undef update_info_window
#undef remove_info_window

#ifdef GC_INVERT
// Predicate function for use with XCheckIfEvent.
//
// This is used to detect when a keyrelease is followed by a keypress with the
// same keycode and timestamp, indicating autorepeat.

static Bool predicate_keyrepeatpress(Display *dummy, XEvent *ev, XPointer arg) {
	(void)dummy;
	XEvent *release_event = (XEvent *)arg;
	if (ev->type != KeyPress)
		return False;
	if (release_event->xkey.keycode != ev->xkey.keycode)
		return False;
	return release_event->xkey.time == ev->xkey.time;
}

// Show information window until the key used to activate (keycode) is
// released.
//
// This routine used to disable autorepeat for the duration, but modern X
// servers seem to only change the keyboard control after all keys have been
// physically released, which is not so useful here.  Instead, we detect when a
// key release is followed by a key press with the same code and timestamp,
// which indicates autorepeat.

void client_show_info(struct client *c, XEvent *e) {
	unsigned input;

	if (e->type == KeyPress) {
		input = e->xkey.keycode;
		if (XGrabKeyboard(display.dpy, c->screen->root, False, GrabModeAsync, GrabModeAsync, CurrentTime) != GrabSuccess)
			return;
	} else {
		input = e->xbutton.button;
		if (!grab_pointer(c->screen->root, None))
			return;
	}

#ifdef INFOBANNER
	create_info_window(c);
#else
	init_outline(c);
	set_outline(c);
#endif

	for (;;) {
		XEvent ev;
		if (e->type == KeyPress) {
			XMaskEvent(display.dpy, KeyReleaseMask, &ev);
			if (ev.xkey.keycode != input)
				continue;
			if (XCheckIfEvent(display.dpy, &ev, predicate_keyrepeatpress, (XPointer)&ev)) {
				// Autorepeat keypress detected - ignore!
				continue;
			}
		} else {
			XMaskEvent(display.dpy, ButtonReleaseMask, &ev);
			if (ev.xbutton.button != input)
				continue;
		}
		break;
	}

#ifdef INFOBANNER
	remove_info_window();
#else
	clear_outline(c, outline_c);
#endif

	if (e->type == KeyPress) {
		XUngrabKeyboard(display.dpy, CurrentTime);
	} else {
		XUngrabPointer(display.dpy, CurrentTime);
	}
}
#else
void client_show_info(struct client *c, XEvent *e) {}
#endif

// Move window to (potentially updated) client coordinates.

void client_moveresize(struct client *c) {
	XMoveResizeWindow(display.dpy, c->parent, c->x - c->border, c->y - c->border,
			c->width, c->height);
	XMoveResizeWindow(display.dpy, c->window, 0, 0, c->width, c->height);
	send_config(c);
}

// Same, but raise the client first.

void client_moveresizeraise(struct client *c) {
	client_raise(c);
	client_moveresize(c);
}

// Make sure window is onscreen (by moving it *if* necessary)
void client_intersect(struct client *c) {
	int intersects;
	struct monitor *close = client_monitor(c, &intersects);
	if (intersects) return;
	if (c->x > close->x+close->width ) c->x = close->x+close->width;
	if (c->x < close->x-c    ->width ) c->x = close->x-c    ->width;
	if (c->y > close->y+close->height) c->y = close->y+close->height;
	if (c->y < close->y-c    ->height) c->y = close->y-c    ->height;
	client_moveresize(c);
}

// Maximise (or de-maximise) horizontally, vertically, or both.
//
// Updates EWMH properties, but also stores old dimensions in evilwm-specific
// properties so that they persist across window manager restarts.

void client_maximise(struct client *c, int action, int hv) {
	struct monitor monitor;

	// Maximising to monitor or screen?
	if (hv & MAXIMISE_SCREEN) {
		monitor = (struct monitor){
			0, 0,
			DisplayWidth(display.dpy, c->screen->screen),
			DisplayHeight(display.dpy, c->screen->screen),
		.area=-1};
		// monitor.area=monitor.width*monitor.height;
	} else {
		monitor = *client_monitor(c, NULL);
	}

	if (hv & MAXIMISE_HORZ) {
		if (c->oldw) {
			if (action == NET_WM_STATE_REMOVE || action == NET_WM_STATE_TOGGLE) {
				c->x = c->oldx;
				c->width = c->oldw;
				c->oldw = 0;
				XDeleteProperty(display.dpy, c->window, X_ATOM(_EVILWM_UNMAXIMISED_HORZ));
			}
		} else {
			if (action == NET_WM_STATE_ADD || action == NET_WM_STATE_TOGGLE) {
				unsigned long props[2];
				c->oldx = c->x;
				c->oldw = c->width;
				c->x = monitor.x;
				c->width = monitor.width;
				props[0] = c->oldx;
				props[1] = c->oldw;
				XChangeProperty(display.dpy, c->window, X_ATOM(_EVILWM_UNMAXIMISED_HORZ),
						XA_CARDINAL, 32, PropModeReplace,
						(unsigned char *)&props, 2);
			}
		}
	}
	if (hv & MAXIMISE_VERT) {
		if (c->oldh) {
			if (action == NET_WM_STATE_REMOVE || action == NET_WM_STATE_TOGGLE) {
				c->y = c->oldy;
				c->height = c->oldh;
				c->oldh = 0;
				XDeleteProperty(display.dpy, c->window, X_ATOM(_EVILWM_UNMAXIMISED_VERT));
			}
		} else {
			if (action == NET_WM_STATE_ADD || action == NET_WM_STATE_TOGGLE) {
				unsigned long props[2];
				c->oldy = c->y;
				c->oldh = c->height;
				c->y = monitor.y;
				c->height = monitor.height;
				props[0] = c->oldy;
				props[1] = c->oldh;
				XChangeProperty(display.dpy, c->window, X_ATOM(_EVILWM_UNMAXIMISED_VERT),
						XA_CARDINAL, 32, PropModeReplace,
						(unsigned char *)&props, 2);
			}
		}
	}
	_Bool change_border = 0;
	if (c->oldw && c->oldh) {
		// maximised - remove border
		if (c->border) {
			c->border = 0;
			change_border = 1;
		}
	} else {
		// not maximised - add border
		if (!c->border && c->normal_border) {
			c->border = c->normal_border;
			change_border = 1;
		}
	}
	if (change_border) {
		XSetWindowBorderWidth(display.dpy, c->parent, c->border);
		ewmh_set_net_frame_extents(c->window, c->border);
	}
	ewmh_set_net_wm_state(c);
	client_moveresizeraise(c);
#ifdef MAXIMIZE_DISCARDENTERS
	discard_enter_events(c);
#endif
}

// Find and select the "next" client, relative to the currently selected one
// (basically, handle Alt+Tab).  Order is most-recently-used (maintained in the
// clients_tab_order list).

static struct client *next_visible_client(struct list *order) {
	if (!order) return NULL;
	for (struct list *l = order->next/*note the next*/; l; l=l->next) {
		if (is_visible((struct client *)l->data)) return l->data;
	}
	return NULL;
}

void client_select_next(void) {
	if (!clients_tab_order) return;
	struct client *c = next_visible_client(list_find(clients_tab_order, current));
	if (!c) c = next_visible_client(&(struct list){.next=clients_tab_order});
	if (!c) return;
	client_raise(c);
	client_intersect(c);
#ifdef NEXT_WARP_POINTER
	setmouse(c->window, c->width/2, c->height/2);
#elif defined(WARP_POINTER)
	setmouse(c->window, c->width+c->border-1, c->height+c->border-1);
#endif
	select_client(c);
#ifdef NEXT_DISCARDENTERS
	discard_enter_events(c);
#endif
}

#undef init_outline
#undef clear_outline
#undef set_outline
