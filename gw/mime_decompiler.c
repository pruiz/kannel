/*
 * mime_decompiler.c - decompiling application/vnd.wap.multipart.* 
 *                     to multipart/ *
 *
 * This is a header for Mime decompiler for decompiling binary mime
 * format to text mime format, which is used for transmitting POST  
 * data from mobile terminal to decrease the use of the bandwidth.
 *
 * See comments below for explanations on individual functions.
 *
 * Bruno Rodrigues
 */

#include "config.h"

#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include "gwlib/gwlib.h"
#include "wap/wsp.h"
#include "wap/wsp_headers.h"
#include "wap/wsp_strings.h"
#include "mime_decompiler.h"

/***********************************************************************
 * Declarations of data types. 
 */

int mime_decompile(Octstr *binary_mime, Octstr **mime)
{ 
    char *boundary = "kannel_boundary";
    ParseContext *context;
    long mime_parts;
    long i, j;
    unsigned long headers_len, data_len;

    i = mime_parts = headers_len = data_len = 0;

    debug("wap.wsp.multipart.form.data", 0, "MIMEDEC: begining decoding");

    if(binary_mime == NULL || octstr_len(binary_mime) < 1) {
        warning(0, "MIMEDEC: invalid mime, ending");
        return -1;
    }
    *mime = octstr_create("");

    /* already dumped in deconvert_content
    debug("mime", 0, "MMSDEC: binary mime dump:");
    octstr_dump(binary_mime, 0);
    */

    context = parse_context_create(binary_mime);
    debug("mime", 0, "MIMEDEC: context created");

    mime_parts = parse_get_uintvar(context);
    debug("mime", 0, "MIMEDEC: mime has %ld multipart entities", mime_parts);
    if(mime_parts == 0) {
        debug("mime", 0, "MIMEDEC: mime has none multipart entities, ending");
        return 0;
    }

    while(parse_octets_left(context) > 0) {
        Octstr *headers, *data;
        List *list_headers;
        i++;
    
        octstr_append(*mime, octstr_imm("--"));
        octstr_append(*mime, octstr_imm(boundary));
        octstr_append(*mime, octstr_imm("\n"));

        headers_len = parse_get_uintvar(context);
        data_len = parse_get_uintvar(context);
        debug("mime", 0, "MIMEDEC[%ld]: headers length <0x%02lx>, "
                         "data length <0x%02lx>", i, headers_len, data_len);

        if((headers = parse_get_octets(context, headers_len)) != NULL) {
            list_headers = wsp_headers_unpack(headers, 1);
            for(j=0; j<list_len(list_headers);j++) {
                octstr_append(*mime, list_get(list_headers, j));
                octstr_append(*mime, octstr_imm("\n"));
            }
        } else {
            error(0, "MIMEDEC[%ld]: headers length is out of range, ending", i);
            return -1; 
        }

        if((data = parse_get_octets(context, data_len)) != NULL ||
           (i = mime_parts && /* XXX SE-T610 eats last byte, which is generally null */
	    (data = parse_get_octets(context, data_len - 1)) != NULL)) { 
            debug("mime", 0, "MMSDEC[%ld]: body [%s]", i, octstr_get_cstr(data));
            octstr_append(*mime, octstr_imm("\n"));
            octstr_append(*mime, data);
            octstr_append(*mime, octstr_imm("\n"));
        } else {
            error(0, "MIMEDEC[%ld]: data length is out of range, ending", i);
            return -1;
        }
    }
    octstr_append(*mime, octstr_imm("--"));
    octstr_append(*mime, octstr_imm(boundary));
    octstr_append(*mime, octstr_imm("--\n"));

    /* already dumped in deconvert_content
    debug("mime", 0, "MMSDEC: text mime dump:");
    octstr_dump(*mime, 0);
    */

    return 0;
}

