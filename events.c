/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// X11 event processing.  This is the core of the window manager and processes
// events from clients and user interaction in a loop until signalled to exit.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>

#include <X11/X.h>
#include <X11/Xlib.h>

#ifdef SHAPE
#include <X11/extensions/shape.h>
#endif
#ifdef RANDR
#include <X11/extensions/Xrandr.h>
#endif

#include "bind.h"
#include "client.h"
#include "display.h"
#include "events.h"
#include "evilwm.h"
#include "ewmh.h"
#include "list.h"
#include "log.h"
#include "screen.h"
#include "util.h"

// Event loop will run until this flag is set
_Bool end_event_loop;

// Flags that the client list should be scanned and marked clients removed.
// Set by unhandled X errors and unmap requests.
int need_client_tidy = 0;

// Apply the changes from an XWindowChanges struct to a client.

static void do_window_changes(int value_mask, XWindowChanges *wc, struct client *c,
		int gravity) {
	LOG_XENTER("do_window_changes(window=%lx), mask: 0x%x, gravity: 0x%x, was %dx%d+%d+%d",
		c->window, value_mask, gravity, c->width, c->height, c->x, c->y);
	// https://x.org/releases/X11R7.7/doc/xproto/x11protocol.html#requests:ConfigureWindow
	if (gravity == 0)
		gravity = c->win_gravity_hint;
	c->win_gravity = gravity;
	if (value_mask & CWX) {
		c->x = wc->x;
		LOG_XDEBUG("CWX      x=%d\n", wc->x);
	}
	if (value_mask & CWY) {
		c->y = wc->y;
		LOG_XDEBUG("CWY      y=%d\n", wc->y);
	}
	if (value_mask & (CWWidth|CWHeight)) {
		int dw = 0, dh = 0;
		if (!(value_mask & (CWX|CWY))) {
			client_gravitate(c, -c->border);
		}
		if (value_mask & CWWidth) {
			LOG_XDEBUG("CWWidth  width=%d\n", wc->width);
			int neww = wc->width;
			if (neww < c->min_width)
				neww = c->min_width;
			if (c->max_width && neww > c->max_width)
				neww = c->max_width;
			dw = neww - c->width;
			c->width = neww;
		}
		if (value_mask & CWHeight) {
			LOG_XDEBUG("CWHeight height=%d\n", wc->height);
			int newh = wc->height;
			if (newh < c->min_height)
				newh = c->min_height;
			if (c->max_height && newh > c->max_height)
				newh = c->max_height;
			dh = newh - c->height;
			c->height = newh;
		}

		// only apply position fixes if not being explicitly moved
		if (!(value_mask & (CWX|CWY))) {
			switch (gravity) {
			default:
			case NorthWestGravity:
				break;
			case NorthGravity:
				c->x -= (dw / 2);
				break;
			case NorthEastGravity:
				c->x -= dw;
				break;
			case WestGravity:
				c->y -= (dh / 2);
				break;
			case CenterGravity:
				c->x -= (dw / 2);
				c->y -= (dh / 2);
				break;
			case EastGravity:
				c->x -= dw;
				c->y -= (dh / 2);
				break;
			case SouthWestGravity:
				c->y -= dh;
				break;
			case SouthGravity:
				c->x -= (dw / 2);
				c->y -= dh;
				break;
			case SouthEastGravity:
				c->x -= dw;
				c->y -= dh;
				break;
			}
			value_mask |= CWX|CWY;
			client_gravitate(c, c->border);
		}
	}

	if (value_mask & CWSibling) {
		LOG_XDEBUG("CWSibling =0x%lx\n", wc->sibling);
	}
	if (value_mask & CWStackMode) {
		LOG_XDEBUG("CWSibling =%i?0x%lx\nCWStackMode %i ", value_mask & CWSibling, wc->sibling, wc->stack_mode);
		switch (wc->stack_mode){
		case Above:
			LOG_XDEBUG_("Above\n");
			if (value_mask & CWSibling) {
				// place above = place under next higher
				struct client *sc = find_client(wc->sibling);
				if (!sc) break;
				struct list *iter = clients_stacking_order;
				while (iter && iter->data != sc) iter=iter->next; // back to front
				if (!iter) break;
				iter=iter->next;
				client_under(c, iter?iter->data:NULL);
			} else {
				client_raise(c);
			}
			break;
		case Below:
			LOG_XDEBUG_("Below\n");
			if (value_mask & CWSibling) {
				client_under(c,find_client(wc->sibling));
			} else {
				client_lower(c);
			}
			break;
		case TopIf:
			LOG_XDEBUG_("TopIf\n");
			LOG_ERROR("Cannot handle CWStackMode %i from window=%lx",
				wc->stack_mode, c->window);
			break; // raise if occluded by sibling
		case BottomIf:
			LOG_XDEBUG_("BottomIf\n");
			LOG_ERROR("Cannot handle CWStackMode %i from window=%lx",
				wc->stack_mode, c->window);
			break; // lower if occludes sibling
		case Opposite:
			LOG_XDEBUG_("Opposite\n");
			LOG_ERROR("Cannot handle CWStackMode %i from window=%lx",
				wc->stack_mode, c->window);
			break; // one of previous
		default:
			LOG_XDEBUG_("?\n");
			break;
		}
	}
	value_mask &=~CWStackMode;
	value_mask &=~CWSibling;

	wc->x = c->x - c->border;
	wc->y = c->y - c->border;
	wc->border_width = c->border;
	XConfigureWindow(display.dpy, c->parent, value_mask, wc);
	XMoveResizeWindow(display.dpy, c->window, 0, 0, c->width, c->height);
	if ((value_mask & (CWX|CWY)) && !(value_mask & (CWWidth|CWHeight))) {
		send_config(c);
	}
	LOG_XLEAVE();
}

static void handle_configure_notify(XConfigureEvent *e) {
#ifdef XDEBUG
	struct client *c = find_client(e->window);
	if (!c) {
		LOG_XDEBUG("handle_configure_notify() on unmanaged window\n");
		return;
	}
	LOG_XENTER("handle_configure_notify(window=%lx, parent=%lx)", c->window, c->parent);
	LOG_XDEBUG("x,y w,h=%d,%d %d,%d\n", e->x, e->y, e->width, e->height);
	LOG_XDEBUG("bw=%d\n", e->border_width);
	LOG_XDEBUG("above=%lx\n", (unsigned long)e->above);
	LOG_XDEBUG("override_redirect=%d\n", (int)e->override_redirect);
	LOG_XLEAVE();
#else
	(void)e;
#endif
}

#ifdef CONFIGREQ
static void handle_configure_request(XConfigureRequestEvent *e) {
	struct client *c = find_client(e->window);
	if (c && c->ignore_configreq) {
		LOG_DEBUG("Ignoring configure request due to 'manual' directive\n");
		return;
	}

	XWindowChanges wc;
	wc.x = e->x;
	wc.y = e->y;
	wc.width = e->width;
	wc.height = e->height;
	wc.border_width = 0;
	wc.sibling = e->above;
	wc.stack_mode = e->detail;

	if (c) {
		if (e->value_mask & CWStackMode && e->value_mask & CWSibling) {
			struct client *sibling = find_client(e->above);
			if (sibling) {
				wc.sibling = sibling->parent;
			}
		}
		do_window_changes(e->value_mask, &wc, c, ForgetGravity);
#ifdef CONFIGURECURRENT_DISCARDENTERS
		if (c == current)
			discard_enter_events(c);
#endif
	} else {
		LOG_XENTER("XConfigureWindow(window=%lx, value_mask=%lx)", (unsigned long)e->window, e->value_mask);
		XConfigureWindow(display.dpy, e->window, e->value_mask, &wc);
		LOG_XLEAVE();
	}
}
#endif

static void handle_map_request(XMapRequestEvent *e) {
	struct client *c = find_client(e->window);

	LOG_ENTER("handle_map_request(window=%lx)", (unsigned long)e->window);
	if (c) {
#ifdef MAPREQUEST_SHOWEXISTING
		if (!on_vdesk(c)) switch_vdesk(c->screen, c->vdesk);
		client_show(c);
		client_raise(c);
#endif
	} else {
		XWindowAttributes attr;
		XGetWindowAttributes(display.dpy, e->window, &attr);
		client_manage_new(e->window, find_screen(attr.root));
	}
	LOG_LEAVE();
}

static void handle_unmap_event(XUnmapEvent *e) {
	struct client *c = find_client(e->window);

	LOG_XENTER("handle_unmap_event(window=%lx)", (unsigned long)e->window);
	if (c) {
		if (c->ignore_unmap) {
			c->ignore_unmap--;
			LOG_XDEBUG("ignored (%d ignores remaining)\n", c->ignore_unmap);
		} else {
			LOG_XDEBUG("flagging client for removal\n");
			c->remove = 1;
			need_client_tidy = 1;
		}
	} else {
		LOG_XDEBUG("unknown client!\n");
	}
	LOG_XLEAVE();
}

static void handle_colormap_change(XColormapEvent *e) {
	struct client *c = find_client(e->window);

	if (c && e->new) {
		c->cmap = e->colormap;
		XInstallColormap(display.dpy, c->cmap);
	}
}

static void handle_property_change(XPropertyEvent *e) {
	struct client *c = find_client(e->window);
	if (!c) return;
#ifdef DEBUG
#ifndef XDEBUG
	if (e->atom!=XA_WM_NORMAL_HINTS && e->atom!=X_ATOM(_NET_WM_WINDOW_TYPE)) return;
#endif
	XTextProperty wmname;
	XGetWMName(display.dpy, e->window, &wmname);
	LOG_ENTER("handle_property_change(window=%lx (\"%s\"), atom=%s)", (unsigned long)e->window, wmname.value, debug_atom_name(e->atom));
	if (wmname.value)
		XFree(wmname.value);
#endif

	if (e->atom == XA_WM_NORMAL_HINTS) {
		get_wm_normal_hints(c);
		LOG_DEBUG("geometry=%dx%d+%d+%d\n", c->width, c->height, c->x, c->y);
	} else if (e->atom == X_ATOM(_NET_WM_WINDOW_TYPE)) {
		get_window_type(c);
		if (is_visible(c)) client_show(c);
	}
	LOG_LEAVE();
}

static void handle_enter_event(XCrossingEvent *e) {
	struct client *c = find_client(e->window);

	if (c) {
		if (!is_visible(c)) return;
		client_select(c);
		clients_tab_order = list_to_head(clients_tab_order, c);
	}
}

static void handle_mappingnotify_event(XMappingEvent *e) {
	XRefreshKeyboardMapping(e);
	if (e->request == MappingKeyboard) {
		int i;
		for (i = 0; i < display.nscreens; i++) {
			bind_grab_for_screen(&display.screens[i]);
		}
	}
}

#ifdef SHAPE
static void handle_shape_event(XShapeEvent *e) {
	struct client *c = find_client(e->window);
	if (c && c->window == e->window)
		set_shape(c);
}
#endif

#ifdef RANDR
static void handle_randr_event(XRRScreenChangeNotifyEvent *e) {
	struct screen *s = find_screen(e->root);
	// Record geometries of clients relative to monitor
	scan_clients_before_resize(s);
	// Update Xlib's idea of screen size
	XRRUpdateConfiguration((XEvent*)e);
	// Scan new monitor list
	screen_probe_monitors(s);
	// Fix any clients that are now not visible on any monitor.  Also
	// adjusts maximised geometries where appropriate.
	fix_screen_after_resize(s);
	// Update various EWMH properties that reflect screen geometry
	ewmh_set_screen_workarea(s);
}
#endif

// Events sent to clients

static void handle_client_message(XClientMessageEvent *e) {
	struct screen *s = find_current_screen();
	struct client *c;

	LOG_ENTER("handle_client_message(window=%lx, format=%d, type=%s)", (unsigned long)e->window, e->format, debug_atom_name(e->message_type));
#ifdef XDEBUG
	switch (e->format) {
		case 8:
			LOG_DEBUG("b={ ");
			for (int i=0;i<20;i++) {
				LOG_DEBUG_("%d, ",e->data.b[i]);
			}
			LOG_DEBUG_("}\n");
			break;
		case 16:
			LOG_DEBUG("s={ ");
			for (int i=0;i<10;i++) {
				LOG_DEBUG_("%d, ",e->data.s[i]);
			}
			LOG_DEBUG_("}\n");
			break;
		case 32:
			LOG_DEBUG("l={ ");
			for (int i=0;i<5;i++) {
				LOG_DEBUG_("%li, ",e->data.l[i]);
			}
			LOG_DEBUG_("}\n");
			break;
	}
#endif

	if (e->message_type == X_ATOM(_NET_CURRENT_DESKTOP)) {
		switch_vdesk(s, e->data.l[0]);
		LOG_LEAVE();
		return;
	}

	c = find_client(e->window);
	if (!c) {

		// _NET_REQUEST_FRAME_EXTENTS is intended to be sent from
		// unmapped windows.  The reply only needs to be an _estimate_
		// of the border widths, but calculate it anyway - the only
		// thing that affects this for us is MWM hints.

		if (e->message_type == X_ATOM(_NET_REQUEST_FRAME_EXTENTS)) {
			int bw = window_normal_border(e->window);
			ewmh_set_net_frame_extents(e->window, bw);
#ifdef UNMAN_FOCUS_WARP_POINTER
		} else if (e->message_type == X_ATOM(_NET_ACTIVE_WINDOW)) {
			LOG_DEBUG("Unmanaged window asks for focus\n"); // XXX Steam does this with its dropdowns
			XWindowAttributes attr;
			int root_x; int root_y;
			//dummy:
			Window root;
			Window child;// that the pointer is in
			int window_x; int window_y;
			unsigned int mask;//kb modifiers
			if ( XQueryPointer(display.dpy, e->window, &root, &child, &root_x, &root_y, &window_x, &window_y, &mask)
				&& XGetWindowAttributes(display.dpy, e->window, &attr) ) {
				int x = root_x - attr.x;
				int y = root_y - attr.y;
				if (x > attr.width ) x = attr.width;
				if (y > attr.height) y = attr.height;
				if (x < 0) x = 0;
				if (y < 0) y = 0;
				setmouse(e->window, x, y);
			}
#endif
		} else {
			LOG_DEBUG("Unmanaged window, discarding\n");
		}

		LOG_LEAVE();
		return;
	}

	if (e->message_type == X_ATOM(_NET_ACTIVE_WINDOW)) {
		// Only do this if it came from direct user action
		if (e->data.l[0] == 2 && c->screen == s)
			client_select(c);
		LOG_LEAVE();
		return;
	}

	if (e->message_type == X_ATOM(_NET_CLOSE_WINDOW)) {
		// Only do this if it came from direct user action
		if (e->data.l[1] == 2) {
			send_wm_delete(c, 0);
		}
		LOG_LEAVE();
		return;
	}

	if (e->message_type == X_ATOM(_NET_MOVERESIZE_WINDOW)) {
		// Only do this if it came from direct user action
		int source_indication = (e->data.l[0] >> 12) & 3;
		if (source_indication == 2) {
			int value_mask = (e->data.l[0] >> 8) & 0x0f;
			int gravity = e->data.l[0] & 0xff;
			XWindowChanges wc;

			wc.x = e->data.l[1];
			wc.y = e->data.l[2];
			wc.width = e->data.l[3];
			wc.height = e->data.l[4];
			do_window_changes(value_mask, &wc, c, gravity);
		}
		LOG_LEAVE();
		return;
	}

	if (e->message_type == X_ATOM(_NET_RESTACK_WINDOW)) {
		// Only do this if it came from direct user action
		if (e->data.l[0] == 2) {
			XWindowChanges wc;

			wc.sibling = e->data.l[1];
			wc.stack_mode = e->data.l[2];
			do_window_changes(CWSibling | CWStackMode, &wc, c, c->win_gravity);
		}
		LOG_LEAVE();
		return;
	}

	if (e->message_type == X_ATOM(_NET_WM_DESKTOP)) {
		// Only do this if it came from direct user action
		if (e->data.l[1] == 2) {
			LOG_DEBUG("Swapping desktop\n");
			client_to_vdesk(c, e->data.l[0]);
		}
		LOG_LEAVE();
		return;
	}

#ifdef CONFIGREQ
	if (e->message_type == X_ATOM(_NET_WM_STATE)) {
		if (c->ignore_configreq) {
			LOG_DEBUG("Ignoring _NET_WM_STATE message due to 'manual' directive\n");
			LOG_LEAVE();
			return;
		}
		int i, maximise_hv = 0;
		// Message can contain up to two state changes:
		for (i = 1; i <= 2; i++) {
			if ((Atom)e->data.l[i] == X_ATOM(_NET_WM_STATE_MAXIMIZED_VERT)) {
				maximise_hv |= MAXIMISE_VERT;
			} else if ((Atom)e->data.l[i] == X_ATOM(_NET_WM_STATE_MAXIMIZED_HORZ)) {
				maximise_hv |= MAXIMISE_HORZ;
			} else if ((Atom)e->data.l[i] == X_ATOM(_NET_WM_STATE_FULLSCREEN)) {
				maximise_hv |= MAXIMISE_VERT|MAXIMISE_HORZ;
			}
		}
		if (maximise_hv) {
			client_maximise(c, e->data.l[0], maximise_hv);
		}
		LOG_LEAVE();
		return;
	}
#endif

	LOG_LEAVE();
}

// Run the main event loop.  This will run until something tells us to quit
// (generally, a signal).

void event_main_loop(void) {
	// XEvent is a big union of all the core event types, but we also need
	// to handle events about extensions, so make a union of the union...
	union {
		XEvent xevent;
#ifdef SHAPE
		XShapeEvent xshape;
#endif
#ifdef RANDR
		XRRScreenChangeNotifyEvent xrandr;
#endif
	} ev;

	// Main event loop
	while (!end_event_loop) {
		if (interruptibleXNextEvent(&ev.xevent)) {
			if ( ev.xevent.type!=UnmapNotify && ev.xevent.type!=DestroyNotify && ev.xevent.type!=EnterNotify && ev.xevent.type!=ColormapNotify ) {
				if (initialising) LOG_XDEBUG("Resetting initialising from 0x%lx to NULL\n",initialising);
				initialising = None;
				if (removing) LOG_XDEBUG("Resetting removing from 0x%lx to NULL\n",removing);
				removing = None;
				removing_parent = None;
			}
			LOG_XDEBUG("%s:",xevent_string(ev.xevent.type));

			switch (ev.xevent.type) {
			case KeyPress:
			case ButtonPress:
				bind_handle(&ev.xevent.xkey);
				break;
#ifdef CONFIGREQ
			case ConfigureRequest:
				handle_configure_request(&ev.xevent.xconfigurerequest);
				break;
#endif
			case ConfigureNotify:
				handle_configure_notify(&ev.xevent.xconfigure);
				break;
			case MapRequest:
				handle_map_request(&ev.xevent.xmaprequest);
				break;
			case ColormapNotify:
				handle_colormap_change(&ev.xevent.xcolormap);
				break;
			case EnterNotify:
				handle_enter_event(&ev.xevent.xcrossing);
				break;
			case PropertyNotify:
				handle_property_change(&ev.xevent.xproperty);
				break;
			case UnmapNotify:
				handle_unmap_event(&ev.xevent.xunmap);
				break;
			case MappingNotify:
				handle_mappingnotify_event(&ev.xevent.xmapping);
				break;
			case ClientMessage:
				handle_client_message(&ev.xevent.xclient);
				break;
			default:
#ifdef SHAPE
				if (display.have_shape && ev.xevent.type == display.shape_event) {
					handle_shape_event(&ev.xshape);
					break;
				}
#endif
#ifdef RANDR
				if (display.have_randr && ev.xevent.type == display.randr_event_base + RRScreenChangeNotify) {
					handle_randr_event(&ev.xrandr);
					break;
				}
#endif
				LOG_XDEBUG("%s\n", xevent_string(ev.xevent.type));
				break;
			}
		}

		// Scan list for clients flagged to be removed
		if (need_client_tidy) {
			struct list *iter, *niter;
			need_client_tidy = 0;
			for (iter = clients_tab_order; iter; iter = niter) {
				struct client *c = iter->data;
				niter = iter->next;
				if (c->remove)
					client_remove(c);
			}
		}
	}
}
