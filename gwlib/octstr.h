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
 * Several octet strings can also be placed in a list, OctstrList.
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

typedef struct Octstr Octstr;
typedef struct OctstrList OctstrList;

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
 * as octstr_create except that the string is converted to lower case
 */
Octstr *octstr_create_tolower(const char *cstr);

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
 * Create a new octet string by catenating an existing one and one character. 
 * Return pointer to the new object.
 */
Octstr *octstr_cat_char(Octstr *ostr1, int ch);


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
 * Search the string from octet string. Returns the start position (index) of
 * the substring, -1 if not found.
 */
int octstr_search_str(Octstr *ostr, char *str) ;


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
 * Append a normal C string at the tail of an octet string.
 */
void octstr_append_cstr(Octstr *ostr, char *cstr);


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
 * Create an empty list of octet strings. Return pointer to new list.
 */
OctstrList *octstr_list_create(void);


/*
 * Destroy a list of octet strings. If `strings_also' is non-zero (true),
 * the strings in the list are also destroyed (using octstr_destroy).
 */
void octstr_list_destroy(OctstrList *list, int strings_also);


/*
 * Return length of octet string.
 */
long octstr_list_len(OctstrList *list);


/*
 * Append a new octet string to end of list.
 */
void octstr_list_append(OctstrList *list, Octstr *ostr);


/*
 * Return an item in an octet string. `index' starts at 0. If it is outside
 * the length of the list, return NULL.
 */
Octstr *octstr_list_get(OctstrList *list, long index);


/*
 * Split an octet string into words at whitespace, and return a list
 * containing the new octet strings.
 */
OctstrList *octstr_split_words(Octstr *ostr);


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

#endif
