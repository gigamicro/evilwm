/* evilwm - Minimalist Window Manager for X
 * Copyright (C) 1999-2021 Ciaran Anscomb
 * see README for license and other details. */

// Configuration parsing.
//
// Scans options either from an array (e.g., from command line) or file.

#ifndef EVILWM_XCONFIG_H__
#define EVILWM_XCONFIG_H__

enum xconfig_result {
	XCONFIG_OK = 0,
	XCONFIG_BAD_OPTION,
	XCONFIG_MISSING_ARG,
	XCONFIG_FILE_ERROR
};

// Option types.

enum xconfig_option_type {
	XCONFIG_BOOL,  // int
	XCONFIG_INT,  // int
	XCONFIG_STRING,  // char *
	XCONFIG_STR_LIST,  // char **
	XCONFIG_CALL_0,  // (void (*)(void)
	XCONFIG_CALL_1,  // (void (*)(const char *)
	XCONFIG_END
};

// An array of struct xconfig_option passed to the parsing functions specifies
// recognised options, their type, and where to store any result.  Mark the end
// of the list with entry of type XCONFIG_END.

struct xconfig_option {
	enum xconfig_option_type type;
	const char *name;
	void *dest;
};

enum xconfig_result xconfig_parse_file(struct xconfig_option *options,
				       const char *filename);

enum xconfig_result xconfig_parse_cli(struct xconfig_option *options,
				      int argc, char **argv, int *argn);

#endif
