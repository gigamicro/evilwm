/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// This file is not generated, it is used to sanity check compile options.

// INFOBANNER_MOVERESIZE depends on INFOBANNER
#if defined(INFOBANNER_MOVERESIZE) && !defined(INFOBANNER)
# define INFOBANNER
#endif

#ifndef GC_INVERT
# undef FONT
#endif

// INFOBANNER depends on FONT
#if defined(INFOBANNER) && !defined(FONT)
# define FONT
#endif

// FONT depends on GC_INVERT
#if defined(FONT) && !defined(GC_INVERT)
# define GC_INVERT
#endif
