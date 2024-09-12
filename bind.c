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
#include <string.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <X11/XKBlib.h>
#include <X11/keysymdef.h>

#include "bind.h"
#include "client.h"
#include "display.h"
#include "evilwm.h"
#include "func.h"
#include "list.h"
#include "log.h"
#include "screen.h"
#include "util.h"
#include "xalloc.h"

// Configurable modifier bits.
#define KEY_STATE_MASK ( ShiftMask | ControlMask | Mod1Mask | \
                         Mod2Mask | Mod3Mask | Mod4Mask | Mod5Mask )

// Map modifier name to mask

struct name_to_modifier name_to_modifier[] = {
	{ "mask1",      0 },
	{ "mask2",      0 },
	{ "altmask",    0 },
	{ "shift",      ShiftMask },
	// { "caps",       LockMask },
	{ "control",    ControlMask }, { "ctrl",       ControlMask },
	{ "mod1",       Mod1Mask }, { "alt",        Mod1Mask }, // alt/meta
	{ "mod2",       Mod2Mask }, // numlock
	{ "mod3",       Mod3Mask }, // iso shift5
	{ "mod4",       Mod4Mask }, // super/hyper
	{ "mod5",       Mod5Mask }, // iso shift3 (altgr)
};
#define NUM_NAME_TO_MODIFIER (int)(sizeof(name_to_modifier) / sizeof(name_to_modifier[0]))

// All bindable functions present the same call interface.  'sptr' should point
// to a relevant data structure (controlled by flags FL_SCREEN or FL_CLIENT).

typedef void (*func_dispatch)(void *sptr, XEvent *e, unsigned flags);

// Maps a user-specifiable function name to internal function and the base
// flags required to call it, including whether 'sptr' should be (struct screen
// *) or (struct client *).  Futher flags may be ORed in.

struct function_def {
        const char *name;
        func_dispatch func;
        unsigned flags;
};

static struct function_def name_to_func[] = {
	{ "delete", func_delete,    FL_CLIENT|0 },
	{ "kill",   func_delete,    FL_CLIENT|1 },
	{ "dock",   func_dock,      FL_CLIENT },
	{ "docks",  func_docks,     FL_SCREEN },
	{ "info",   func_info,      FL_CLIENT },
	{ "lower",  func_lower,     FL_CLIENT },
	{ "move",   func_move,      FL_CLIENT },
	{ "next",   func_next,      0 },
	{ "raise",  func_raise,     FL_CLIENT },
	{ "resize", func_resize,    FL_CLIENT },
	{ "spawn",  func_spawn,     0 },
	{ "vdesk",  func_vdesk,     FL_SCREEN },
	{ "fix",    func_fix,       FL_CLIENT },
	{ "binds",  func_binds,     FL_SCREEN },
};
#define NUM_NAME_TO_FUNC (int)(sizeof(name_to_func) / sizeof(name_to_func[0]))

// Map flag name to flag

static struct {
	const char *name;
	unsigned flags;
} name_to_flags[] = {
	{ "up",             FL_UP },
	{ "u",              FL_UP },
	{ "on",             FL_UP },
	{ "down",           FL_DOWN },
	{ "d",              FL_DOWN },
	{ "off",            FL_DOWN },
	{ "left",           FL_LEFT },
	{ "l",              FL_LEFT },
	{ "right",          FL_RIGHT },
	{ "r",              FL_RIGHT },
	{ "top",            FL_TOP },
	{ "bottom",         FL_BOTTOM },
	{ "relative",       FL_RELATIVE },
	{ "rel",            FL_RELATIVE },
	{ "toggle",         FL_TOGGLE },
	{ "vertical",       FL_VERT },
	{ "v",              FL_VERT },
	{ "horizontal",     FL_HORZ },
	{ "h",              FL_HORZ },
};
#define NUM_NAME_TO_FLAGS (int)(sizeof(name_to_flags) / sizeof(name_to_flags[0]))

// Lists of built-in binds.

static struct {
	const char *ctl;
	const char *func;
} control_builtins[] = {
	// Button controls
	{ "mask2+button1",                "move" },
	{ "mask2+button2",                "resize" },
	{ "mask2+button3",                "lower" },
	// TODO: button4-9 scroll u/d, hscroll l/r or r/l, bk/fw

	// Client misc
	{ "mask2+Tab",              "next" },
	{ "mask1+Return",           "spawn" },
	{ "mask1+Escape",           "delete" },
	{ "mask1+altmask+Escape",   "kill" },
	{ "mask1+i",                "info" },
	{ "mask1+Insert",           "lower" },
	{ "mask1+KP_Insert",        "lower" },
	{ "mask1+f",                "fix,toggle" },

	// Move client
#ifdef QWERTZ_KEYMAP
	{ "mask1+z",                "move,top+left" },
#else
	{ "mask1+y",                "move,top+left" },
#endif
	{ "mask1+u",                "move,top+right" },
	{ "mask1+h",                "move,relative+left" },
	{ "mask1+j",                "move,relative+down" },
	{ "mask1+k",                "move,relative+up" },
	{ "mask1+l",                "move,relative+right" },
	{ "mask1+b",                "move,bottom+left" },
	{ "mask1+n",                "move,bottom+right" },

	// Resize client
	{ "mask1+altmask+h",        "resize,relative+left" },
	{ "mask1+altmask+j",        "resize,relative+down" },
	{ "mask1+altmask+k",        "resize,relative+up" },
	{ "mask1+altmask+l",        "resize,relative+right" },
	{ "mask1+x",                "resize,toggle+v+h" },
	{ "mask1+equal",            "resize,toggle+v" },
	{ "mask1+altmask+equal",    "resize,toggle+h" },

	// Screen misc
	{ "mask1+d",                "docks,toggle" },
	{ "mask1+Multi_key",        "binds,toggle" },

	// Virtual desktops
	{ "mask1+1",                "vdesk,0" },
	{ "mask1+2",                "vdesk,1" },
	{ "mask1+3",                "vdesk,2" },
	{ "mask1+4",                "vdesk,3" },
	{ "mask1+5",                "vdesk,4" },
	{ "mask1+6",                "vdesk,5" },
	{ "mask1+7",                "vdesk,6" },
	{ "mask1+8",                "vdesk,7" },
	{ "mask1+a",                "vdesk,toggle" },
	{ "mask1+Left",             "vdesk,relative+down" },
	{ "mask1+Right",            "vdesk,relative+up" },
	{ "mask1+Down",             "vdesk,relative+left" },
	{ "mask1+Up",               "vdesk,relative+right" },
};
#define NUM_CONTROL_BUILTINS (int)(sizeof(control_builtins) / sizeof(control_builtins[0]))

void putdefaultbinds(void) {
	for(unsigned i = 0; i < NUM_CONTROL_BUILTINS; i++) {
		fputs("bind ", stdout);
		fputs(control_builtins[i].ctl, stdout);
		fputs("=", stdout);
		puts(control_builtins[i].func);
	}
}

struct bind {
	// KeyPress or ButtonPress
	int type;
	// Bound key or button
	union {
		KeySym key; // (ulong)
		unsigned button;
	} control;
	// Modifier state
	unsigned state;
	// Dispatch function
	func_dispatch func;
	// Control flags
	unsigned flags;
};

static struct list *controls = NULL;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// String-to-value mapping helper functions

static struct name_to_modifier *modifier_by_name(const char *name) {
	for (int i = 0; i < NUM_NAME_TO_MODIFIER; i++) {
		if (!strcmp(name_to_modifier[i].name, name)) {
			return &name_to_modifier[i];
		}
	}
	return NULL;
}

static unsigned button_by_name(const char *name) {
	const char *pre = "button";
	if (strncmp(name,pre,strlen(pre))==0) return strtoul(name+strlen(pre),NULL,0);
	return 0;
}

static struct function_def *func_by_name(const char *name) {
	for (int i = 0; i < NUM_NAME_TO_FUNC; i++) {
		if (!strcmp(name_to_func[i].name, name)) {
			return &name_to_func[i];
		}
	}
	return NULL;
}

static unsigned flags_by_name(const char *name) {
	for (int i = 0; i < NUM_NAME_TO_FLAGS; i++) {
		if (!strcmp(name_to_flags[i].name, name)) {
			return name_to_flags[i].flags;
		}
	}
	LOG_ERROR("Flag '%s' unknown\n",name);
	return 0;
}

// Manage list of binds
static struct list *controlstash = NULL;
void stashbinds(struct screen *s) {
	if (controlstash) return;
	LOG_DEBUG("stashing binds");
	controlstash = controls;
	controls = NULL;
	struct list *buttoncontrols = NULL;
	for (struct list *l = controlstash; l; l = l->next) {
		struct bind *b = l->data;
		if (b->func == func_binds && b->flags&(FL_TOGGLE|FL_UP)) {
			//keep binds for, well, reapplying the binds
			LOG_DEBUG("!");
			controls = list_prepend(controls, b);
		} else if (b->type == ButtonPress) {
			LOG_DEBUG(":");
			buttoncontrols = list_prepend(buttoncontrols, b);
			//ungrab buttons on all clients
			for (struct list *lc = clients_tab_order; lc; lc = lc->next) {
				struct client *c = lc->data;
				if (c->screen != s) continue;
				XUngrabButton(display.dpy, b->control.button, b->state, c->parent);
			}
		} else {
			LOG_DEBUG(".");
		}
	}
	LOG_DEBUG("\n");
	if (!controls) {
		LOG_ERROR("stashbinds(): No surviving controls! Binding mask1+altmask+Multi_key=binds,up\n");
		struct bind *b = xmalloc(sizeof(*b));
		*b=(struct bind){
			.type=KeyPress,.control.key=XStringToKeysym("Multi_key"),
			.state = modifier_by_name("mask1")->value | modifier_by_name("altmask")->value,
			.func=func_binds,.flags=FL_SCREEN|FL_UP,
		};
		controls = list_prepend(controls, b);
	}
	//rebind kept controls
	bind_grab_for_screen(s);
	//and add button controls back to the list, for border clicks
	struct list *l = controls;
	while (l->next) l=l->next;
	l->next = buttoncontrols;
}
void unstashbinds(struct screen *s) {
	if (!controlstash) return;
	LOG_DEBUG("unstashing binds\n");
	while (controls) controls = list_delete(controls, controls->data);
	controls = controlstash;
	controlstash = NULL;
	bind_grab_for_screen(s);
	// go through and grab buttons on clients
	for (struct list *l = clients_tab_order; l; l = l->next) {
		struct client *c = l->data;
		if (c->screen != s) continue;
		bind_grab_for_client(c);
	}
}
void togglebinds(struct screen *s) {
	if (controlstash) unstashbinds(s);
	else stashbinds(s);
}

void bind_unset(void) {
	// unbind _all_ controls
	// note, does not ungrab keysyms & buttons
	while (controls) {
		struct bind *b = controls->data;
		controls = list_delete(controls, b);
		free(b);
	}
}

void bind_defaults(void) {
	// then rebind what's configured
	for (int i = 0; i < NUM_CONTROL_BUILTINS; i++) {
		bind_control(control_builtins[i].ctl, control_builtins[i].func);
	}
}

// set value of name_to_modifier with name modname to the state represented in modstr
void bind_modifier(const char *modname, const char *modstr) {
	if (!modstr)
		return;
	struct name_to_modifier *destm = modifier_by_name(modname);
	if (!destm)
		return;
	destm->value = 0;

	// parse modstr
	char *moddup = xstrdup(modstr);
	for (char *tmp = strtok(moddup, ",+"); tmp; tmp = strtok(NULL, ",+")) {
		// is this a modifier?
		struct name_to_modifier *m = modifier_by_name(tmp);
		if (m) {
			destm->value |= m->value;
			continue;
		}
	}
	free(moddup);
	if (!strcmp(modname, "altmask"))
		altmask = destm->value;
}

void bind_control(const char *ctlname, const char *func) {
	// Parse control string
	char *ctldup = xstrdup(ctlname);
	if (!ctldup)
		return;
	struct bind *newbind = xmalloc(sizeof(*newbind));
	*newbind = (struct bind){0};

	// newbind->control & ->state
	for (char *toksv=NULL, *tok=strtok_r(ctldup,",+",&toksv); tok; tok=strtok_r(NULL,",+",&toksv)) {
		// is this a modifier?
		struct name_to_modifier *m = modifier_by_name(tok);
		if (m) {
			newbind->state |= m->value;
			continue;
		}

		// only consider first key or button listed
		if (newbind->type)
			continue;

		// maybe it's a button?
		unsigned b = button_by_name(tok);
		if (b) {
			newbind->type = ButtonPress;
			newbind->control.button = b;
			continue;
		}

		// ok, see if it's recognised as a key name
		KeySym k = XStringToKeysym(tok);
		if (k != NoSymbol) {
			newbind->type = KeyPress;
			newbind->control.key = k;
			continue;
		}
		LOG_ERROR("Errant token '%s' in bind %s=%s\n",tok,ctlname,func);
	}
	free(ctldup);

	// No known control type?  Abort.
	if (!newbind->type) {
		LOG_DEBUG("typeless bind %s\n",ctlname);
		free(newbind);
		return;
	}

	if (newbind->type == ButtonPress && !newbind->state)
		newbind->state = grabmask2;

	// always unbind any existing matching control
	for (struct list *l = controls; l; l = l->next) {
		struct bind *b = l->data;
		if (b->state != newbind->state) continue;
		if (b->type != newbind->type) continue;
		if ((newbind->type == KeyPress    && b->control.key    == newbind->control.key)
		 || (newbind->type == ButtonPress && b->control.button == newbind->control.button)) {
			controls = list_delete(controls, b);
			free(b);
			LOG_DEBUG("Dupe %s bind deleted\n",ctlname);
			break;
		}
	}

	// empty function definition implies unbind.  already done, so return.
	if (!func || !*func) {
		free(newbind);
		LOG_DEBUG("unbound %s\n",ctlname);
		return;
	}

	// parse the second string for function & flags

	char *funcdup = xstrdup(func);
	if (!funcdup)
		return;

	for (char *cur = strtok(funcdup, ",+"); cur; cur = strtok(NULL, ",+")) {
		if (!*cur) LOG_ERROR("bind_controls(): nul token in funcstr\n");

		// function name?
		struct function_def *fn = func_by_name(cur);
		if (fn) {
			newbind->func = fn->func;
			newbind->flags = fn->flags;
			continue;
		}

		// a simple number?
		char *strtolend;
		long num = strtol(cur, &strtolend, 0);
		if (!*strtolend) {
			newbind->flags &= ~FL_VALUEMASK;
			newbind->flags |= num & FL_VALUEMASK;
			continue;
		}

		// treat it as a flag name then
		newbind->flags |= flags_by_name(cur);
	}

	if (newbind->flags&FL_CLIENT && newbind->flags&FL_SCREEN) {
		LOG_ERROR("Bind func '%s' has both screen and client flags!\n", func);
	}

	if (newbind->func) {
		controls = list_prepend(controls, newbind);
	} else {
		free(newbind);
		LOG_DEBUG("Bind dropped!\n");
	}

	free(funcdup);
}

static void grab_keysym(KeySym keysym, unsigned modmask, Window w) {
	KeyCode keycode = XKeysymToKeycode(display.dpy, keysym);
	XGrabKey(display.dpy, keycode, modmask, w, True,
		 GrabModeAsync, GrabModeAsync);
	XGrabKey(display.dpy, keycode, modmask|LockMask, w, True,
		 GrabModeAsync, GrabModeAsync);
	if (numlockmask) {
		XGrabKey(display.dpy, keycode, modmask|numlockmask, w, True,
			 GrabModeAsync, GrabModeAsync);
		XGrabKey(display.dpy, keycode, modmask|numlockmask|LockMask, w, True,
			 GrabModeAsync, GrabModeAsync);
	}
}

static void grab_button(unsigned button, unsigned modifiers, Window w) {
	XGrabButton(display.dpy, button, modifiers, w,
		    False, ButtonPressMask | ButtonReleaseMask,
		    GrabModeAsync, GrabModeSync, None, None);
	XGrabButton(display.dpy, button, modifiers|LockMask, w,
		    False, ButtonPressMask | ButtonReleaseMask,
		    GrabModeAsync, GrabModeSync, None, None);
	if (numlockmask) {
		XGrabButton(display.dpy, button, modifiers|numlockmask, w,
			    False, ButtonPressMask | ButtonReleaseMask,
			    GrabModeAsync, GrabModeSync, None, None);
		XGrabButton(display.dpy, button, modifiers|numlockmask|LockMask, w,
			    False, ButtonPressMask | ButtonReleaseMask,
			    GrabModeAsync, GrabModeSync, None, None);
	}
}

void bind_grab_for_screen(struct screen *s) {
	XUngrabKey(display.dpy, AnyKey, AnyModifier, s->root);
	XUngrabButton(display.dpy, AnyButton, AnyModifier, s->root);

	for (struct list *l = controls; l; l = l->next) {
		struct bind *b = l->data;
		if (b->type == KeyPress) {
			grab_keysym(b->control.key, b->state, s->root);
		}
		else if (b->type == ButtonPress && !(b->flags & FL_CLIENT)) {
			grab_button(b->control.button, b->state, s->root);
		}
	}
}

void bind_grab_for_client(struct client *c) {
	XUngrabButton(display.dpy, AnyButton, AnyModifier, c->parent);

	for (struct list *l = controls; l; l = l->next) {
		struct bind *b = l->data;
		if (b->type == ButtonPress) {
			grab_button(b->control.button, b->state, c->parent);
		}
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Handle keyboard & mousebutton events.
// XButtonEvent & XKeyEvent are identical but for the unsigned button/keycode being named differently

void bind_handle(XKeyEvent *e, int doagain) {
	if (e->type != KeyPress && e->type != ButtonPress) return;
	for (struct list *l = controls; l; l = l->next) {
		struct bind *bind = l->data;
		if (bind->type != e->type) continue;
		if (bind->type == KeyPress
			&& XkbKeycodeToKeysym(display.dpy, e->keycode, 0, 0) != bind->control.key) continue;
		if (bind->type == ButtonPress
			&& ((XButtonEvent *)e)->button != bind->control.button) continue;
		if ( (e->state & KEY_STATE_MASK & ~numlockmask)
			!= (bind->state & ~numlockmask) ) continue;
		void *sptr = NULL;
		if (bind->flags & FL_CLIENT)
			sptr = bind->type == KeyPress ? current : find_client(e->window);
		if (bind->flags & FL_SCREEN)
			sptr = find_current_screen();
		if (bind->flags & (FL_CLIENT|FL_SCREEN) && !sptr)
			return;
		bind->func(sptr, (XEvent *)e, bind->flags);
		return;
	}
	if (doagain) {
		e->state ^= grabmask2;
		bind_handle(e, 0);
		e->state ^= grabmask2;
	}
	else LOG_ERROR("Unfound bind! (%s = %lx, state = %x)\n",
		e->type==ButtonPress ? "button" : "key",
		e->type==ButtonPress ? ((XButtonEvent *)e)->button : XkbKeycodeToKeysym(display.dpy, e->keycode, 0, 0),
		e->state
	);
}
