#ifndef _ALT_CHARSETS_H
#define _ALT_CHARSETS_H

/* file of FtS -kludges (Fuck the Standards)
 *
 * used to bypass non-standard charset things in SMS Centers
 *
 * Kalle Marjola 1999
 */


/* this one is for Nokia CIMD 1.3.6 SMS Center - it causes error
 * if a standard specified dollar sign is sent.
 */
#define CIMD_PLAIN_DOLLAR_SIGN		1

/* for Sonera EMI, for unknown reason the $ and ¡ characters
 * are swapped in ISO to MT conversion
 */

#define EMI_SWAPPED_CHARS		2


#endif
