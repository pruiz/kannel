/*
 * gwlib - string functions
 *
 * Lars Wirzenius & Kalle Marjola 1999
 */

#ifndef _GWSTR_H
#define _GWSTR_H


/* Remove leading and trailing whitespace. */
char *trim_ends(char *str);


/* Count the number of times `pat' occurs on `str'. */
int count_occurences(char *str, char *pat);


/* Make a dynamically allocated copy of first `n' characters of `str'. */
char *strndup(char *str, size_t n);


/* like strstr, but ignore case */
char *str_case_str(char *str, char *pat);


/* seek string 's' backward from offset 'start_offset'. Return offset of
 * the first occurance of any character in 'accept' string, or -1 if not
 * found  */
int str_reverse_seek(const char *s, int start_offset, const char *accept);


/* as above but ignoring case */
int str_reverse_case_seek(const char *s, int start_offset, const char *accept);

#endif
