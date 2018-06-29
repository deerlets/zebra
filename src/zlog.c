#include "zlog.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#ifdef _WIN32
#define CL_RESET ""
#define CL_NORMAL CL_RESET
#define CL_NONE  CL_RESET
#define CL_WHITE ""
#define CL_GRAY  ""
#define CL_RED  ""
#define CL_GREEN ""
#define CL_YELLOW ""
#define CL_BLUE  ""
#define CL_MAGENTA ""
#define CL_CYAN  ""
#else
#define CL_RESET "\033[0;0m"
#define CL_NORMAL CL_RESET
#define CL_NONE  CL_RESET
#define CL_WHITE "\033[1;29m"
#define CL_GRAY  "\033[1;30m"
#define CL_RED  "\033[1;31m"
#define CL_GREEN "\033[1;32m"
#define CL_YELLOW "\033[1;33m"
#define CL_BLUE  "\033[1;34m"
#define CL_MAGENTA "\033[1;35m"
#define CL_CYAN  "\033[1;36m"
#endif

static enum zlog_level limit = ZLOG_INFO;
static zlog_before_log_func_t before_log_cb;
static zlog_after_log_func_t after_log_cb;

int zlog_set_level(int level)
{
	int previous = limit;
	limit = level;
	return previous;
}

void zlog_set_before_log_cb(zlog_before_log_func_t cb)
{
	before_log_cb = cb;
}

void zlog_set_after_log_cb(zlog_after_log_func_t cb)
{
	after_log_cb = cb;
}

static int
__zlog_message(enum zlog_level level, const char *string, va_list ap)
{
	if (level < limit) return 0;

	if (before_log_cb)
		before_log_cb(string, ap);

	if (!string || *string == '\0') {
		printf("__zlog_message: Empty string.\n");
		return 1;
	}

	char prefix[40];
	switch (level) {
	case ZLOG_DEBUG: // Bright Cyan, important stuff!
		strcpy(prefix, CL_CYAN"[Debug]"CL_RESET": ");
		break;
	case ZLOG_INFO: // Bright White (Variable information)
		strcpy(prefix, CL_WHITE"[Info]"CL_RESET": ");
		break;
	case ZLOG_NOTICE: // Bright White (Less than a warning)
		strcpy(prefix, CL_WHITE"[Notice]"CL_RESET": ");
		break;
	case ZLOG_WARN: // Bright Yellow
		strcpy(prefix, CL_YELLOW"[Warning]"CL_RESET": ");
		break;
	case ZLOG_ERROR: // Bright Red (Regular errors)
		strcpy(prefix, CL_RED"[Error]"CL_RESET": ");
		break;
	case ZLOG_FATAL: // Bright Red (Fatal errors, abort(); if possible)
		strcpy(prefix, CL_RED"[Fatal Error]"CL_RESET": ");
		break;
	case ZLOG_NONE: // None
		strcpy(prefix, "");
		break;
	default:
		printf("__zlog_message: Invalid level passed.\n");
		return 1;
	}

	printf("%s", prefix);
	vprintf(string, ap);
	fflush(stdout);

	if (after_log_cb)
		after_log_cb(string, ap);
	return 0;
}

int zlog_debug(const char *string, ...)
{
	int rc;
	va_list ap;

	va_start(ap, string);
	rc = __zlog_message(ZLOG_DEBUG, string, ap);
	va_end(ap);
	return rc;
}

int zlog_info(const char *string, ...)
{
	int rc;
	va_list ap;

	va_start(ap, string);
	rc = __zlog_message(ZLOG_INFO, string, ap);
	va_end(ap);
	return rc;
}

int zlog_notice(const char *string, ...)
{
	int rc;
	va_list ap;

	va_start(ap, string);
	rc = __zlog_message(ZLOG_NOTICE, string, ap);
	va_end(ap);
	return rc;
}

int zlog_warn(const char *string, ...)
{
	int rc;
	va_list ap;

	va_start(ap, string);
	rc = __zlog_message(ZLOG_WARN, string, ap);
	va_end(ap);
	return rc;
}

int zlog_error(const char *string, ...)
{
	int rc;
	va_list ap;

	va_start(ap, string);
	rc = __zlog_message(ZLOG_ERROR, string, ap);
	va_end(ap);
	return rc;
}

int zlog_fatal(const char *string, ...)
{
	int rc;
	va_list ap;

	va_start(ap, string);
	rc = __zlog_message(ZLOG_FATAL, string, ap);
	va_end(ap);
	return rc;
}

int zlog_none(const char *string, ...)
{
	int rc;
	va_list ap;

	va_start(ap, string);
	rc = __zlog_message(ZLOG_NONE, string, ap);
	va_end(ap);
	return rc;
}