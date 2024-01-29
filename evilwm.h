/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Default option values

#define DEF_FONT        "variable"
#define DEF_FG          "goldenrod"
#define DEF_BG          "grey50"
#define DEF_BW          1
#define DEF_FC          "blue"
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
	unsigned vdesksmod;

	// Snap to border flag
	int snap;

	// Whole screen flag (ignore monitor information)
	int wholescreen;

#ifdef SOLIDDRAG
	// Solid drag disabled flag
	int no_solid_drag;
#endif

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
