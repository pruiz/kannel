/* wsp-strings.c: lookup code for various tables defined by WSP standard
 *
 * This file provides functions to translate strings to numbers and numbers
 * to strings according to the Assigned Numbers tables in appendix A
 * of the WSP specification.
 *
 * The tables are in wsp-strings.def, in a special format suitable for
 * use with the C preprocessor, which we abuse liberally to get the
 * interface we want. 
 */

/* Currently we only have LINEAR tables, which number their strings
 * sequentially starting at 0.  Eventually we may also need tables
 * where each string is assigned a number, but we'll leave that 
 * complication until later. */

#include "gwlib/gwlib.h"
#include "wsp-strings.h"

#define TABLE_SIZE(table) ((long)(sizeof(table) / sizeof(table[0])))

static int initialized;

struct element {
	unsigned char *str;
	long number;
};

/* Declare the data */
#define LINEAR(name, strings) \
	static unsigned char *name##_table[] = { strings };
#define STRING(string) string,
#define NUMBERED(name, strings) \
	static struct element name##_table[] = { strings };
#define ASSIGN(string, number) { string, number },
#include "wsp-strings.def"

/* Define the functions for translating number to Octstr */
#define LINEAR(name, strings) \
Octstr *wsp_##name##_to_string(long number) { \
	unsigned char *cstr = wsp_##name##_to_cstr(number); \
	return cstr ? octstr_create(cstr) : NULL; \
}
#define STRING(string)
#include "wsp-strings.def"

/* Define the functions for translating number to constant string */
#define LINEAR(name, strings) \
unsigned char *wsp_##name##_to_cstr(long number) { \
	gw_assert(initialized); \
	if (number < 0 || number >= TABLE_SIZE(name##_table)) \
		return NULL; \
	return name##_table[number]; \
}
#define STRING(string)
#define NUMBERED(name, strings) \
unsigned char *wsp_##name##_to_cstr(long number) { \
	long i; \
	gw_assert(initialized); \
	for (i = 0; i < TABLE_SIZE(name##_table); i++) { \
		if (name##_table[i].number == number) \
			return name##_table[i].str; \
	} \
	return NULL; \
}
#include "wsp-strings.def"

/* Define the functions for translating Octstr to number.  Currently
 * this is a linear search, but it can be speeded up greatly if that
 * proves necessary by creating hash tables or other indices in
 * wsp_strings_init.
 * The functions return -1 if the string is not in the table. */
#define LINEAR(name, strings) \
long wsp_string_to_##name(Octstr *ostr) { \
	long i; \
	gw_assert(initialized); \
	for (i = 0; i < TABLE_SIZE(name##_table); i++) { \
		if (octstr_str_compare(ostr, name##_table[i]) == 0) \
			return i; \
	} \
	return -1; \
}
#define STRING(string)
#define NUMBERED(name, strings) \
long wsp_string_to_##name(Octstr *ostr) { \
	long i; \
	gw_assert(initialized); \
	for (i = 0; i < TABLE_SIZE(name##_table); i++) { \
		if (octstr_str_compare(ostr, name##_table[i].str) == 0) \
			return name##_table[i].number; \
	} \
	return -1; \
}
#include "wsp-strings.def"

void wsp_strings_init(void) {
	initialized = 1;
}

void wsp_strings_shutdown(void) {
	initialized = 0;
}
