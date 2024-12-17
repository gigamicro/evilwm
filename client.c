/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Client management.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_MATH_H
#include <math.h>
#endif

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#ifdef SHAPE
#include <X11/extensions/shape.h>
#endif

#include "client.h"
#include "display.h"
#include "evilwm.h"
#include "ewmh.h"
#include "list.h"
#include "log.h"
#include "screen.h"
#include "util.h"

// Client tracking information
struct list *clients_tab_order = NULL; // head is most recent
struct list *clients_mapping_order = NULL; // head is eldest
struct list *clients_stacking_order = NULL; // head is furthest back
struct client *current = NULL;

// Get WM_NORMAL_HINTS property.  Populates appropriate parts of the client
// structure and returns the hint flags (which indicates whether sizes or
// positions were user- or program-specified).

long get_wm_normal_hints(struct client *c) {
	long flags;
	long dummy;
	XSizeHints *size = XAllocSizeHints();

	LOG_XENTER("XGetWMNormalHints(window=%lx)", (unsigned long)c->window);
	XGetWMNormalHints(display.dpy, c->window, size, &dummy);
	debug_wm_normal_hints(size);
	LOG_XLEAVE();

	flags = size->flags;

	if (flags & PMinSize) {
		c->min_width = size->min_width;
		c->min_height = size->min_height;
	} else {
		c->min_width = c->min_height = 0;
	}

	if (flags & PMaxSize) {
		c->max_width = size->max_width;
		c->max_height = size->max_height;
	} else {
		c->max_width = c->max_height = 0;
	}

	if (flags & PBaseSize) {
		c->base_width = size->base_width;
		c->base_height = size->base_height;
	} else {
		c->base_width = c->min_width;
		c->base_height = c->min_height;
	}

	c->width_inc = c->height_inc = 1;
	if (flags & PResizeInc) {
		c->width_inc = size->width_inc ? size->width_inc : 1;
		c->height_inc = size->height_inc ? size->height_inc : 1;
	}

	if (!(flags & PMinSize)) {
		c->min_width = c->base_width + c->width_inc;
		c->min_height = c->base_height + c->height_inc;
	}

	if (flags & PWinGravity) {
		c->win_gravity_hint = size->win_gravity;
	} else {
		c->win_gravity_hint = NorthWestGravity;
	}
	c->win_gravity = c->win_gravity_hint;

	XFree(size);
	return flags;
}

// Determine EWMH "window type" and update client flags accordingly.  The only
// windows we currently treat any differently are docks.

void get_window_type(struct client *c) {
	unsigned type = ewmh_get_net_wm_window_type(c->window);
	update_window_type_flags(c, type);
}

void update_window_type_flags(struct client *c, unsigned type) {
	c->is_dock = (type & EWMH_WINDOW_TYPE_DOCK) ? 1 : 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Managed windows are all reparented, so most client operations act on the
// parent window.

// find_client() is used all over the place.  Return the client that has
// specified window as either window or parent.  NULL if not found.

struct client *find_client(Window w) {
	for (struct list *iter = clients_tab_order; iter; iter = iter->next) {
		struct client *c = iter->data;
		if (w == c->parent || w == c->window)
			return c;
	}
	return NULL;
}

static int imin(int a, int b) {
	return (a < b) ? a : b;
}

static int imax(int a, int b) {
	return (a > b) ? a : b;
}

// Determine which monitor to consider "closest" for the client.  This is
// either largest intersection ratio or, if no intersection with any monitor
// (ie not visible), closest mid points.
//
// Largest ratio should mean that if one monitor is a subset of another, a
// window within it will be considered to be "closest" to the smaller monitor
// for the purpose of maximising, etc.
//
// 'intersects' is set to represent whether client intersects with any monitor.

struct monitor *client_monitor(struct client *c, Bool *intersects) {
	int cx1 = c->x - c->border;
	int cy1 = c->y - c->border;
	int cx2 = cx1 + c->width + c->border*2;
	int cy2 = cy1 + c->height + c->border*2;
#ifdef HAVE_MATH_H
	int cmidx = (cx1 + cx2)/2;
	int cmidy = (cy1 + cy2)/2;
#endif

	struct monitor *best = NULL;
	Bool have_intersection = 0;
	double best_area_ratio = 0.0;
#ifdef HAVE_MATH_H
	double best_distance = 0.0;
#endif

	for (int i = 0; i < c->screen->nmonitors; i++) {
		struct monitor *m = &c->screen->monitors[i];
		int mx2 = m->x + m->width;
		int my2 = m->y + m->height;

		int iw = imax(0, imin(mx2, cx2) - imax(m->x, cx1));
		int ih = imax(0, imin(my2, cy2) - imax(m->y, cy1));
		int iarea = iw * ih;

		// XXX if you're building on a platform without any floating
		// point, you could consider simply returning the first monitor
		// for which an intersection is found.

		if (iarea > 0) {
			// Found an intersection.  Higher ratio wins.
			double iarea_ratio = (double)iarea / (double)m->area;
			if (!have_intersection || iarea_ratio > best_area_ratio) {
				have_intersection = 1;
				best_area_ratio = iarea_ratio;
				best = m;
				continue;
			}
			// Break ties in favor of matching mon_name
			else if (iarea_ratio == best_area_ratio
				&& m->name == c->mon_name)
				best = m;
		}

		// Otherwise, any previous intersection trumps distance.
		if (have_intersection)
			continue;

#ifdef HAVE_MATH_H
		// No intersections yet, calculate distance between midpoints.
		int mmidx = (m->x + mx2)/2;
		int mmidy = (m->y + my2)/2;
		int dx = abs(cmidx - mmidx);
		int dy = abs(cmidy - mmidy);
		double d = sqrt(dx*dx + dy*dy);

		if (!best || d < best_distance) {
			best_distance = d;
			best = m;
		}
#endif
	}

	if (!best) {
		best = &c->screen->monitors[0];
	}
	if (intersects) {
		*intersects = have_intersection;
	}
	return best;
}

// "Hides" the client (unmaps and flags it as iconified).  Used to simulate
// virtual desktops by hiding all clients not on the current vdesk.

void client_hide(struct client *c) {
	LOG_DEBUG("hiding window=%lx\n", (unsigned long)c->window);
	c->ignore_unmap++;  // ignore unmap so we don't remove client
	XUnmapWindow(display.dpy, c->parent);
	set_wm_state(c, IconicState);
	if (current == c) {
		client_select(NULL);
	}
}

// Show client (and flag it as normal - not iconified).  Used for vdesks and
// initial managing of client.

void client_show(struct client *c) {
	LOG_DEBUG("showing window=%lx\n",(unsigned long)c->window);
	XMapWindow(display.dpy, c->parent);
	set_wm_state(c, NormalState);
}

// Test for overlap
_Bool client_client(struct client *c, struct client *cc) {
	int cx1 = c->x - c->border;
	int cy1 = c->y - c->border;
	int cx2 = cx1 + c->width + c->border;
	int cy2 = cy1 + c->height + c->border;
	int ccx1 = cc->x - cc->border;             if ( ccx1 > cx2 ) return 0;
	int ccy1 = cc->y - cc->border;             if ( ccy1 > cy2 ) return 0;
	int ccx2 = ccx1 + cc->width  + cc->border; if ( cx1 > ccx2 ) return 0;
	int ccy2 = ccy1 + cc->height + cc->border; if ( cy1 > ccy2 ) return 0;
	return 1;
}

// Place 'under' directly under 'over'
// Maintains clients_stacking_order list and EWMH hints
void client_under(struct client *under, struct client *over) {
	if (!under) {
		LOG_ERROR("client_under(): null under!\n");
		return;
	}
	if (!over) {
		LOG_XDEBUG("XRaiseWindow(window=%lx,parent=%lx)\n", (unsigned long)under->window, (unsigned long)under->parent);
		XRaiseWindow(display.dpy, under->parent);
		clients_stacking_order=list_to_tail(clients_stacking_order,under);
		ewmh_set_net_client_list_stacking(under->screen);
		return;
	}
	LOG_XDEBUG("XRestackWindows({window=%lx,parent=%lx},{window=%lx,parent=%lx})\n",
		(unsigned long)over->window, (unsigned long)over->parent,
		(unsigned long)under->window, (unsigned long)under->parent);
	XRestackWindows(display.dpy, (Window[]){ over->parent, under->parent }, 2);
	clients_stacking_order = list_delete(clients_stacking_order, under); // XXX can this be omitted?
	if (clients_stacking_order->data == over) {
		clients_stacking_order = list_prepend(clients_stacking_order, under);
	} else {
		struct list *pover = list_find_prev(clients_stacking_order, over);
		pover->next = list_prepend(pover->next, under);
	}
	ewmh_set_net_client_list_stacking(under->screen);
}

// Raise client
// Put the client directly under the client one above the highest matching client
void client_raise(struct client *c) {
	if (!c) {
		LOG_ERROR("client_raise(): null client!\n");
		return;
	}
#if defined(LOWERRAISE_OVERLAP) || defined(LOWERRAISE_VISIBLE)
	struct list *iter = clients_stacking_order;
	while (iter && iter->data!=c) iter = iter->next;
	if (!iter) { // list-/>c, must be added
		iter = clients_stacking_order = list_prepend(clients_stacking_order, c);
	}
	struct list *cnode = iter;
	struct list *last = iter;
	while ((iter=iter->next)) {
		struct client *cc = iter->data;
		if (!cc) continue; // skip null data
		if (cc==c) LOG_DEBUG("duplicate node for window=%lx in clients_stacking_order\n", c->window);
		if (!is_visible(cc)) continue; // wrong vdesk
#ifdef LOWERRAISE_OVERLAP
		if (!client_client(c,cc)) continue; // no collide
#endif
		last = iter;
	}
	// last is last/highest overlap
	if (cnode==last) return; // already on top
	if (last->next) client_under(c,last->next->data);
	else // tail
#endif
	client_under(c,NULL);
}

// Lower client
// Put the client directly under the furthest back matching client
void client_lower(struct client *c) {
	if (!c) {
		LOG_ERROR("client_lower(): null client!\n");
		return;
	}
	struct list *iter = clients_stacking_order;
	if (!iter) { // in an environment of no clients, raising is as lowering
		LOG_ERROR("client_lower(): null list!\n");
		LOG_XDEBUG("XLowerWindow(window=%lx,parent=%lx)\n", (unsigned long)c->window, (unsigned long)c->parent);
		XLowerWindow(display.dpy, c->parent);
		clients_stacking_order = list_to_head(clients_stacking_order, c);
		ewmh_set_net_client_list_stacking(c->screen);
		return;
	}
#if defined(LOWERRAISE_OVERLAP) || defined(LOWERRAISE_VISIBLE)
	iter=&(struct list){ .next=iter, .data=NULL };
	while ((iter=iter->next)) {
		struct client *cc = iter->data;
		if (!cc) continue; // skip null data
		if (cc==c) return; // nothing underneath
		if (!is_visible(cc)) continue; // wrong vdesk
#ifdef LOWERRAISE_OVERLAP
		if (!client_client(c,cc)) continue; // no collide
#endif
		break;
	}
	// iter is first/lowest overlap
	if (iter) client_under(c,iter->data);
	else // no overlap & list-/>c
		client_under(c,clients_stacking_order->data);
#else
	if (iter->data==c) return; // already at bottom
	client_under(c,iter->data);
#endif
}

// Set window state.  This is either NormalState (visible), IconicState
// (hidden) or WithdrawnState (removing).

void set_wm_state(struct client *c, int state) {
	// Using "long" for the type of "data" looks wrong, but the fine people
	// in the X Consortium defined it this way (even on 64-bit machines).
	long data[2] = { state, None };
	XChangeProperty(display.dpy, c->window, X_ATOM(WM_STATE), X_ATOM(WM_STATE), 32,
			PropModeReplace, (unsigned char *)data, 2);
}

// Inform client of changed geometry by sending ConfigureNotify to its window.

void send_config(struct client *c) {
	XEvent ev = {
		.xconfigure = {
			.type = ConfigureNotify,
			.event = c->window,
			.window = c->window,
			.x = c->x,
			.y = c->y,
			.width = c->width,
			.height = c->height,
			.border_width = 0,
			.above = None,
			.override_redirect = False
		}
	};
	XSendEvent(display.dpy, c->window, False, StructureNotifyMask, &ev);
}

// Offset client to show border according to window's gravity.  e.g.,
// SouthEastGravity will offset the client up and left by the supplied border
// width.

void client_gravitate(struct client *c, int bw) {
	int dx = 0, dy = 0;
	switch (c->win_gravity) {
	default:
	case NorthWestGravity:
		dx = bw;
		dy = bw;
		break;
	case NorthGravity:
		dy = bw;
		break;
	case NorthEastGravity:
		dx = -bw;
		dy = bw;
		break;
	case EastGravity:
		dx = -bw;
		break;
	case CenterGravity:
		break;
	case WestGravity:
		dx = bw;
		break;
	case SouthWestGravity:
		dx = bw;
		dy = -bw;
		break;
	case SouthGravity:
		dy = -bw;
		break;
	case SouthEastGravity:
		dx = -bw;
		dy = -bw;
		break;
	}
	// Don't gravitate if client would be maximised along either axis
	// (unless it's offset already).
	if (c->x != 0 || c->width != DisplayWidth(display.dpy, c->screen->screen)) {
		c->x += dx;
	}
	if (c->y != 0 || c->height != DisplayHeight(display.dpy, c->screen->screen)) {
		c->y += dy;
	}
}

// Activate a client.  Colours its border (and uncolours the
// previously-selected), installs any colourmap, sets input focus and updates
// EWMH properties.

void client_select(struct client *c) {
	struct client *old_current = current;
	if (old_current)
		XSetWindowBorder(display.dpy, current->parent, current->screen->bg.pixel);
	if (c) {
		XSetWindowBorder(display.dpy, c->parent, is_fixed(c) ? c->screen->fc.pixel : c->screen->fg.pixel);
		XInstallColormap(display.dpy, c->cmap);
		XSetInputFocus(display.dpy, c->window, RevertToPointerRoot, CurrentTime);
	}

	current = c;

	if (old_current)
		ewmh_set_net_wm_state(old_current);
	if (c)
		ewmh_set_net_wm_state(c);
}

int client_point(struct client *c, int margin_l, int margin_u, int margin_r, int margin_d) {
	int window_x; int window_y;
	int root_x; int root_y;
	Window root;
	//dummy:
	Window child;// that the pointer is in
	unsigned int mask;//kb modifiers
	if (!XQueryPointer(display.dpy, c->window, &root, &child, &root_x, &root_y, &window_x, &window_y, &mask))
		return setmouse(c->window, c->width/2, c->height/2); // not found
	window_x = root_x - c->x;
	window_y = root_y - c->y;
	int mclamp_r = c->width  - margin_r;
	int mclamp_d = c->height - margin_d;
	int x = window_x; int y = window_y;
	// LOG_DEBUG("client_point: margins (%i,%i,%i,%i) on client with (%i,%i) and pointer at (%i,%i) for hclamps (%i,%i)\n",
	// 	margin_l, margin_u, margin_r, margin_d,   c->width,c->height,   x,y,   mclamp_r, mclamp_d );

	if (y < -c->border || y > c->height + c->border) {}
	else if (margin_l && x < margin_l) x = margin_l;
	else if (margin_r && x > mclamp_r) x = mclamp_r;
	if (x!=window_x && margin_l + margin_r >= c->width ) x = c->width /2;

	if (x < -c->border || x > c->width  + c->border) {}
	else if (margin_u && y < margin_u) y = margin_u;
	else if (margin_d && y > mclamp_d) y = mclamp_d;
	if (y!=window_y && margin_u + margin_d >= c->height) y = c->height/2;

	if (x!=window_x || y!=window_y) return setmouse(root, c->x+x, c->y+y);
	return 0;
}

// Move a client to a specific vdesk.  If that means it should no longer be
// visible, hide it.

void client_to_vdesk(struct client *c, unsigned vdesk) {
	if (valid_vdesk(vdesk)) {
		LOG_DEBUG("window=%lx to vdesk %u\n",(unsigned long)c->window,vdesk);
		_Bool was_visible = is_visible(c);
		c->vdesk = vdesk;
		if (is_visible(c))
			{ if (!was_visible) client_show(c); }
		else
			if (was_visible) client_hide(c);
		if (current == c) client_select(c); // border color
		ewmh_set_net_wm_desktop(c);
	}
}

// Stop managing a client.  Undoes any transformations that were made when
// managing it.

void client_remove(struct client *c) {
	LOG_ENTER("client_remove(window=%lx, %s)", (unsigned long)c->window, c->remove ? "withdrawing" : "wm quitting");

	// Flag to ignore any X errors we trigger.  The window may well already
	// have been deleted from the server, so anything we try to do to it
	// here would raise one.
	removing = c->window;
	removing_parent = c->parent;

	// ICCCM 4.1.3.1
	// "When the window is withdrawn, the window manager will either change
	//  the state field's value to WithdrawnState or it will remove the
	//  WM_STATE property entirely."
	//
	// EWMH 1.3
	// "The Window Manager should remove the property whenever a window is
	//  withdrawn but it should leave the property in place when it is
	//  shutting down." (both _NET_WM_DESKTOP and _NET_WM_STATE)

	if (c->remove) {
		LOG_DEBUG("setting WithdrawnState\n");
		if (c == current) {
			XSetInputFocus(display.dpy, PointerRoot, RevertToPointerRoot,
			               CurrentTime);
		}
		set_wm_state(c, WithdrawnState);
		ewmh_withdraw_client(c);
	} else {
		ewmh_remove_allowed_actions(c);
	}

	// Undo the geometry changes applied when we managed the client
	client_gravitate(c, -c->border);
	client_gravitate(c, c->old_border);
	c->x -= c->old_border;
	c->y -= c->old_border;

	// Reparent window back to the root
	XReparentWindow(display.dpy, c->window, c->screen->root, c->x, c->y);

	// Restore any old border
	XSetWindowBorderWidth(display.dpy, c->window, c->old_border);

	// Remove window from "save set": we are no longer its parent, so if we
	// die now, the window should be fine.
	XRemoveFromSaveSet(display.dpy, c->window);

	// Destroy parent window
	if (c->parent) {
		XDestroyWindow(display.dpy, c->parent);
	}

	// Remove from the client lists
	clients_tab_order = list_delete(clients_tab_order, c);
	clients_mapping_order = list_delete(clients_mapping_order, c);
	clients_stacking_order = list_delete(clients_stacking_order, c);

	// If the wm is quitting, we'll remove the client list properties
	// soon enough, otherwise, update them:
	if (c->remove) {
		ewmh_set_net_client_list(c->screen);
		ewmh_set_net_client_list_stacking(c->screen);
	}

	// Deselect if this client were previously selected
	if (current == c) client_select(NULL);
	free(c);

#ifdef DEBUG
	{
		int i = 0;
		for (struct list *iter = clients_tab_order; iter; iter = iter->next)
			i++;
		LOG_DEBUG("free(), window count now %d\n", i);
	}
#endif

	// removing = None;
	LOG_LEAVE();
}

// Delete a window.  Sends WM_DELETE_WINDOW to a client if that protocol is
// found to be supported.  Otherwise (or if forced by setting kill_client), use
// XKillClient (terminates its connection to the server).

void send_wm_delete(struct client *c, int kill_client) {
	_Bool delete_supported = 0;
	int n;
	Atom *protocols;

	if (!kill_client && XGetWMProtocols(display.dpy, c->window, &protocols, &n)) {
		for (int i = 0; i < n; i++)
			if (protocols[i] == X_ATOM(WM_DELETE_WINDOW))
				delete_supported = 1;
		XFree(protocols);
	}
	if (delete_supported) {
		XEvent ev = {
			.xclient = {
				.type = ClientMessage,
				.window = c->window,
				.message_type = X_ATOM(WM_PROTOCOLS),
				.format = 32,
				.data.l = { X_ATOM(WM_DELETE_WINDOW), CurrentTime }
			}
		};
		XSendEvent(display.dpy, c->window, False, NoEventMask, &ev);
	} else {
		XKillClient(display.dpy, c->window);
	}
}

#ifdef SHAPE

// Query the shape "extents" applied to a window and duplicate them for the
// parent.

void set_shape(struct client *c) {
	Bool bounding_shaped;
	Bool b;  // dummy
	int i;  // dummy
	unsigned u;  // dummy

	if (!display.have_shape) return;

	// Logic to decide if we have a shaped window cribbed from fvwm-2.5.10.
	// Previous method (more than one rectangle returned from
	// XShapeGetRectangles) worked _most_ of the time.

	if (XShapeQueryExtents(display.dpy, c->window, &bounding_shaped, &i, &i,
				&u, &u, &b, &i, &i, &u, &u) && bounding_shaped) {
		LOG_DEBUG("Shape(window=%li)\n", c->window);
		XShapeCombineShape(display.dpy, c->parent, ShapeBounding, 0, 0,
				c->window, ShapeBounding, ShapeSet);
	}
}

#endif

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Compiling with -DINFOBANNER enables a client information window

#ifdef INFOBANNER

void create_info_window(struct client *c) {
        display.info_window = XCreateSimpleWindow(display.dpy, c->screen->root, -4, -4, 2, 2,
                        0, c->screen->fg.pixel, c->screen->fg.pixel);
        XMapRaised(display.dpy, display.info_window);
        update_info_window(c);
}

void update_info_window(struct client *c) {
        char *name;
        char buf[27];
        int namew, iwinx, iwiny, iwinw, iwinh;
        int width_inc = c->width_inc, height_inc = c->height_inc;

        if (!display.info_window)
                return;
        snprintf(buf, sizeof(buf), "%dx%d+%d+%d", (c->width-c->base_width)/width_inc,
                (c->height-c->base_height)/height_inc, c->x, c->y);
        iwinw = XTextWidth(display.font, buf, strlen(buf)) + 2;
        iwinh = display.font->max_bounds.ascent + display.font->max_bounds.descent;
        XFetchName(display.dpy, c->window, &name);
        if (name) {
                namew = XTextWidth(display.font, name, strlen(name));
                if (namew > iwinw)
                        iwinw = namew + 2;
                iwinh = iwinh * 2;
        }
        iwinx = c->x + c->border + c->width - iwinw;
        iwiny = c->y - c->border;
        if (iwinx + iwinw > DisplayWidth(display.dpy, c->screen->screen))
                iwinx = DisplayWidth(display.dpy, c->screen->screen) - iwinw;
        if (iwinx < 0)
                iwinx = 0;
        if (iwiny + iwinh > DisplayHeight(display.dpy, c->screen->screen))
                iwiny = DisplayHeight(display.dpy, c->screen->screen) - iwinh;
        if (iwiny < 0)
                iwiny = 0;
        XMoveResizeWindow(display.dpy, display.info_window, iwinx, iwiny, iwinw, iwinh);
        XClearWindow(display.dpy, display.info_window);
        if (name) {
                XDrawString(display.dpy, display.info_window, c->screen->invert_gc,
                                1, iwinh / 2 - 1, name, strlen(name));
                XFree(name);
        }
        XDrawString(display.dpy, display.info_window, c->screen->invert_gc, 1, iwinh - 1,
                        buf, strlen(buf));
}

void remove_info_window(void) {
        if (display.info_window)
                XDestroyWindow(display.dpy, display.info_window);
        display.info_window = None;
}

#endif
