#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "gwlib.h"
#include "OTAbitmap.h"



/* create empty OTAbitmap */

OTAbitmap *OTAbitmap_create_empty(void)
{
    OTAbitmap *new;

    new = gw_malloc(sizeof(OTAbitmap));
    memset(new, 0, sizeof(OTAbitmap));
    return new;
}

void OTAbitmap_delete(OTAbitmap *pic)
{
    gw_free(pic->ext_fields);
    gw_free(pic->main_image);
    if (pic->animated_image) {
	int i;
	for(i=0; i < pic->animimg_count; i++)
	    gw_free(pic->animated_image[i]);
	gw_free(pic->animated_image);
    }
    gw_free(pic);
}

OTAbitmap *OTAbitmap_create(int width, int height, int depth,
			    Octet *data, int flags)
{
    OTAbitmap *new;
    int i, j, siz, osiz;
    Octet val;
    
    new = OTAbitmap_create_empty();

    if (width > 255 || height > 255)
	new->infofield = 0x10;		/* set bit */
    else
	new->infofield = 0x00;
    
    new->width = width;
    new->height = height;

    siz = (width * height + 7)/8;
    
    new->main_image = gw_malloc(siz);
    osiz = (width+7)/8 * height;
    for(i=j=0; i<osiz; i++, j+=8) {
	val = data[i];
	if (flags & REVERSE) val = reverse_octet(val);	
	if (flags & NEGATIVE) val = ~val;

	if (i > 0 && i % ((width+7)/8) == 0 && width % 8 > 0)
	    j -= 8 + width % 8;

	if (j % 8 == 0) {
	    new->main_image[j/8] = val;
	}
	else {
	    new->main_image[j/8] |= val >> (j % 8);
	    new->main_image[j/8 + 1] = val << (8 - j % 8);
	}	    
    }
    /* no palette nor animated images, yet */
    
    return new;
}



/* create Octet stream from given OTAbitmap */

int OTAbitmap_create_stream(OTAbitmap *pic, Octet **stream)
{
    Octet	tmp_header[10];
    int		hdr_len;
    int		pic_size;

    if (pic->infofield & 0x10) {
	sprintf(tmp_header, "%c%c%c%c%c%c", pic->infofield, pic->width/256,
		pic->width%256, pic->height/256, pic->height%256, pic->depth); 
	hdr_len = 6;
    } else {
	sprintf(tmp_header, "%c%c%c%c", pic->infofield,
		pic->width, pic->height, pic->depth);
	hdr_len = 4;
    }	
    
    pic_size = (pic->width * pic->height + 7)/8;

    *stream = gw_malloc(pic_size+pic_size);
    memcpy(*stream, tmp_header, hdr_len);
    memcpy(*stream + hdr_len, pic->main_image, pic_size);

    debug(0, "picture %d x %d, stream length %d",
	  pic->width, pic->height, hdr_len + pic_size);
    
    return (hdr_len + pic_size);
}
