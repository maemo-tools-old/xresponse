/*
 * xresponse - Interaction latency tester,
 *
 * Written by Ross Burton & Matthew Allum
 *              <info@openedhand.com>
 *
 * Copyright (C) 2005,2011 Nokia
 *
 * Licensed under the GPL v2 or greater.
 *
 * Window detection is based on code that is Copyright (C) 2007 Kim Woelders.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdarg.h>

#include <glib.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xutil.h>
#include <X11/Xproto.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/record.h>

#include "report.h"

/**
 * Message record data structure
 */
typedef struct {
	/* message timestamp */
	Time timestamp;
	/* message body */
	char* message;
	/* If true the message will be printed even in silent mode.
	 * Set for response reporting messages. */
	bool print_always;
} record_t;


/**
 * Report data structure.
 */
typedef struct {
	/* message queue */
	GQueue messages;
	/* the output file stream */
	FILE* fp;
	/* flag specifying ownership of the output stream */
	bool fp_owner;
	/* Flag specifying silent mode. In this mode damage common messages
	 * (damage reports and such) are hidden. Used for application response
	 * reporting. */
	bool silent;
} report_t;

/* the report */
static report_t report = {
		.fp = NULL,
		.fp_owner = false,
		.silent = false,
};


/**
 * Releases resources allocated by the report record.
 * @param[in] rec  the record to free.
 * @param[in]      unused.
 */
static void record_free(record_t* rec, void* __attribute__((unused)) data)
{
	free(rec->message);
	g_slice_free(record_t, rec);
}


/**
 * Writes logger record to the defined output.
 *
 * @param[in] rec  the record to write.
 */
static void report_write_record(record_t* rec, void* __attribute__((unused)) data)
{
	static bool displayed_header = false;
	/* the last report timestamp */
	static Time last_timestamp = 0;

	if (!last_timestamp) last_timestamp = rec->timestamp;

	if (!report.silent) {
		if (!displayed_header) { /* Header */
			fprintf(report.fp, "\n"
				" Server Time : Diff    : Info\n"
				"-----------------------------\n");
			displayed_header = true;
		}
		fprintf(report.fp, "%10lums : %5lums : %s", rec->timestamp, rec->timestamp - last_timestamp, rec->message);
	}
	else {
		if (rec->print_always)
			fprintf(report.fp, "%s", rec->message);
	}
	last_timestamp = rec->timestamp;
}

/**
 * Compares two report records by their timestamps.
 *
 * @param[in] rec1      the first record to compare.
 * @param[in] rec2      the second record to compare.
 * @return              <0 the first timestamp is less.
 *                      =0 the timestamps are equal.
 *                      >0 the first timestamp is greater.
 */
static gint compare_records_by_time(const record_t* rec1, const record_t* rec2, void* __attribute__((unused)) data)
{
	return rec1->timestamp - rec2->timestamp;
}


static void add_message(Time timestamp, bool print_always, const char* format, va_list* ap)
{
	/* the last report timestamp */
	static Time last_timestamp = 0;

	if (timestamp == REPORT_LAST_TIMESTAMP) {
		timestamp = last_timestamp;
	}
	else if (last_timestamp == 0) {
		last_timestamp = timestamp;
	}

	record_t* rec = g_slice_new(record_t);
	if (vasprintf(&rec->message, format, *ap) == -1) {
		free(rec);
		rec = NULL;
	}
	if (!rec) {
		fprintf(stderr, "Warning, failed to buffer log record, out of memory\n");
		exit(-1);
	}
	rec->timestamp = timestamp;
	rec->print_always = print_always;
	g_queue_push_tail(&report.messages, rec);
	last_timestamp = timestamp;
}

/*
 * Public API implementation.
 */

void report_init(const char* filename)
{
	g_queue_init(&report.messages);
	if (filename) {
		report.fp = fopen(filename, "w");
		if (!report.fp) {
			fprintf(stderr, "Error while creating report file %s (%s)\n", filename, strerror(errno));
			exit (-1);
		}
		report.fp_owner = true;
	}
	else {
		report.fp = stdout;
	}
}


void report_fini()
{
	g_queue_foreach(&report.messages, (GFunc)record_free, NULL);
	g_queue_clear(&report.messages);
	if (report.fp_owner) {
		fclose(report.fp);
	}
}


void report_add_message(Time timestamp, const char* format, ...)
{
	/* the last report timestamp */
	va_list ap;
	va_start(ap, format);
	add_message(timestamp, false, format, &ap);
	va_end(ap);
}


void report_add_message_forced(const char* format, ...)
{
	/* the last report timestamp */
	va_list ap;
	va_start(ap, format);
	add_message(REPORT_LAST_TIMESTAMP, true, format, &ap);
	va_end(ap);
}


void report_flush_queue()
{
	g_queue_sort(&report.messages, (GCompareDataFunc)compare_records_by_time, NULL);
	g_queue_foreach(&report.messages, (GFunc)report_write_record, NULL);
	g_queue_foreach(&report.messages, (GFunc)record_free, NULL);
	g_queue_clear(&report.messages);
	fflush(report.fp);
}

void report_set_silent(bool value)
{
	report.silent = value;
}

bool report_get_silent()
{
	return report.silent;
}
