/*
 * octstr.h - Octet strings
 *
 * This header file declares an abstract data type, Octstr, for storing
 * and manipulating octet strings: strings of arbitrary binary data in
 * 8-bit bytes. Unlike C strings, they can contain the NUL byte ('\0').
 * Conceptually, they consist of a sequence of octets (bytes) and the
 * length of the sequence. There are various basic operations on octet
 * strings: concatenating, comparing, printing, etc.
 *
 * See comments below for explanations on individual functions. Note that
 * all functions use gw_malloc and friends, so they won't return if the
 * memory allocations fail.
 *
 * Lars Wirzenius for WapIT Ltd.
 */

#ifndef OCTSTR_H
#define OCTSTR_H

#include <stdio.h>

#include "list.h"

typedef struct Octstr Octstr;

/*
 * Create an empty octet string. Return pointer to the new object.
 */
Octstr *octstr_create_empty(void);


/*
 * Create an octet string from a NUL-terminated C string. Return pointer to
 * the new object.
 */
Octstr *octstr_create(const char *cstr);

/*
 * as above except that the string is truncated after max_len
 */
Octstr *octstr_create_limited(char *cstr, int max_len);

/*
 * Create an octet string from arbitrary binary data. The length of the
 * data is given, so it can contain NUL characters.
 */
Octstr *octstr_create_from_data(const void *data, long len);


/*
 * Destroy an octet string, freeing all memory it uses. A NULL argument
 * is ignored.
 */
void octstr_destroy(Octstr *ostr);


/*
 * Return the length of (number of octets in) an object string.
 */
long octstr_len(Octstr *ostr);


/*
 * Create a new octet string by copying part of an existing one. Return 
 * pointer to the new object. If `from' is after end of `ostr', an empty
 * octet string is created. If `from+len' is after the end of `ostr', 
 * `len' is reduced appropriately.
 */
Octstr *octstr_copy(Octstr *ostr, long from, long len);


/*
 * as copy but duplicates entirely
 */
Octstr *octstr_duplicate(Octstr *ostr);


/*
 * Create a new octet string by catenating two existing ones. Return 
 * pointer to the new object.
 */
Octstr *octstr_cat(Octstr *ostr1, Octstr *ostr2);


/*
 * Return value of octet at a given position in an octet string. The returned
 * value has a range of 0..255 for valid positions, and -1 if `pos' is
 * after the end of the octet string.
 */
int octstr_get_char(Octstr *ostr, long pos);


/*
 * Replace a single, existing character in an octet string. Operation cannot
 * fail: if pos is not inside the string, the operation will silently be
 * ignored.
 */
void octstr_set_char(Octstr *ostr, long pos, int ch);


/*
 * Copy bytes from octet string into array.
 */
void octstr_get_many_chars(void *buf, Octstr *ostr, long pos, long len);


/*
 * Return pointer to contents of octet string as a NUL-terminated C string.
 * This is guaranteed to have a NUL character at the end, but it is not
 * guaranteed (how could it?) to not contain NUL characters elsewhere.
 * The pointer points directly into the internal buffer of the octet
 * string, and must not be modified, and must not be used after any
 * octstr_* function that modifies the octet string is called after this
 * one. It is meant for printing debug messages easily.
 *
 * If the octet string is empty, an empty C string is returned, not NULL.
 */
char *octstr_get_cstr(Octstr *ostr);


/* Convert the octet string in-place to printable hexadecimal format.
 * "xyz" would be converted to "78797a".  If the uppercase
 * flag is set, 'A' through 'F' are used instead of 'a' through 'f'.
 */
void octstr_binary_to_hex(Octstr *ostr, int uppercase);


/* Convert the octet string in-place from printable hexadecimal
 * format to binary.  "78797a" or "78797A" would be converted to "xyz".
 * If the string is not in the expected format, return -1 and leave
 * the string unchanged.  If all was fine, return 0. */
int octstr_hex_to_binary(Octstr *ostr);


/* Base64-encode the octet string in-place, using the MIME base64
 * encoding defined in RFC 2045.  Note that the result may be
 * multi-line and is always terminated with a CR LF sequence.  */
void octstr_binary_to_base64(Octstr *ostr);


/* Base64-decode the octet string in-place, using the MIME base64
 * encoding defined in RFC 2045. */
void octstr_base64_to_binary(Octstr *ostr);


/* Parse a number at position 'pos' in 'ostr', using the same rules as
 * strtol uses regarding 'base'.  Skip leading whitespace.
 * 
 * Return the position of the first character after the number,
 * or -1 if there was an error.  Return the length of the octet string
 * if the number ran to the end of the string.
 * 
 * Assign the number itself to the location pointed to by 'number', if
 * there was no error.
 * 
 * Possible errno values in case of an error:
 *    ERANGE    The number did not fit in a long.
 *    EINVAL    No digits of the appropriate base were found.
 */
long octstr_parse_long(long *number, Octstr *ostr, long pos, int base);


/* Run the 'filter' function over each character in the specified range.
 * Return 1 if the filter returned true for all characters, otherwise 0.
 * The octet string is not changed.
 * For example: ok = octstr_check_range(o, 1, 10, isdigit);
 */
typedef int (*octstr_func_t)(int);
int octstr_check_range(Octstr *ostr, long pos, long len, octstr_func_t filter);


/* Run the 'map' function over each character in the specified range,
 * replacing each character with the return value of that function.
 * For example: octstr_convert_range(o, 1, 10, tolower);
 */
void octstr_convert_range(Octstr *ostr, long pos, long len, octstr_func_t map);


/*
 * Compare two octet strings, returning 0 if they are equal, negative if
 * `ostr1' is less than `ostr2' (when compared octet-value by octet-value),
 * and positive if greater.
 */
int octstr_compare(Octstr *ostr1, Octstr *ostr2);


/*
 * as above, but comparing is done only up to n bytes
 */
int octstr_ncompare(Octstr *ostr1, Octstr *ostr2, long n);


/*
 * Same as octstr_compare, but compares the content of the octet string to 
 * a C string.
 */
int octstr_str_compare(Octstr *ostr1, char *str);


/*
 * Write contents of octet string to a file. Return -1 for error, 0 for OK.
 */
int octstr_print(FILE *f, Octstr *ostr);


/*
 * Search the character from octet string. Returns the position (index) of 
 * the char in string, -1 if not found.
 */
int octstr_search_char(Octstr *ostr, int ch);


/*
 * Search the character from octet string starting from position pos. Returns 
 * the position (index) of the char in string, -1 if not found.
 */
int octstr_search_char_from(Octstr *ostr, int ch, long pos);


/*
 * Search for a substring in an octet string. Return the start
 * position (index) of the substring, -1 if not found.
 */
int octstr_search_cstr(Octstr *ostr, char *str);


/*
 * Like octstr_search_cstr, but start at a given position.
 */
int octstr_search_cstr_from(Octstr *ostr, char *str, long pos);


/*
 * Search for the octet string 'needle' in the octet string 'haystack'.
 * Return the start position (index) of 'needle' in 'haystack'.
 * Return -1 if not found.
 */
int octstr_search(Octstr *haystack, Octstr *needle);


/*
 * Write contents of octet string to a file, in human readable form. 
 * Return -1 for error, 0 for OK. Octets that are not printable characters
 * are printed using C-style escape notation.
 */
int octstr_pretty_print(FILE *f, Octstr *ostr);


/*
 * Write contents of octet string to a socket. Return -1 for error, 0 for OK.
 */
int octstr_write_to_socket(int socket, Octstr *ostr);

/*
 * Write contents of octet string starting at 'from' to a
 * non-blocking file descriptor.
 * Return the number of octets written.  Return -1 for error.
 * It is possible for this function to write only part of the octstr.
 */
long octstr_write_data(Octstr *ostr, int fd, long from);

/*
 * Read available data from socket and return it as an octstr.
 * Block if no data is available.  If a lot of data is available,
 * read only up to an internal limit.
 * Return -1 for error.
 */
int octstr_append_from_socket(Octstr *ostr, int socket);

/*
 * Replace current contents of the octstr with given data.
 */
void octstr_replace(Octstr *ostr, char *data, long len);


/*
 * Insert one octet string into another. `pos' gives the position
 * in `ostr1' where `ostr2' should be inserted.
 */
void octstr_insert(Octstr *ostr1, Octstr *ostr2, long pos);


/*
 * Insert characters from C array into an octet string. `pos' 
 * gives the position in `ostr' where `data' should be inserted. `len'
 * gives the number of characters in `data'.
 * If the given `pos' is greater than the length of the input octet string,
 * it is set to that length, resulting in an append.
 */
void octstr_insert_data(Octstr *ostr, long pos, char *data, long len);


/*
 * Append characters from C array at the tail of an octet string.
 */
void octstr_append_data(Octstr *ostr, char *data, long len);


/*
 * Append a second octstr to the first.
 */
void octstr_append(Octstr *ostr1, Octstr *ostr2);


/*
 * Append a normal C string at the tail of an octet string.
 */
void octstr_append_cstr(Octstr *ostr, char *cstr);


/*
 * Append a single character at the tail of an octet string.
 */
void octstr_append_char(Octstr *ostr, int ch);


/*
 * truncate length of octstr to 'new_len'. If new_len is same or more
 * than current, do nothing. cannot fail.
 */
void octstr_truncate(Octstr *ostr, int new_len);


/*
 * octstr_strip_blank - strips white space from start and end of a octet
 * string.
 */
void octstr_strip_blank(Octstr *text);


/*
 * octstr_shrink_blank - shrinks following white space characters into one 
 * white space.
 */
void octstr_shrink_blank(Octstr *text);


/*
 * Delete part of an octet string.
 */
void octstr_delete(Octstr *ostr1, long pos, long len);


/*
 * Read the contents of a named file to an octet string. Return pointer to
 * octet string.
 */
Octstr *octstr_read_file(const char *filename);


/*
 * Split an octet string into words at whitespace, and return a list
 * containing the new octet strings.
 */
List *octstr_split_words(Octstr *ostr);


/*
 * Print debugging information about octet string.
 */
void octstr_dump(Octstr *ostr, int level);

/*
 * Send(2) given octstr into 'fd', possibly blocking. 
 * Return -1 if failed, 0 otherwise.
 */
int octstr_send(int fd, Octstr *ostr);

/*
 * Read recv(2) one Octstr from (socket) 'fd'.
 * Blocks until the whole Octstr is read.
 * Return -1 if failed, 0 if the socket was closed, 1 for data.
 */
int octstr_recv(int fd, Octstr **ostr);

/*
 * decode url-encoded octstr in-place.
 * Return 0 if all went fine, or -1 if there was some garbage
 */

int octstr_url_decode(Octstr *ostr);

/*
 * octstr_get_digit_from - search long int from position pos. return found
 * integer or -1 if not found. in case of an overflow or underflow errno is
 * set to ERANGE.
 */
long octstr_get_digit_from(Octstr *ostr, long pos);

/*
 * Treat the octstr as an unsigned array of bits, most significant bit
 * first, and return the indicated bit range as an integer.  numbits
 * must not be larger than 32.  Bits beyond the end of the string will
 * be read as 0.
 */
long octstr_get_bits(Octstr *ostr, long bitpos, int numbits);

/*
 * Treat the octstr as an unsigned array of bits, most significant bit
 * first, and set the indicated bit range to the given value.  numbits
 * must not be larger than 32.  The value must fit in that number of bits.
 * The string will be extended with 0-valued octets as necessary to hold
 * the indicated bit range.
 */
void octstr_set_bits(Octstr *ostr, long bitpos, int numbits, unsigned long value);

#endif
