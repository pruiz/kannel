/*
 * msg.c - manipulate messages
 *
 * This file contains implementations of the functions that create, destroy,
 * pack, and unpack messages.
 *
 * Lars Wirzenius <liw@wapit.com>
 */

#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <netinet/in.h>

#include "msg.h"
#include "gwlib/gwlib.h"

/**********************************************************************
 * Prototypes for private functions.
 */

static void append_integer(Octstr *os, long i);
static void prepend_integer(Octstr *os, long i);
static void append_string(Octstr *os, Octstr *field);

static int parse_integer(long *i, Octstr *packed, int *off);
static int parse_string(Octstr **os, Octstr *packed, int *off);

static char *type_as_str(Msg *msg);


/**********************************************************************
 * Implementations of the exported functions.
 */

Msg *msg_create(enum msg_type type) {
	Msg *msg;

	msg = gw_malloc(sizeof(Msg));

	msg->type = type;

	#define INTEGER(name) p->name = 0
	#define OCTSTR(name) p->name = NULL
	#define MSG(type, stmt) { struct type *p = &msg->type; stmt }
	#include "msg-decl.h"

	return msg;
}

Msg *msg_duplicate(Msg *msg) {
	Msg *new;

	new = msg_create(msg->type);

	#define INTEGER(name) p->name = q->name
	#define OCTSTR(name) \
		if (q->name == NULL) p->name = NULL; \
		else p->name = octstr_duplicate(q->name);
	#define MSG(type, stmt) { \
		struct type *p = &new->type; \
		struct type *q = &msg->type; \
		stmt }
	#include "msg-decl.h"

	return new;
}

void msg_destroy(Msg *msg) {
	if (msg == NULL)
		return;

	#define INTEGER(name) p->name = 0
	#define OCTSTR(name) octstr_destroy(p->name)
	#define MSG(type, stmt) { struct type *p = &msg->type; stmt }
	#include "msg-decl.h"

	gw_free(msg);
}

void msg_destroy_item(void *msg) {
	msg_destroy(msg);
}

void msg_dump(Msg *msg, int level) {
	debug("gw.msg", 0, "%*sMsg object at %p:", level, "", (void *) msg);
	debug("gw.msg", 0, "%*s type: %s", level, "", type_as_str(msg));
	#define INTEGER(name) \
		debug("gw.msg", 0, "%*s %s.%s: %ld", \
			level, "", t, #name, (long) p->name)
	#define OCTSTR(name) \
		debug("gw.msg", 0, "%*s %s.%s:", level, "", t, #name); \
		octstr_dump(p->name, level + 1)
	#define MSG(tt, stmt) \
		if (tt == msg->type) \
			{ char *t = #tt; struct tt *p = &msg->tt; stmt }
	#include "msg-decl.h"
	debug("gw.msg", 0, "Msg object ends.");
}


enum msg_type msg_type(Msg *msg) {
	return msg->type;
}

Octstr *msg_pack(Msg *msg) {
	Octstr *os;

	os = octstr_create("");
	append_integer(os, msg->type);

	#define INTEGER(name) append_integer(os, p->name)
	#define OCTSTR(name) append_string(os, p->name)
	#define MSG(type, stmt) \
		case type: { struct type *p = &msg->type; stmt } break;
	switch (msg->type) {
		#include "msg-decl.h"
	default:
		panic(0, "Internal error: unknown message type %d", 
		    	 msg->type);
	}

	prepend_integer(os, octstr_len(os));

	return os;
}


Msg *msg_unpack(Octstr *os) {
	Msg *msg;
	int off;
	long i;

	msg = msg_create(0);
	if (msg == NULL)
		goto error;

	off = 0;

	/* Skip length. */
	if (parse_integer(&i, os, &off) == -1)
		goto error;

	if (parse_integer(&i, os, &off) == -1)
		goto error;
	msg->type = i;

	#define INTEGER(name) \
		if (parse_integer(&(p->name), os, &off) == -1) goto error
	#define OCTSTR(name) \
		if (parse_string(&(p->name), os, &off) == -1) goto error
	#define MSG(type, stmt) \
		case type: { struct type *p = &(msg->type); stmt } break;
	switch (msg->type) {
		#include "msg-decl.h"
	default:
		panic(0, "Internal error: unknown message type: %d", 
			msg->type);
	}

	return msg;

error:
	error(errno, "Msg packet was invalid.");
	return NULL;
}


/**********************************************************************
 * Implementations of private functions.
 */


static void append_integer(Octstr *os, long i) {
	Octstr *temp;

	i = htonl(i);
	temp = octstr_create_from_data((char *) &i, sizeof(i));
	octstr_insert(os, temp, octstr_len(os));
	octstr_destroy(temp);
}

static void prepend_integer(Octstr *os, long i) {
	Octstr *temp;

	i = htonl(i);
	temp = octstr_create_from_data((char *) &i, sizeof(i));
	octstr_insert(os, temp, 0);
	octstr_destroy(temp);
}

static void append_string(Octstr *os, Octstr *field) {
	if (field == NULL)
		append_integer(os, -1);
	else {
		append_integer(os, octstr_len(field));
		octstr_insert(os, field, octstr_len(os));
	}
}


static int parse_integer(long *i, Octstr *packed, int *off) {
	gw_assert(*off >= 0);
	if ((int) sizeof(long) + *off > octstr_len(packed)) {
		error(0, "Packet too short while unpacking Msg.");
		return -1;
	}

	octstr_get_many_chars((char *) i, packed, *off, sizeof(long));
	*i = ntohl(*i);
	*off += sizeof(long);
	return 0;
}


static int parse_string(Octstr **os, Octstr *packed, int *off) {
	long len;

	if (parse_integer(&len, packed, off) == -1)
		return -1;

	if (len == -1) {
		*os = NULL;
		return 0;
	}

	/* XXX check that len is ok */

	*os = octstr_copy(packed, *off, len);
	if (*os == NULL)
		return -1;
	*off += len;

	return 0;
}


static char *type_as_str(Msg *msg) {
	switch (msg->type) {
	#define MSG(t, stmt) case t: return #t;
	#include "msg-decl.h"
	default:
		return "unknown type";
	}
}
