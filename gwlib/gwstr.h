/*
 * gwlib - string functions
 *
 * Lars Wirzenius & Kalle Marjola 1999
 */

#ifndef _GWSTR_H
#define _GWSTR_H


/* Remove leading and trailing whitespace. */
char *trim_ends(char *str);


/* like strstr, but ignore case */
char *str_case_str(char *str, char *pat);


/* seek string 's' backward from offset 'start_offset'. Return offset of
 * the first occurance of any character in 'accept' string, or -1 if not
 * found  */
int str_reverse_seek(const char *s, int start_offset, const char *accept);


#endif
