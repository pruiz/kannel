/*
 * gwlib/charset.h - character set conversions
 *
 * This header defines some utility functions for converting between
 * character sets.  Approximations are made when necessary, so avoid
 * needless conversions.
 * 
 * Currently only GSM and Latin-1 are supported with Kannel specific 
 * functions. This module contains also wrappers for libxml2 character 
 * set conversion functions that work either from or to UTF-8. More 
 * about libxml2's character set support on the header file 
 * <libxml/encoding.h> or the implementation file encoding.c. Short 
 * version: it has a few basic character set supports built in; for 
 * the rest iconv is used.
 *
 * Richard Braakman
 * Tuomas Luttinen
 */

#ifndef CHARSET_H
#define CHARSET_H

#include <libxml/encoding.h>
#include <libxml/tree.h>

/*
 * Initialize the charset subsystem.
 */
void charset_init(void);

/*
 * Shutdown the charset subsystem.
 */
void charset_shutdown(void);

/* Convert a string in the GSM default character set (GSM 03.38)
 * to ISO-8859-1.  A series of Greek characters (codes 16, 18-26)
 * are not representable and are converted to '?' characters.
 * GSM default is a 7-bit alphabet.  Characters with the 8th bit
 * set are left unchanged. */
void charset_gsm_to_latin1(Octstr *gsm);

/* Convert a string in the ISO-8859-1 character set to the GSM 
 * default character set (GSM 03.38).  A large number of characters
 * are not representable.  Approximations are made in some cases
 * (accented characters to their unaccented versions, for example),
 * and the rest are converted to '?' characters. */
void charset_latin1_to_gsm(Octstr *latin1);

/* 
 * Convert from GSM default character set to NRC ISO 21 (German)
 * and vise versa.
 */
void charset_gsm_to_nrc_iso_21_german(Octstr *ostr);
void charset_nrc_iso_21_german_to_gsm(Octstr *ostr);

/* Trunctate a string of GSM characters to a maximum length.
 * Make sure the last remaining character is a whole character,
 * and not half of an escape sequence.
 * Return 1 if any characters were removed, otherwise 0. 
 */
int charset_gsm_truncate(Octstr *gsm, long max);

/* Convert a string from  character set specified by charset_from into 
 * UTF-8 character set. The result is stored in the octet string *to that 
 * is allocated by the function. The function returns the number of bytes 
 * written for success, -1 for general error, -2 for an transcoding error 
 * (the input string wasn't valid string in the character set it was said 
 * to be or there was no converter found for the character set).
 */
int charset_to_utf8(Octstr *from, Octstr **to, Octstr *charset_from);

/* Convert a string from UTF-8 character set into another character set 
 * specified by charset_from. The result is stored in the octet string *to 
 * that is allocated by the function. The function returns the number of 
 * bytes written for success, -1 for general error, -2 for an transcoding 
 * error (the input string wasn't valid string in the character set it 
 * was said to be or there was no converter found for the character set).
 */
int charset_from_utf8(Octstr *utf8, Octstr **to, Octstr *charset_to);

/* 
 * Use iconv library to convert an Octstr in place, from source character 
 * set to destination character set
 */
int charset_convert(Octstr *string, char *charset_from, char *charset_to);

#endif
