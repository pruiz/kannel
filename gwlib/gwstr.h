/*
 * gwlib - string functions
 *
 * Lars Wirzenius & Kalle Marjola 1999
 */

#ifndef _GWSTR_H
#define _GWSTR_H


/* Split string `buf' up to `max' words at space characters. Return number
 * of words found. If there are more than `max' words, all the remaining
 * words are the last word, even if it may contain spaces. */
int split_words(char *buf, int max, char **words);


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


/* url-decode given string, doing the appropriate conversion in place.
 * Any corrupted codings ('%pr' for example) are left in place.
 * If the end of the string is malformed ('%n\0' or '%\0') returns -1,
 * 0 otherwise. The string so-far is modified. */
int url_decode(char *string);


#endif
