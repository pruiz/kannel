/*
 * Module: cookies.c
 *
 * Description: Implements a minimal cookie handler for session persistence.
 *
 * References: RFC 2109
 *
 * Author: Paul Keogh, ANAM Wireless Internet Solutions
 *
 * Date: May 2000
 */

#include <string.h>
#include <ctype.h>

#include "gwlib/gwlib.h"
#include "wsp.h"
#include "cookies.h"

/* Statics */

static Octstr *get_header_value (Octstr *);
static Cookie *parse_cookie (Octstr *);
static void add_cookie_to_cache (const WSPMachine *, Cookie *);
static void expire_cookies (List *);
static void cookie_destroy (Cookie *);
static int have_cookie (List *, Cookie *);
static int parse_http_date (const char *);
static Cookie emptyCookie;

/*
 * Function: _create, _destroy
 *
 * Description: Memory management wrappers for cookies.
 */

Cookie *cookie_create (void)
{
	Cookie *p;

	p = gw_malloc (sizeof (Cookie));	/* Never returns NULL */

	*p = emptyCookie;
	p -> max_age = -1;
	time (&p -> birth);
	return p;
}

void cookies_destroy (List *cookies) {
	Cookie *c;

	gwlib_assert_init();

	if (cookies == NULL)
		return;

	while ((c = list_extract_first(cookies)) != NULL)
		cookie_destroy(c);

	list_destroy(cookies);
}

/*
 * Function: get_cookies
 *
 * Description: Parses the returned HTTP headers and adds the Cookie: headers to
 *		the cookie cache of the active WSPMachine.
 *
 * Returns: 0 on success, -1 on failure
 */

int get_cookies (List *headers, const WSPMachine *sm)
{
	Octstr *header = NULL;
	Octstr *value = NULL;
	Cookie *cookie = NULL;
	long pos = 0;

	/* This can happen if the user aborts while the HTTP request is pending from the server.
	 * In that case, the session machine is destroyed and is not available to this function
	 * for cookie caching.
	 */

	if (sm == NULL) {
		info (0, "No session machine for cookie retrieval");
		return 0;
	}

	for (pos = 0; pos < list_len (headers); pos++) {
		header = list_get(headers, pos);
		debug ("wap.wsp.http", 0, "get_cookies: Examining header (%s)", octstr_get_cstr (header));
		if (strcasecmp ("set-cookie", octstr_get_cstr (header)) == 0) {		
			debug ("wap.wsp.http", 0, "Caching cookie (%s)", octstr_get_cstr (header));

			if ((value = get_header_value (header)) == NULL) {
				error (0, "get_cookies: No value in (%s)",
					octstr_get_cstr(header));
				continue;
			}

			/* Parse the received cookie */

			if ((cookie = parse_cookie (value)) != NULL) {

				/* Check to see if this cookie is already present */

				if (have_cookie (sm->cookies, cookie) == 1) {
					debug ("wap.wsp.http", 0, "parse_cookie: Cookie present");
					cookie_destroy (cookie);
					continue;
				}
				else {
					add_cookie_to_cache (sm, cookie);
					debug ("wap.wsp.http", 0, "get_cookies: Added (%s)", 
						octstr_get_cstr (cookie -> name));
				}
			}
		}
	}

	debug ("wap.wsp.http", 0, "get_cookies: End");
	return 0;
}

/*
 * Function: set_cookies
 *
 * Description: Adds the cookies from the WSPMachine cache to the outgoing HTTP request,
 *		rewriting the standard attributes and expiring the cookies if necessary.
 *
 * Returns: 0 on success, -1 on failure
 */

int set_cookies (List *headers, WSPMachine *sm)
{
	Cookie *value = NULL;
	Octstr *cookie = NULL;
	long pos = 0;

	if (sm->cookies == NULL)
		sm->cookies = list_create ();

	if (headers == NULL || sm == NULL) {
		error (0, "set_cookies: Null argument(s) - no headers, WSPMachine or both");
		return -1;
	}

	/* Expire cookies that have timed out */

	expire_cookies (sm->cookies);

	/* Walk through the cookie cache, adding the cookie to the request headers */

	if (list_len (sm->cookies) > 0) {
		debug ("wap.wsp.http", 0, "set_cookies: Cookies in cache");

		for (pos = 0; pos < list_len (sm->cookies); pos++) {
			value = list_get (sm->cookies, pos);

			cookie = octstr_create ("Cookie: ");
			octstr_append (cookie, value->version);
			octstr_append (cookie, value->name);
			octstr_append_char (cookie, '=');
			octstr_append (cookie, value->value);

			if (value->path) {
				octstr_append_char (cookie, ';');
				octstr_append (cookie, value->path);
			}
			if (value->domain) {
				octstr_append_char (cookie, ';');
				octstr_append (cookie, value->domain);
			}

			list_append (headers, cookie);
			debug ("wap.wsp.http", 0, "set_cookies: Added (%s)", octstr_get_cstr (cookie));
		}
	}
	else
		debug ("wap.wsp.http", 0, "set_cookies: No cookies in cache");

	return 0;
}

/*
 * Private interface functions
 */

/*
 * Function: get_header_value
 *
 * Description: Gets the header value as an Octstr.
 */

static Octstr *get_header_value (Octstr *header) 
{
	Octstr *h = NULL;
	long colon = -1;
	
	if (header == NULL) {
		error (0, "get_header_value: NULL argument");
		return NULL;
	}

	octstr_strip_blank(header);
	colon = octstr_search_char(header, ':');
	if (colon == -1) {
		error (0, "get_header_value: Malformed header (%s)", octstr_get_cstr (header));
		return NULL;
	} else {
		h = octstr_copy (header, colon + 1, octstr_len(header));
		octstr_strip_blank(h);
	}

	debug ("wap.wsp.http", 0, "get_header_value: Value (%s)", octstr_get_cstr (h));
	return h;
}

/*
 * Function: parse_cookie
 *
 * Description: Parses the received cookie and rewrites it for sending.
 */

static Cookie *parse_cookie (Octstr *cookiestr)
{
	char *v = NULL;
	char *p = NULL;
	int delta = 0;
	Cookie *c = NULL;
	Octstr **f = NULL;

	if (cookiestr == NULL) {
		error (0, "parse_cookie: NULL argument");
		return NULL;
	}

	v = gw_strdup (octstr_get_cstr (cookiestr));
	p = strtok (v, ";");
	
	c = cookie_create ();	/* Never returns NULL */

	while (p != NULL) {
		while (isspace ((int)*p)) p++;		/* Skip leading whitespace */

		if (strcasecmp ("version", p) == 0)
			f = &c -> version;
		else if (strcasecmp ("path", p) == 0)
			f = &c -> path;
		else if (strcasecmp ("domain", p) == 0)
			f = &c -> domain;
		else if (strcasecmp ("max-age", p) == 0) {
			c -> max_age = atol (strrchr (p, '=') + 1);
			p = strtok (NULL, ";");
			continue;
		}
		else if (strcasecmp ("expires", p) == 0) {
			delta = parse_http_date (p);
			if (delta != -1) c -> max_age = delta;
			p = strtok (NULL, ";");
			continue;
		}
		else if (strcasecmp ("comment", p) == 0 ||
				 strcasecmp ("secure", p) == 0) {
			p = strtok (NULL, ";");
			continue;
		}
		else {		/* Name value pair - this should be first */
			char *equals = NULL;

			if ((equals = strrchr (p, '=')) != NULL) {
				*equals = '\0';

				c -> name = octstr_create (p);
				c -> value = octstr_create (equals + 1);
			}
			else {
				error (0, "parse_cookie: Bad name=value cookie component (%s)", p);
				cookie_destroy (c);
				return NULL;
			}
			p = strtok (NULL, ";");
			continue;
		}

		if (*f != NULL) {	/* Undefined behaviour - 4.2.2 */
			error (0, "parse_cookie: Duplicate cookie field (%s), discarding", p);
			p = strtok (NULL, ";");
			continue;
		}

		*f = octstr_create ("$");
		octstr_append_cstr (*f, p);
		p = strtok (NULL, ";");
	}

	/* Process version - 4.3.4 */

	if (c -> version == NULL) {
		c -> version = octstr_create_empty ();
		octstr_append_cstr (c -> version, "$Version=\"0\";");
	}

	gw_free (v);
	return c;
}

/*
 * Function: add_cookie_to_cache
 *
 * Description: Adds the cookie to the WSPMachine cookie cache.
 */

static void add_cookie_to_cache (const WSPMachine *sm, Cookie *value)
{
	gw_assert (sm != NULL);
	gw_assert (sm -> cookies != NULL);
	gw_assert (value != NULL);

	list_append(sm -> cookies, value);

	return;
}

/*
 * Function: have_cookie
 *
 * Description: Checks to see if the cookie is present in the list.
 */

static int have_cookie (List *cookies, Cookie *cookie)
{
	Cookie *value = NULL;
	long pos = 0;

	if (cookies == NULL || cookie == NULL) {
		error (0, "have_cookie: Null argument(s) - no Cookie list, Cookie or both");
		return 0;
	}

	/* Walk through the cookie cache, comparing cookie */

	while (pos < list_len (cookies)) {
		value = list_get (cookies, pos);

		/* Worrying - octstr_compare returns 0 on MIN (len1, len2) == 0 - but this
		   behaviour marked as a bug ! */

		if (((value -> name) && octstr_compare (value -> name, cookie -> name) == 0) &&
			((value -> path) && octstr_compare (value -> path, cookie -> path) == 0) &&
			((value -> domain) && octstr_compare (value -> domain, cookie -> domain) == 0)) {
			
			/* We have a match according to 4.3.3 - discard the old one */

			cookie_destroy (value);
			list_delete (cookies, pos, 1);

			/* Discard the new cookie also if max-age is 0 */

			if (cookie -> max_age == 0) 
				return 1;

			debug ("wap.wsp.http", 0, "have_cookie: Updating cached cookie (%s)", 
				octstr_get_cstr (cookie->name));
		}
		else
			pos++;
	}

	return 0;
}

/*
 * Function: expire_cookies
 *
 * Description: Walks thru the cookie list and checking for expired cookies.
 */

static void expire_cookies (List *cookies)
{
	Cookie *value = NULL;
	time_t now = 0;
	long pos = 0;

	if (cookies == NULL) {
		error (0, "expire_cookies: Null argument(s) - no Cookie list");
		return;
	}

	/* Walk through the cookie cache */

	time (&now);

	if (list_len (cookies) > 0) {
		debug ("wap.wsp.http", 0, "expire_cookies: Cookies in cache");
		for (pos = 0; pos < list_len (cookies); pos++) {
			value = list_get(cookies, pos);
			gw_assert (value != NULL);

			if (value -> max_age != -1) {		/* Interesting value */
				if (value -> max_age + value -> birth < now) {
					debug ("wap.wsp.http", 0, "expire_cookies: Expired cookie (%s)",
						octstr_get_cstr(value->name));
					cookie_destroy (value);
					list_delete (cookies, pos, 1);
				}
			}
		}
	}
	else
		debug ("wap.wsp.http", 0, "expire_cookies: No cookies in cache");

	return;
}

static void cookie_destroy (Cookie *p)
{
	if (p == NULL) return;

	octstr_destroy (p -> name);
	octstr_destroy (p -> value);
	octstr_destroy (p -> version);
	octstr_destroy (p -> domain);
	octstr_destroy (p -> path);

	gw_free (p);
	debug ("wap.wsp.http", 0, "cookie_destroy: Destroyed cookie");
	return;
}

/*
 * Function: parse_http_date
 *
 * Description: Parses an HTTP date format as used by the Expires: header. See RFC 2616
 *		section 3.3.1 for more information.HTTP applications have historically allowed three different formats
   for the representation of date/time stamps:

      Sun, 06 Nov 1994 08:49:37 GMT  ; RFC 822, updated by RFC 1123
      Sunday, 06-Nov-94 08:49:37 GMT ; RFC 850, obsoleted by RFC 1036
      Sun Nov  6 08:49:37 1994       ; ANSI C's asctime() format

   The first format is preferred as an Internet standard and represents
   a fixed-length subset of that defined by RFC 1123 [8] (an update to
   RFC 822 [9]). The second format is in common use, but is based on the
   obsolete RFC 850 [12] date format and lacks a four-digit year.
   HTTP/1.1 clients and servers that parse the date value MUST accept
   all three formats (for compatibility with HTTP/1.0), though they MUST
   only generate the RFC 1123 format for representing HTTP-date values
   in header fields.
 *
 * Returns: -1 on success, max-age sematic value on success.
 */

static const char* months [] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", NULL
};

static int month_index (const char *s)
{
	const char **p = &months[0];
	int i = 0;

	while (*p != NULL) {
		if (strcmp (s, *p) == 0)
			return i;
		p++, i++;
	}

	return -1;
}

static int parse_http_date (const char *expires)
{
	struct tm ti;
	char *p = NULL;
	char *date = NULL;
	char month [MAX_HTTP_DATE_LENGTH];
	time_t rv;
	time_t now;

	memset (&ti, 0, sizeof(struct tm));

	/* Break up the Expires: header */

	if (!(date = strchr (expires, '='))) {
		error (0, "parse_http_date: Bogus expires type=value header (%s)", expires);
		return -1;
	}
	else {
		date++;
		while (isspace((int)*date))
			++date;
	}

	/* Onto the date value */

	if(!(p = strchr (date,' '))) {
		error (0, "parse_http_date: Bogus date string (%s)", date);
		return -1;
	} else
		while (isspace((int)*p))
			++p;

	if (MAX_HTTP_DATE_LENGTH < strlen(p)) {
		error (0, "parse_http_date: %s blows length limit (%d)", date, MAX_HTTP_DATE_LENGTH);
		return -1;
	}

	if (isalpha ((int)*p)) {
		/* ctime */
		sscanf (p, (strstr(p, "DST") ? "%s %d %d:%d:%d %*s %d" : "%s %d %d:%d:%d %d"),
			month, &ti.tm_mday, &ti.tm_hour, &ti.tm_min,
			&ti.tm_sec, &ti.tm_year);
		ti.tm_year -= 1900;

	} else if (p[2] == '-') {
		/* RFC 850 (normal HTTP) */

		char  buf [MAX_HTTP_DATE_LENGTH];

		sscanf(p,"%s %d:%d:%d", buf, &ti.tm_hour, &ti.tm_min, &ti.tm_sec);
		buf [2] = '\0';
		ti.tm_mday = atoi(buf);
		buf [6] = '\0';

		strcpy(month,&buf[3]);
		ti.tm_year = atoi (&buf[7]);

		/* Prevent wraparound from ambiguity */

		if (ti.tm_year < 70) {
			ti.tm_year += 100;
		} else if (ti.tm_year > 1900) {
			ti.tm_year -= 1900;
		}
	} else {
		/* RFC 822 */

		sscanf (p,"%d %s %d %d:%d:%d",&ti.tm_mday, month, &ti.tm_year,
			&ti.tm_hour, &ti.tm_min, &ti.tm_sec);

		/* since tm_year is years since 1900 and the year we parsed
		 * is absolute, we need to subtract 1900 years from it
		 */

		ti.tm_year -= 1900;
	}

	ti.tm_mon = month_index (month);
	if (ti.tm_mon == -1) {
		error (0, "parse_http_date () failed on bad month value (%s)", month);
		return -1;
	}

	ti.tm_isdst = -1;

	rv = mktime (&ti);
	if (ti.tm_isdst)
		rv -= 3600;

	if (rv == -1) {
		error (0, "parse_http_date(): mktime() was unable to resolve date/time: %s", 
			asctime (&ti));
		return -1;
	}

	debug ("parse_http_date", 0, "Parsed date (%s) as: %s", date, asctime (&ti));

	/* If rv is valid, it should be some time in the (near) future. Normalise this to
	 * a max-age semantic so we can use the same expiry mechanism 
	 */

	now = time (NULL);

	if (rv - now < 0) {

		/* This is bad - set the delta to 0 so we expire next time around */

		error (0, "parse_http_date () Expiry time (%s) (delta=%ld) is in the past !", 
			asctime (&ti), rv-now);
		return 0;
	}

	return rv - now;
}
 
