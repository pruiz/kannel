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

#ifndef _COOKIES_H_
#define _COOKIES_H_

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

Cookie *cookie_create(void);
void cookies_destroy(List *);
int get_cookies(List *, const WSPMachine *);
int set_cookies(List *, WSPMachine *);
WSPMachine *find_session_machine_by_id (int);

#define MAX_HTTP_DATE_LENGTH	128

#endif
