/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// main() function parses options and kicks off the main event loop.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include <X11/X.h>
#include <X11/Xlib.h>

#include "bind.h"
#include "client.h"
#include "display.h"
#include "events.h"
#include "evilwm.h"
#include "list.h"
#include "log.h"
#include "xalloc.h"
#include "xconfig.h"

#define CONFIG_FILE ".evilwmrc"

#define xstr(s) str(s)
#define str(s) #s

// WM will manage/process events/unmanage until this flag is set
static _Bool wm_exit;

struct options option;

static struct list *opt_bind = NULL;

unsigned numlockmask = 0;

struct list *applications = NULL;

static void set_bind(const char *arg);
static void set_mask(const char *arg);
static void set_mask1(const char *arg);
static void set_mask2(const char *arg);
static void set_altmask(const char *arg);
static void set_app(const char *arg);
static void set_app_geometry(const char *arg);
#ifdef CONFIGREQ
static void set_app_manual(void);
#endif
static void set_app_dock(void);
static void set_app_vdesk(const char *arg);
static void set_app_fixed(void);
static void unset_solid_drag(void);

static struct xconfig_option evilwm_options[] = {
	{ XCONFIG_STRING,   "display",      { .s = &option.display } },
	{ XCONFIG_STR_LIST, "term",         { .sl = &option.term } },
#ifdef FONT
	{ XCONFIG_STRING,   "fn",           { .s = &option.font } },
#endif
	{ XCONFIG_STRING,   "fg",           { .s = &option.fg } },
	{ XCONFIG_STRING,   "fc",           { .s = &option.fc } },
	{ XCONFIG_STRING,   "bg",           { .s = &option.bg } },
	{ XCONFIG_INT,      "bw",           { .i = &option.bw } },
	{ XCONFIG_INT,      "snap",         { .i = &option.snap } },
	{ XCONFIG_INT,      "kbpx",         { .i = &option.kbpx } },
	{ XCONFIG_DOUBLE,   "quickmove",    { .d = &option.quickmove } },
	{ XCONFIG_BOOL,     "wholescreen",  { .i = &option.wholescreen } },
	{ XCONFIG_UINT,     "numvdesks",    { .u = &option.vdesks } },
	{ XCONFIG_UINT,     "modvdesks",    { .u = &option.modvdesks } },
	{ XCONFIG_INT,      "docks",        { .i = &option.docks } },
	{ XCONFIG_INT,     "soliddrag",     { .i = &option.solid_drag } },
	{ XCONFIG_INT,     "solidsweep",    { .i = &option.solid_sweep } },
	{ XCONFIG_CALL_0,   "nosoliddrag",  { .c0 = &unset_solid_drag } },
	{ XCONFIG_CALL_1,   "bind",         { .c1 = &set_bind } },
	{ XCONFIG_BOOL,    "nodefaultbinds",{ .i = &option.nodefaultbinds } },
	{ XCONFIG_CALL_1,   "mask",         { .c1 = &set_mask } },
	{ XCONFIG_CALL_1,   "mask1",        { .c1 = &set_mask1 } },
	{ XCONFIG_CALL_1,   "mask2",        { .c1 = &set_mask2 } },
	{ XCONFIG_CALL_1,   "altmask",      { .c1 = &set_altmask } },

	{ XCONFIG_CALL_1,   "app",          { .c1 = &set_app } },
	{ XCONFIG_CALL_1,   "geometry",     { .c1 = &set_app_geometry } },
	{ XCONFIG_CALL_1,   "g",            { .c1 = &set_app_geometry } },
#ifdef CONFIGREQ
	{ XCONFIG_CALL_0,   "manual",       { .c0 = &set_app_manual } },
	{ XCONFIG_CALL_0,   "m",            { .c0 = &set_app_manual } },
#endif
	{ XCONFIG_CALL_0,   "dock",         { .c0 = &set_app_dock } },
	{ XCONFIG_CALL_0,   "d",            { .c0 = &set_app_dock } },
	{ XCONFIG_CALL_1,   "vdesk",        { .c1 = &set_app_vdesk } },
	{ XCONFIG_CALL_1,   "v",            { .c1 = &set_app_vdesk } },
	{ XCONFIG_CALL_0,   "fixed",        { .c0 = &set_app_fixed } },
	{ XCONFIG_CALL_0,   "f",            { .c0 = &set_app_fixed } },
	{ XCONFIG_CALL_0,   "s",            { .c0 = &set_app_fixed } }, // XXX? "sticky"
	{ XCONFIG_END, NULL, { NULL } }
};

static void handle_signal(int signo);
static void handle_sigsegv(int signo);

static void helptext(void) { puts(
"Usage: evilwm [OPTION]...\n"
"evilwm is a minimalist window manager for X11.\n"
"\n"
" Exiting options:\n"
"  -h, --help      display this help and exit\n"
"  -hh,--writedefaults output default options (inc. bindings) and exit\n"
"  -V, --version   output version information and exit\n"
"\n"
" Options:\n"
"  --display DISPLAY   X display [from environment]\n"
"  --term PROGRAM      command executed for --bind spawn [" DEF_TERM "]\n"
#ifdef FONT
"  --fn FONTNAME       X LFD for font used to display text [" DEF_FONT "]\n"
#endif
"  --fg COLOUR         colour (name or #XXXXXX) of active window frames [" DEF_FG "]\n"
"  --fc COLOUR         colour of active fixed window frames [" DEF_FC "]\n"
"  --bg COLOUR         colour of inactive window frames [" DEF_BG "]\n"
"  --bw PIXELS         window border width [" xstr(DEF_BW) "]\n"
"  --snap PIXELS       snap distance when dragging windows [" xstr(DEF_SNAP) "]\n"
"  --kbpx PIXELS       keyboard moveresize distance [" xstr(DEF_KBPX) "]\n"
"  --quickmove N.N     multiply kbpx by this much when repeated within 250ms [" xstr(DEF_QUICKMOVE) "]\n"
"  --wholescreen       ignore monitor geometries when maximising\n"
"  --numvdesks N       total number of virtual desktops [" xstr(DEF_VDESKS) "]\n"
"  --modvdesks N       virtual desktop subdivision size; 0 means value of numvdesks [" xstr(DEF_VDESKSMOD) "]\n"
"  --soliddrag N       nonzero to move the window directly rather than showing a placeholder [" xstr(DEF_SOLIDDRAG) "]\n"
"  --solidsweep N      same but for resizing [" xstr(DEF_SOLIDSWEEP) "]\n"
"  --nosoliddrag       alias for 'soliddrag 0'\n"
"\n"
" Application matching options:\n"
"  --app NAME/CLASS/WMNAME match application by WM_CLASS name & class + window title\n"
"    -g, --geometry GEOM   apply X geometry (eg 100x160+50+80) to matched application\n"
#ifdef CONFIGREQ
"    -m, --manual          disallow app from modifying its own geometry\n"
#endif
"    -d, --dock            treat matched app as a dock\n"
"    -v, --vdesk VDESK     move app to numbered vdesk (indexed from 0)\n"
"    -f, --fixed           matched app should start fixed\n"
"\n"
" Bind options:\n"
"  --bind CTL[=FUNC]   bind (or unbind) input to window manager function\n"
"  --nodefaultbinds    don't use default bindings\n"
"  --mask1 MASK        modifiers for most keyboard controls [" DEF_MASK1 "]\n"
"  --mask2 MASK        modifiers for mouse button controls [" DEF_MASK2 "]\n"
"  --altmask MASK      modifiers selecting alternate control behaviour [" DEF_ALTMASK "]\n"
"When binding a control, CTL contains a (case-sensitive) list of modifiers,\n"
"buttons or keys (using the X11 keysym name) and FUNC lists a function\n"
"name and optional extra flags.  List entries can be separated with ','\n"
"or '+'.  If FUNC is missing or empty, the control is unbound.  Modifiers are\n"
"ignored when binding buttons.\n"
"\n"
"Modifiers: mask1, mask2, altmask, shift, control, mod1 (alt), mod2..mod5\n"
"Buttons: button1..button5\n"
"Functions: delete, dock, fix, info, kill, lower, move, next, resize,\n"
"           spawn, vdesk\n"
"Flags: up (u,on), down (d,off), left (l), right (r), top, bottom,\n"
"       relative (rel), toggle, vertical (v), horizontal (h)\n"
);}

static const char *default_options[] = {
	"display",
	"term " DEF_TERM,
	"fn " DEF_FONT,
	"fg " DEF_FG,
	"fc " DEF_FC,
	"bg " DEF_BG,
	"bw " xstr(DEF_BW),
	"snap " xstr(DEF_SNAP),
	"kbpx " xstr(DEF_KBPX),
	"quickmove " xstr(DEF_QUICKMOVE),
	"docks 1",
	"#wholescreen",
	"numvdesks " xstr(DEF_VDESKS),
	"modvdesks " xstr(DEF_VDESKSMOD),
	"soliddrag " xstr(DEF_SOLIDDRAG),
	"solidsweep " xstr(DEF_SOLIDSWEEP),
	"mask1 " DEF_MASK1,
	"mask2 " DEF_MASK2,
	"altmask " DEF_ALTMASK,
	"#nodefaultbinds",
};
#define NUM_DEFAULT_OPTIONS (sizeof(default_options)/sizeof(default_options[0]))

static void putdefaultopts(void) {
	for(unsigned i = 0; i < NUM_DEFAULT_OPTIONS; i++)
		puts(default_options[i]);
}

int main(int argc, char *argv[]) {
	int argn = 1, ret;
	Window old_current_window = None;

	struct sigaction act = {
		.sa_handler = handle_signal,
		.sa_flags = 0,
	};
	sigemptyset(&act.sa_mask);
	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGHUP, &act, NULL);
	struct sigaction segact = {
		.sa_handler = handle_sigsegv,
		.sa_flags = act.sa_flags,
		.sa_mask = act.sa_mask,
	};
	sigaction(SIGSEGV, &segact, NULL);

	// Run until something signals to quit.
	wm_exit = 0;
	while (!wm_exit) {

		// Default options
		option = (struct options){0};
		for (unsigned i = 0; i < NUM_DEFAULT_OPTIONS; i++)
			xconfig_parse_line(evilwm_options, default_options[i]);

		// Read configuration file
		const char *home = getenv("HOME");
		if (home) {
			char *conffile = xmalloc(strlen(home) + sizeof(CONFIG_FILE) + 2);
			strcpy(conffile, home);
			strcat(conffile, "/" CONFIG_FILE);
			xconfig_parse_file(evilwm_options, conffile);
			free(conffile);
		}

		// Parse CLI options
		ret = xconfig_parse_cli(evilwm_options, argc, argv, &argn);
		if (ret == XCONFIG_MISSING_ARG) {
			fprintf(stderr, "%s: missing argument to `%s'\n", argv[0], argv[argn]);
			fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
			exit(1);
		} else if (ret == XCONFIG_BAD_OPTION) {
			if(0){
			} else if (0 == strcmp(argv[argn], "-h")
			        || 0 == strcmp(argv[argn], "--help")) {
				helptext();
				exit(0);
			} else if (0 == strcmp(argv[argn], "-hh")
			        || 0 == strcmp(argv[argn], "--writedefaults")) {
				puts("### ~/.evilwmrc: EvilWM options file; reloads on SIGHUP ###");
				putdefaultopts();
				putdefaultbinds();
				puts("#app Browser/firefox/Sharing Indicator\n #geometry 100x160+50+80\n #manual\n #dock\n #vdesk 1\n #fixed");
				exit(0);
			} else if (0 == strcmp(argv[argn], "-V")
			        || 0 == strcmp(argv[argn], "--version")) {
				LOG_INFO("evilwm version " VERSION "\n");
				exit(0);
			} else {
				fprintf(stderr, "%s: unrecognised option '%s'\n", argv[0], argv[argn]);
				fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
				exit(1);
			}
		}

		applications = list_reverse(applications, NULL);

		// Do binds after main arg parsing so that masks are already set
		// Also re-reverses the list so later binds override earlier ones?
		bind_unset();
		if (!option.nodefaultbinds) bind_defaults();
		while (opt_bind) {
			char *arg = opt_bind->data;
			opt_bind = list_delete(opt_bind, arg);
			char *ctlstr = strtok(arg, "=");
			if (!ctlstr) {
				free(arg);
				continue;
			}
			char *funcstr = strtok(NULL, "");
			bind_control(ctlstr, funcstr);
			free(arg);
		}

		// Open display only if not already open
		if (!display.dpy) {
			display_open();
		}

		// Manage all eligible clients across all screens
		display_manage_clients();

		// Restore "old current window", if known
		if (old_current_window != None) {
			struct client *c = find_client(old_current_window);
			if (c)
				client_select(c);
		}

		////////////////////////////////////////
		// Event loop will run until interrupted
		end_event_loop = 0;
		event_main_loop();
		LOG_DEBUG("main event loop ended\n");
		////////////////////////////////////////

		// Record "old current window" across SIGHUPs
		old_current_window = current ? current->window : None;

		// Important to clean up anything now that might be reallocated
		// after rereading config, re-managing windows, etc.

		// Free any allocated strings in parsed options
		xconfig_free(evilwm_options);

		// Free application configuration
		while (applications) {
			struct application *app = applications->data;
			applications = list_delete(applications, app);
			if (app->res_name)
				free(app->res_name); // all string parameters are one malloc
			free(app);
		}

		display_unmanage_clients();
		XSync(display.dpy, True);
	}

	// Close display
	display_close();

	return 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Option parsing callbacks

static void set_bind(const char *arg) {
	char *argdup = xstrdup(arg);
	if (!argdup)
		return;
	opt_bind = list_prepend(opt_bind, argdup);
}

static void set_mask(const char *arg) {
	char *argdup = xstrdup(arg);
	if (!argdup) return;
	bind_modifier(strtok(argdup, "="), strtok(NULL, ""));
	free(argdup);
}
static void set_mask1  (const char *arg) {bind_modifier("mask1",  arg);}
static void set_mask2  (const char *arg) {bind_modifier("mask2",  arg);}
static void set_altmask(const char *arg) {bind_modifier("altmask",arg);}

static void set_app(const char *arg) {
	struct application *new = xmalloc(sizeof(struct application));
	*new=(struct application){0,.vdesk=VDESK_NONE};
	applications = list_prepend(applications, new);
	if (!*arg) return;
	new->res_name = xstrdup(arg);
	// a/b -> a\0b
	new->res_class = strchr(new->res_name, '/');
	if (!new->res_class) return;
	*new->res_class++ = '\0';

	new->WM_NAME = strchr(new->res_class, '/');
	if (!new->WM_NAME) return;
	*new->WM_NAME++ = '\0';
}

static void set_app_geometry(const char *arg) {
	if (applications) {
		struct application *app = applications->data;
		app->geometry_mask = XParseGeometry(arg,
				&app->x, &app->y, &app->width, &app->height);
	}
}

#ifdef CONFIGREQ
static void set_app_manual(void) {
	if (applications) {
		struct application *app = applications->data;
		app->ignore_configreq = 1;
	}
}
#endif

static void set_app_dock(void) {
	if (applications) {
		struct application *app = applications->data;
		app->is_dock = 1;
	}
}

static void set_app_vdesk(const char *arg) {
	unsigned v = strtoul(arg, NULL, 0);
	if (applications && valid_vdesk(v)) {
		struct application *app = applications->data;
		app->vdesk = v;
	}
}

static void set_app_fixed(void) {
	if (applications) {
		struct application *app = applications->data;
		app->vdesk = VDESK_FIXED;
	}
}

static void unset_solid_drag(void) {
	option.solid_drag = 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Signals configured in main() trigger a clean shutdown

static void handle_signal(int signo) {
	if (signo != SIGHUP) {
		wm_exit = 1;
	}
	end_event_loop = 1;
}

static void handle_sigsegv(int signo) {
	(void)signo;
	LOG_ERROR("SEGFAULT!\n");
	XUngrabPointer(display.dpy, CurrentTime);
	XUngrabServer(display.dpy);
	if (wm_exit != 1) {
		wm_exit = 1;
		end_event_loop = 1;
		display_unmanage_clients();
		XSync(display.dpy, True);
		display_close();
	}
	else LOG_ERROR("ABORTING SEGFAULT HANDLING!\n");
	exit(139);
}
