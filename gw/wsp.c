/*
 * wsp.c - Parts of WSP shared between session oriented and connectionless mode
 */


#include <string.h>

#include "gwlib/gwlib.h"
#include "wsp.h"
#include "wsp_pdu.h"
#include "wsp_headers.h"

static int wsp_encode_content_type(Octstr *type);

/***********************************************************************
 * Public functions
 */


Octstr *wsp_encode_http_headers(Octstr *content_type) {
	long type;
	Octstr *os;
	
	type = wsp_encode_content_type(content_type);

	/* `type' must be a short integer a la WSP */
	gw_assert(type >= 0x00);
	gw_assert(type < 0x80);

	os = octstr_create("");
	octstr_append_char(os, ((unsigned char) type) | 0x80);
	
	return os;
}


long wsp_convert_http_status_to_wsp_status(long http_status) {
	static struct {
		long http_status;
		long wsp_status;
	} tab[] = {
		{ 200, 0x20 },
		{ 413, 0x4D },
		{ 415, 0x4F },
		{ 500, 0x60 },
	};
	int num_items = sizeof(tab) / sizeof(tab[0]);
	int i;
	
	for (i = 0; i < num_items; ++i)
		if (tab[i].http_status == http_status)
			return tab[i].wsp_status;
	error(0, "WSP: Unknown status code used internally. Oops.");
	return 0x60; /* Status 500, or "Internal Server Error" */
}

static int wsp_encode_content_type(Octstr *type) {
	static struct {
		char *type;
		int shortint;
	} tab[] = {
		{ "text/plain", 0x03 },
		{ "text/vnd.wap.wml", 0x08 },
		{ "text/vnd.wap.wmlscript", 0x09 },
		{ "application/vnd.wap.wmlc", 0x14 },
		{ "application/vnd.wap.wmlscriptc", 0x15 },
		{ "image/vnd.wap.wbmp", 0x21 },
	};
	int num_items = sizeof(tab) / sizeof(tab[0]);
	int i;

	for (i = 0; i < num_items; i++)
		if (octstr_str_compare(type, tab[i].type) == 0)
			return tab[i].shortint;
	/* XXX This is bogus, we should know all the content type assignments,
	 * and unknown content types should be encoded as strings. */
	error(0, "WSP: Unknown content type <%s>, assuming text/plain.",
		octstr_get_cstr(type));
	return 0x03;
}
