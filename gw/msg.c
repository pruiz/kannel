/*
 * msg.c - manipulate messages
 *
 * This file contains implementations of the functions that create, destroy,
 * pack, and unpack messages.
 *
 * Lars Wirzenius <liw@wapit.com>
 */

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <netinet/in.h>

#include "msg.h"
#include "gwlib.h"

/**********************************************************************
 * Prototypes for private functions.
 */

static int append_integer(Octstr *os, int32 i);
static int prepend_integer(Octstr *os, int32 i);
static int append_string(Octstr *os, Octstr *field);

static int parse_integer(int32 *i, Octstr *packed, int *off);
static int parse_string(Octstr **os, Octstr *packed, int *off);

static char *type_as_str(Msg *msg);


/**********************************************************************
 * Implementations of the exported functions.
 */

Msg *msg_create(enum msg_type type) {
	Msg *msg;
	
	msg = malloc(sizeof(Msg));
	if (msg == NULL)
		goto error;
	
	msg->type = type;

	#define INTEGER(name) p->name = 0
	#define OCTSTR(name) p->name = NULL
	#define MSG(type, stmt) { struct type *p = &msg->type; stmt }
	#include "msg-decl.h"

	return msg;

error:
	error(errno, "Out of memory.");
	return NULL;
}

Msg *msg_duplicate(Msg *msg) {
	Msg *new;

	new = msg_create(msg->type);
	if (new == NULL)
		return NULL;

	#define INTEGER(name) p->name = q->name
	#define OCTSTR(name) \
		if (q->name == NULL) p->name = NULL; \
		else { \
		    p->name = octstr_copy(q->name, 0, octstr_len(q->name)); \
		    if (p->name == NULL) goto error; \
		}
	#define MSG(type, stmt) { \
		struct type *p = &new->type; \
		struct type *q = &msg->type; \
		stmt }
	#include "msg-decl.h"

	return new;

error:
	error(errno, "Out of memory.");
	return NULL;
}

void msg_destroy(Msg *msg) {
	if (msg == NULL)
		return;

	#define INTEGER(name) p->name = 0
	#define OCTSTR(name) octstr_destroy(p->name)
	#define MSG(type, stmt) { struct type *p = &msg->type; stmt }
	#include "msg-decl.h"

	free(msg);
}

void msg_dump(Msg *msg) {
	debug(0, "Msg object at %p:", (void *) msg);
	debug(0, "  type: %s", type_as_str(msg));
	#define INTEGER(name) \
		debug(0, "  %s.%s: %ld", t, #name, (long) p->name)
	#define OCTSTR(name) \
		debug(0, "  %s.%s:", t, #name); octstr_dump(p->name)
	#define MSG(tt, stmt) \
		if (tt == msg->type) \
			{ char *t = #tt; struct tt *p = &msg->tt; stmt }
	#include "msg-decl.h"
	debug(0, "Msg object ends.");
}


enum msg_type msg_type(Msg *msg) {
	return msg->type;
}

Octstr *msg_pack(Msg *msg) {
	Octstr *os;
	
	os = octstr_create_empty();
	if (os == NULL)
		goto error;

	if (append_integer(os, msg->type) == -1)
		goto error;

	#define INTEGER(name) \
		if (append_integer(os, p->name) == -1) goto error
	#define OCTSTR(name) \
		if (append_string(os, p->name) == -1) goto error
	#define MSG(type, stmt) \
		case type: { struct type *p = &msg->type; stmt } break;
	switch (msg->type) {
		#include "msg-decl.h"
	}
	
	if (prepend_integer(os, octstr_len(os)) == -1)
		goto error;

	return os;

error:
	error(errno, "Out of memory.");
	return NULL;
}


Msg *msg_unpack(Octstr *os) {
	Msg *msg;
	int off;
	int32 i;
	
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
	}
	
	return msg;

error:
	error(errno, "Out of memory.");
	return NULL;
}


/**********************************************************************
 * Implementations of private functions.
 */


static int append_integer(Octstr *os, int32 i) {
	Octstr *temp;
	
	i = htonl(i);
	temp = octstr_create_from_data((char *) &i, sizeof(i));
	if (temp == NULL)
		goto error;
	if (octstr_insert(os, temp, octstr_len(os)) == -1)
		goto error;
	
	octstr_destroy(temp);
	return 0;

error:
	octstr_destroy(temp);
	return -1;
}

static int prepend_integer(Octstr *os, int32 i) {
	Octstr *temp;
	
	i = htonl(i);
	temp = octstr_create_from_data((char *) &i, sizeof(i));
	if (temp == NULL)
		goto error;
	if (octstr_insert(os, temp, 0) == -1)
		goto error;
	
	octstr_destroy(temp);
	return 0;

error:
	octstr_destroy(temp);
	return -1;
}

static int append_string(Octstr *os, Octstr *field) {
	if (field == NULL) {
		if (append_integer(os, -1) == -1)
			return -1;
		return 0;
	}
	if (append_integer(os, octstr_len(field)) == -1)
		return -1;
	if (octstr_insert(os, field, octstr_len(os)) == -1)
		return -1;
	return 0;
}


static int parse_integer(int32 *i, Octstr *packed, int *off) {
	assert(*off >= 0);
	if (sizeof(int32) + *off > octstr_len(packed)) {
		error(0, "Packet too short while unpacking Msg.");
		return -1;
	}

	octstr_get_many_chars((char *) i, packed, *off, sizeof(int32));
	*i = ntohl(*i);
	*off += sizeof(int32);
	return 0;
}


static int parse_string(Octstr **os, Octstr *packed, int *off) {
	int32 len;

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
