#ifndef _WBMP_H
#define _WBMP_H

/* WBMP - Wireless Bitmap
 *
 * Kalle Marjola 1999 for WapIT Ltd.
 *
 * functions to store WBMPs and and create Octet strings from them
 */

#include "wapitlib.h"

/* extension header parameters... not implemented/supported in any WBMP
 * yet... but for future reference, although I'm quite sure there is
 * something wrong in these...
 */
typedef struct extparam {
    Octet	bitfield;	/* if bitfield additional data */
    /* or */
    char	param[9];	/* parameter */
    char	value[17];	/* and associated value */
} ExtParam;
    
/* WBMP - wireless bitmap format
 *
 * structure to define wireless bitmap - not complete!
 */
typedef struct wbmp {
    MultibyteInt     	type_field;
    Octet	    	fix_header_field;
    /* extension header is a bit more complicated thing that what is
     * represented here but the spesification is a bit obfuscated one
     * and they are not yet used to anything, so it is left undefined
     */
    ExtParam		*ext_header_field;
    int			exthdr_count;	/* total # of ext headers */
    MultibyteInt	width;
    MultibyteInt	height;
    Octet		*main_image;
    Octet		**animated_image;
    int			animimg_count;	/* total # of animated images */
} WBMP;

/* create a new empty WBMP struct. Return a pointer to it or NULL if
 * operation fails
 */
WBMP *wbmp_create_empty(void);


/* delete given WBMP, including freeing the pixmap */
void wbmp_delete(WBMP *pic);


#define NEGATIVE	1	/* source has white=0, black=1 */
#define REVERSE		2	/* source has righmost as most significant */

/* create a new bitmap
 *
 * type: 0 (B/W, Uncompressed bitmap) WBMP - the only type currently
 *  specificated.
 *
 * width and height are size of the bitmap,
 * data is the entire bitmap; from left-top corner to righ-bottom;
 * if the width is not dividable by 8, the rest of the row is padded with
 * zeros. bytes are ordered big-endian
 *
 * target: black=0, white=1, most significant leftmost
 *
 * You can generate raw bitmap in Linux (RH 6.0) with following line:
 * %> convert -monochrome input_file target.mono
 *
 * ..which then requires flags REVERSE and NEGATIVE
 *
 * return pointer to created WBMP, or NULL if fails
 */
WBMP *wbmp_create(int type, int width, int height, Octet *data, int flags);

/* create Octet stream out of given WBMP
 * return the length of stream, *stream is set to new stream which must
 * be freed by the caller
 */
int wbmp_create_stream(WBMP *pic, Octet **stream);


#endif
