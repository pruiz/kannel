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
 * See comments below for explanations on individual functions.
 *
 * Lars Wirzenius for WapIT Ltd.
 */

#ifndef OCTSTR_H
#define OCTSTR_H

#include <stdio.h>

typedef struct Octstr Octstr;
typedef struct OctstrList OctstrList;

/*
 * Create an empty octet string. Return pointer to the new object, or NULL
 * if the operation failed.
 */
Octstr *octstr_create_empty(void);


/*
 * Create an octet string from a NUL-terminated C string. Return pointer to
 * the new object, or NULL if the operation failed.
 */
Octstr *octstr_create(char *cstr);

/*
 * as above except that the string is truncated after max_len
 */
Octstr *octstr_create_limited(char *cstr, int max_len);

/*
 * Create an octet string from arbitrary binary data. The length of the
 * data is given, so it can contain NUL characters.
 */
Octstr *octstr_create_from_data(char *data, size_t len);


/*
 * Destroy an octet string, freeing all memory it uses.
 */
void octstr_destroy(Octstr *ostr);


/*
 * Return the length of (number of octets in) an object string.
 */
size_t octstr_len(Octstr *ostr);


/*
 * Create a new octet string by copying part of an existing one. Return 
 * pointer to the new object, or NULL if the operation failed. If `from'
 * is after end of `ostr', an empty octet string is created. If `from+len'
 * is after the end of `ostr', `len' is reduced appropriately.
 */
Octstr *octstr_copy(Octstr *ostr, size_t from, size_t len);


/*
 * as copy but duplicates entirely
 */
Octstr *octstr_duplicate(Octstr *ostr);


/*
 * Create a new octet string by catenating two existing ones. Return 
 * pointer to the new object, or NULL if the operation failed.
 */
Octstr *octstr_cat(Octstr *ostr1, Octstr *ostr2);


/*
 * Return value of octet at a given position in an octet string. The returned
 * value has a range of 0..255 for valid positions, and -1 if `pos' is
 * after the end of the octet string.
 */
int octstr_get_char(Octstr *ostr, size_t pos);


/*
 * Replace a single, existing character in an octet string. Operation cannot
 * fail: if pos is not inside the string, the operation will silently be
 * ignored.
 */
void octstr_set_char(Octstr *ostr, size_t pos, int ch);


/*
 * Copy characters from octet string into char buffer.
 */
void octstr_get_many_chars(char *buf, Octstr *ostr, size_t pos, size_t len);


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
 * Write contents of octet string to a file. Return -1 for error, 0 for OK.
 */
int octstr_print(FILE *f, Octstr *ostr);


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
 * Replace current contents of the octstr with given data. Return 0
 * for OK, -1 if new data needs more room and allocation fails, in
 * which case the octstr is not modified
 */
int octstr_replace(Octstr *ostr, char *data, size_t len);


/*
 * Insert one octet string into another. Return 0 for OK, -1 for failure.
 * In case of failure, `ostr1' is not modified. `pos' gives the position
 * in `ostr1' where `ostr2' should be inserted.
 */
int octstr_insert(Octstr *ostr1, Octstr *ostr2, size_t pos);


/*
 * Insert characters from C array into an octet string. Return 0 for OK, 
 * -1 for failure. In case of failure, `ostr' is not modified. `pos' 
 * gives the position in `ostr' where `data' should be inserted. `len'
 * gives the number of characters in `data'.
 */
int octstr_insert_data(Octstr *ostr, size_t pos, char *data, size_t len);


/*
 * Delete part of an octet string. This operation cannot fail.
 */
void octstr_delete(Octstr *ostr1, size_t pos, size_t len);


/*
 * Create an empty list of octet strings. Return pointer to new list, or
 * NULL for failure.
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
size_t octstr_list_len(OctstrList *list);


/*
 * Append a new octet string to end of list. Return -1 for failure, 0 for OK.
 * In case of failure, the list is not modified.
 */
int octstr_list_append(OctstrList *list, Octstr *ostr);


/*
 * Return an item in an octet string. `index' starts at 0. If it is outside
 * the length of the list, return NULL.
 */
Octstr *octstr_list_get(OctstrList *list, size_t index);


/*
 * Split an octet string into words at whitespace, and return a list
 * containing the new octet strings. Return NULL if the operation fails.
 */
OctstrList *octstr_split_words(Octstr *ostr);


/*
 * Print debugging information about octet string.
 */
void octstr_dump(Octstr *ostr);

/*
 * Send(2) given octstr into 'fd', possibly blocking. 
 * Return -1 if failed, 0 otherwise.
 */
int octstr_send(int fd, Octstr *ostr);

/*
 * Read recv(2) one Octstr from (socket) 'fd'.
 * Blocks until the whole Octstr is read.
 * Return -1 if failed, 0 otherwise.
 */
int octstr_recv(int fd, Octstr **ostr);

#endif
