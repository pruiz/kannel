#ifndef _OTABITMAP_H
#define _OTABITMAP_H

/* OTA Bitmap - used for CLI Icon and Operator logo messages
 *
 * Kalle Marjola 1999 for WapIT Ltd.
 *
 * functions to store OTA Bitmaps and and create Octet strings from them
 */

#include "gwlib.h"

/* OTA Bitmap
 */
typedef struct OTA_bitmap {
    Octet		infofield;
    Octet		*ext_fields;
    int			extfield_count;
    int			width;	  	/* 8 or 16 bits, defined by infofield */
    int 	     	height;		/* ditto */
    int		  	depth;
    Octet		*main_image;
    Octet		**animated_image;
    int			animimg_count;	/* total # of animated images */
    /* Octet		*palette; */
} OTAbitmap;

/* create a new empty OTAbitmap struct. Return a pointer to it or NULL if
 * operation fails
 */
OTAbitmap *OTAbitmap_create_empty(void);


/* delete given OTAbitmap, including freeing the pixmap */
void OTAbitmap_delete(OTAbitmap *pic);


#define NEGATIVE	1	/* source has white=0, black=1 */
#define REVERSE		2	/* source has righmost as most significant */

/* create a new bitmap
 *
 * width and height are size of the bitmap,
 * data is the entire bitmap; from left-top corner to righ-bottom;
 * if the width is not dividable by 8, the rest of the row is NOT padded 
 * with zeros. bytes are ordered big-endian
 *
 * target: black=0, white=1, most significant leftmost
 *
 * You can generate raw bitmap in Linux (RH 6.0) with following line:
 * %> convert -monochrome input_file target.mono
 *
 * ..which then requires flags REVERSE and NEGATIVE
 *
 * return pointer to created OTAbitmap, or NULL if fails
 */
OTAbitmap *OTAbitmap_create(int width, int height, int depth, Octet *data, int flags);

/* create Octet stream out of given OTAbitmap
 * return the length of stream, *stream is set to new stream which must
 * be freed by the caller
 */
int OTAbitmap_create_stream(OTAbitmap *pic, Octet **stream);


#endif
