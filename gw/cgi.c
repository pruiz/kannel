/*
 * cgi.c - implementations for CGI-BIN utility functions
 *
 * Lars Wirzenius <liw@wapit.com>
 */

#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "gwlib/gwlib.h"
#include "cgi.h"


/***********************************************************************
 * cgi.h declares struct CGIArg, and here we define it. Users of this
 * modules shouldn't be able to use the data structure directly, only
 * through the functions we define.
 */

struct CGIArg{
	char *name;
	char *value;
	struct CGIArg *next;
};

/***********************************************************************
 * Declarations of local functions.
 */

static CGIArg *new_cgiarg(char *name, char *value);


/***********************************************************************
 * The functions declared in cgi.h.
 */

CGIArg *cgiarg_decode_to_list(char *args) {
	CGIArg *list, *tail, *new;
	char *name, *value;

	if (args == NULL)
	    return NULL;
	
	list = NULL;
	tail = NULL;
	while (*args != '\0') {
		name = args;
		value = name + strcspn(name, "=");
		if (*value == '=') {
			*value++ = '\0';
			args = value + strcspn(value, "&");
			if (*args == '&')
				*args++ = '\0';
		} else
			args = value;

		new = new_cgiarg(name, value);
		if (new == NULL)
			goto error;
		if (list == NULL)
			list = tail = new;
		else {
			tail->next = new;
			tail = new;
		}
	}
	return list;

error:
	cgiarg_destroy_list(list);
	return NULL;
}


void cgiarg_destroy_list(CGIArg *list) {
	while (list != NULL) {
		CGIArg *p;
		p = list;
		list = list->next;
		gw_free(p->name);
		gw_free(p->value);
		gw_free(p);
	}
}


int cgiarg_get(CGIArg *list, char *name, char **value) {
	while (list != NULL && strcmp(list->name, name) != 0)
		list = list->next;
	if (list == NULL)
		return -1;
	*value = list->value;
	return 0;
}


/***********************************************************************
 * Implementations of local functions.
 */

static CGIArg *new_cgiarg(char *name, char *value) {
	CGIArg *new;
	
	new = gw_malloc(sizeof(CGIArg));
	new->name = gw_strdup(name);
	new->value = gw_strdup(value);

	url_decode(new->value);	      
	new->next = NULL;
	return new;
}
