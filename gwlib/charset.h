/*
 * gwlib/charset.h - character set conversions
 *
 * This header defines some utility functions for converting between
 * character sets.  Approximations are made when necessary, so avoid
 * needless conversions.
 * 
 * Currently only GSM and Latin-1 are supported.
 *
 * Richard Braakman
 */

#ifndef CHARSET_H
#define CHARSET_H

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

/* Trunctate a string of GSM characters to a maximum length.
 * Make sure the last remaining character is a whole character,
 * and not half of an escape sequence.
 * Return 1 if any characters were removed, otherwise 0. 
 */
int charset_gsm_truncate(Octstr *gsm, long max);

#endif
