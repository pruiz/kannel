#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "gwlib.h"
#include "wbmp.h"



/* create empty WBMP */

WBMP *wbmp_create_empty(void)
{
    WBMP *new;

    new = gw_malloc(sizeof(WBMP));
    memset(new, 0, sizeof(WBMP));
    return new;
}

/* delete a WBMP, freeing everything */

void wbmp_delete(WBMP *pic)
{
    gw_free(pic->ext_header_field);
    gw_free(pic->main_image);
    if (pic->animated_image) {
	int i;
	for(i=0; i < pic->animimg_count; i++)
	    gw_free(pic->animated_image[i]);
	gw_free(pic->animated_image);
    }
    gw_free(pic);
}


WBMP *wbmp_create(int type, int width, int height, Octet *data, int flags)
{
    WBMP *new;
    int i, siz;
    Octet val;
    
    new = wbmp_create_empty();

    new->type_field = type;
    if (type == 0) {
	new->fix_header_field = 0x00;
    } else {
	error(0, "WBMP type %d not supported", type);
	return NULL;
    }
    new->width = width;
    new->height = height;
    siz = (width+7)/8 * height;
    
    new->main_image = gw_malloc(siz);
    for(i=0; i < siz; i++) {
	if (flags & REVERSE) val = reverse_octet(data[i]);
	else val = data[i];
	if (flags & NEGATIVE) val = ~val;
	new->main_image[i] = val;
    }    
    return new;
}



/* create Octet stream from given WBMP */

int wbmp_create_stream(WBMP *pic, Octet **stream)
{
    Octet	tmp_w[30], tmp_h[30];
    int		wl, hl, pic_size;
    
    wl = write_variable_value(pic->width, tmp_w);
    hl = write_variable_value(pic->height, tmp_h);

    pic_size = ((pic->width+7)/8) * pic->height;

    if (pic->type_field != 0) {
	error(0, "Unknown WBMP type %d, cannot convert", pic->type_field);
	return -1;
    }
    *stream = gw_malloc(2+wl+hl+pic_size);
    sprintf(*stream, "%c%c", 0x00, 0x00); 
    memcpy(*stream+2, tmp_w, wl);
    memcpy(*stream+2+wl, tmp_h, hl);
    memcpy(*stream+2+wl+hl, pic->main_image, pic_size);

    debug(0, "picture %d x %d, stream length %d",
	  pic->width, pic->height, 2+wl+hl+pic_size);
    
    return (2+wl+hl+pic_size);
}
