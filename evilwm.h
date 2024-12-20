/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Default option values

#define DEF_FONT        "variable"
#define DEF_FG          "goldenrod"
#define DEF_BG          "grey50"
#define DEF_BW          1
#define DEF_FC          "blue"
#define DEF_VDESKS      8
#define DEF_VDESKSMOD   0
#define DEF_SNAP        0
#define DEF_KBPX        16
#define DEF_QUICKMOVE   1.0
#define DEF_QUICKMOVEMS 250
#define DEF_MASK1       "control+alt"
#define DEF_MASK2       "alt"
#define DEF_ALTMASK     "shift"
#define DEF_SOLIDDRAG   1
#define DEF_SOLIDSWEEP  0
#define DEF_DOCKS       1
#ifdef DEBIAN
#define DEF_TERM        "x-terminal-emulator"
#else
#define DEF_TERM        "xterm"
#endif
#ifndef VERSION
#define VERSION "?.?.?"
#endif

// Options

struct options {
	// Display string (e.g., ":0")
	char *display;
#ifdef FONT
	// Text font
	char *font;
#endif
	// Border colours
	char *fg;  // selected (foreground)
	char *bg;  // unselected (background)
	char *fc;  // fixed & selected

	// Border width
	int bw;

	// Number of virtual desktops
	unsigned vdesks;
	// Loop increment & vertical movement amount for vdesks
	unsigned modvdesks;

	// Snap to border distance
	int snap;

	// Default keyboard movement distance
	int kbpx;
	// multiplier when consecutive within 250ms
	double quickmove;
	int quickmovems;

	// Whole screen flag (ignore monitor information)
	int wholescreen;

	// Solid drag & sweep enable flags
	int solid_drag;
	int solid_sweep;

	// Initial dock state
	int docks;

	// disable loading the default key bindings
	int nodefaultbinds;

	// NULL-terminated array passed to execvp() to launch terminal
	char **term;
};

extern struct options option;

extern unsigned numlockmask;

// Application matching

struct application {
	char *res_name;
	char *res_class;
	char *WM_NAME;
	int geometry_mask;
	int x, y;
	unsigned width, height;
#ifdef CONFIGREQ
	int ignore_configreq;
#endif
	int is_dock;
	unsigned vdesk;
};

extern struct list *applications;
