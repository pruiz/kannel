/* wsp-strings.h: interface to tables defined by WSP standard
 *
 * This file defines an interface to the Assigned Numbers tables
 * in appendix A of the WSP specification.  For each supported
 * table there is a function to convert from number to string and
 * a function to convert from string to number.
 *
 * The tables are in wsp-strings.def, in a special format suitable for
 * use with the C preprocessor, which we abuse liberally to get the
 * interface we want. 
 *
 * For a table named foo, these functions will be declared:
 * Octstr *wsp_foo_to_string(long number);
 * unsigned char *wsp_foo_to_cstr(long number);
 * long wsp_string_to_foo(Octstr *ostr);
 * The first will return NULL if the number has no assigned string.
 * The second will return -1 if the string has no assigned number.
 */

/* Must be called before any of the other functions in this file */
void wsp_strings_init(void);

/* Call this to clean up memory allocations */
void wsp_strings_shutdown(void);

/* Declare the functions */
#define LINEAR(name, strings) \
Octstr *wsp_##name##_to_string(long number); \
unsigned char *wsp_##name##_to_cstr(long number); \
long wsp_string_to_##name(Octstr *ostr);
#define STRING(string)
#include "wsp-strings.def"

/* Define the enumerated types */
#define LINEAR(name, strings)
#define STRING(string)
#define NAMED(name, strings) enum name##_enum { strings name##_dummy };
#define NSTRING(string, name) name,
#include "wsp-strings.def"
