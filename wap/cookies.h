/*
 * Module: cookies.h
 *
 * Description: Include module for cookies.c
 *
 * References: RFC 2109
 *
 * Author: Paul Keogh, ANAM Wireless Internet Solutions
 *
 * Date: May 2000
 */

#ifndef COOKIES_H
#define COOKIES_H

/* No support for Secure or Comment fields */

typedef struct _cookie {
	Octstr *name;
	Octstr *value;
	Octstr *version;
	Octstr *domain;
	Octstr *path;
	time_t max_age;
	time_t birth;
} Cookie;

/* Function prototypes for external interface */
                                             
/* 
 * Memory management wrappers for cookies. 
 */
Cookie *cookie_create(void);
void cookies_destroy(List*);

/*
 * Parses the returned HTTP headers and adds the Cookie: headers to
 * the cookie cache of the active WSPMachine.
 * Returns: 0 on success, -1 on failure
 */
int get_cookies(List*, const WSPMachine*);

/*
 * Adds the cookies from the WSPMachine cache to the outgoing HTTP request,
 * rewriting the standard attributes and expiring the cookies if necessary.
 * Returns: 0 on success, -1 on failure
 */
int set_cookies(List*, WSPMachine*);

WSPMachine *find_session_machine_by_id(int);

#define MAX_HTTP_DATE_LENGTH	128

#endif
