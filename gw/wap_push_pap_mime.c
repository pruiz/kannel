/*
 * Implementation of a gateway oriented mime parser for pap module. This 
 * parser follows proxy rules stated in Push Message, chapter 7.
 *
 * By Aarno Syvänen for Wiral Ltd and Global Networks Inc.
 */

#include "wap_push_pap_mime.h"

/*****************************************************************************
 *
 * Prototypes for internal functions
 */

static int is_cr(int c);
static int is_lf(int c);
static int islwspchar(int c);
static long octstr_drop_leading_blanks(Octstr **header_value);
static void drop_separator(Octstr **header_value, long *pos);
static int parse_preamble(Octstr **mime_content, Octstr *boundary);
static long parse_transport_padding(Octstr *mime_content, long pos);
static long parse_terminator(Octstr *mime_content, long pos);
static int parse_body_part(Octstr **multipart, Octstr *boundary, 
                            Octstr **body_part);
static int parse_encapsulation(Octstr **mime_content, Octstr *boundary, 
                               Octstr **push_data, List **content_headers,
			       Octstr **rdf_content);
static int check_control_headers(Octstr **body_part);
static int check_control_content_type_header(Octstr **body_part);
static int drop_optional_header(Octstr **body_part, char *name);
static int drop_header_true(Octstr **body_part, long content_pos);
static int drop_extension_headers(Octstr **mime_content);
static long parse_field_value(Octstr *pap_content, long pos);
static long parse_field_name(Octstr *pap_content, long pos);
static void octstr_split_by_pos(Octstr **mime_content, Octstr **pap_content, 
                                long boundary_pos);
static Octstr *make_close_delimiter(Octstr *boundary);
static Octstr *make_part_delimiter(Octstr *boundary);
static Octstr *make_start_delimiter(Octstr *dash_boundary);
static int pass_data_headers(Octstr **body_part, List **data_headers);
static int check_data_content_type_header(Octstr **body_part, 
                                          List **data_headers);
static int pass_optional_header(Octstr **body_part, char *name, 
                                List **content_headers);
static int pass_extension_headers(Octstr **body_part, List **data_headers);
static long pass_field_name(Octstr **body_part, Octstr **content_header, 
			    long pos);
static long pass_field_value(Octstr **body_part, Octstr **content_header, 
                             long pos);
static int parse_epilogue(Octstr **mime_content);
static int parse_tail(Octstr **multipart, Octstr *part_delimiter, 
                      long boundary_pos, long *next_part_pos);

/*****************************************************************************
 *
 * Implementation of the external function, PAP uses MIME type multipart/
 * related to communicate a push message and related control information from 
 * pi to ppg. Mime_parse separates parts of message and in addition returns
 * MIME-part-headers of the content entity. Preamble and epilogue of are dis-
 * carded from control messages, but not from a multipart content entity. 
 * Already parsed parts of mime content are removed. 
 * Multipart/related content type is defined in rfc 2046, chapters 5.1, 5.1.1, 
 * and 5.1.7. Grammar is capitulated in rfc 2046 appendix A and in rfc 822, 
 * appendix D. PAP, chapter 8 defines how MIME multipart message is used by PAP
 * protocol. Functions called by mime_parse remove parsed parts from the mime
 * content. 
 * Input: pointer to mime boundary and mime content
 * Output: Pointer to pap control document and push data, if parsable, NULL
 * otherwise. If there is a capabilities document, pointer to this is return-
 * ed, too. If there is none, pointer to NULL instead. Neither prologue nor 
 * epilogue is returned. 
 * In addition, return 1 if parsing was succesfull, 0 otherwise.
 */

int mime_parse(Octstr *boundary, Octstr *mime_content, Octstr **pap_content, 
               Octstr **push_data, List **content_headers, 
               Octstr **rdf_content)
{
    int ret;

    *pap_content = NULL;
    *push_data = NULL;
    *content_headers = NULL;
    *rdf_content = NULL;

    if (parse_preamble(&mime_content, boundary) < 0) {
        warning(0, "erroneous preamble");
        return 0;
    }
    if (parse_body_part(&mime_content, boundary, pap_content) <= 0) {
        warning(0, "erroneous control entity");
        return 0;
    }
    if (check_control_headers(pap_content) == 0) {
        warning(0, "erroneous control headers");
        return 0;
    }

    ret = -1;
    if ((ret = parse_encapsulation(&mime_content, boundary, push_data, 
                                   content_headers, rdf_content)) < 0) {
        warning(0, "erroneous content entity (push message)");
        return 0;
    } else if (ret == 0) {
        gw_assert(*rdf_content == NULL);
        if (octstr_len(mime_content) != 0)
            parse_epilogue(&mime_content);
        return 1;
    }

    if (check_control_headers(rdf_content) == 0) {
        warning(0, "erroneous capacity (rdf) headers");
        return 0;
    }

    if (octstr_len(mime_content) != 0)
        parse_epilogue(&mime_content);
    gw_assert(octstr_len(mime_content) == 0);
    
    return 1;
}

/*****************************************************************************
 *
 * Implementation of internal functions
 */

static int is_cr(int c)
{
    return c == '\r';
}

static int is_lf(int c)
{
    return c == '\n';
}

/*
 * Lwspchar is defined in rfc 822, appendix D.
 */
static int islwspchar(int c)
{
    return c == '\t' || c == ' ';
}

/* These thingies we after normally have after delimiters. */
static int parse_tail(Octstr **multipart, Octstr *delimiter, 
                      long boundary_pos, long *next_part_pos)
{
    *next_part_pos = parse_transport_padding(*multipart, 
         boundary_pos + octstr_len(delimiter));

    if ((*next_part_pos = parse_terminator(*multipart, *next_part_pos)) < 0)
        return -1;
    
    return 0;
}


/*
 * Boundary misses crlf here. This is intentional: Kannel header parsing pro-
 * cess drops this terminator.
 */
static int parse_preamble(Octstr **mime_content, Octstr *boundary)
{
    long boundary_pos,
         next_part_pos;
    Octstr *dash_boundary;

    boundary_pos = next_part_pos = -1;
    dash_boundary = make_start_delimiter(boundary);
    
    if ((boundary_pos = octstr_search(*mime_content, dash_boundary, 0)) < 0)
        goto error;

    if (parse_tail(mime_content, dash_boundary, boundary_pos, 
            &next_part_pos) < 0) 
        goto error;

    octstr_delete(*mime_content, 0, next_part_pos);
    octstr_destroy(dash_boundary);

    return 0;

error:
    octstr_destroy(dash_boundary);
    return -1;
}
    
static long parse_terminator(Octstr *mime_content, long pos)
{
    if (is_cr(octstr_get_char(mime_content, pos)))
        ++pos;
    else 
        return -1;

    if (is_lf(octstr_get_char(mime_content, pos)))
        ++pos;
    else 
        return -1;

    return pos;
}

static long parse_transport_padding(Octstr *mime_content, long pos)
{
    while (islwspchar(octstr_get_char(mime_content, pos)))
        ++pos;

    return pos;
}

static long parse_close_delimiter(Octstr *close_delimiter, Octstr *mime_content,
                                  long pos)
{
    if (octstr_ncompare(close_delimiter, mime_content, 
                        octstr_len(close_delimiter)) != 0)
        return -1;
    pos += octstr_len(close_delimiter);

    return pos;
}

/*
 * Splits the first body part away from the multipart message. A body part end with
 * either with another body or with a close delimiter. We first split the body and
 * then remove the separating stuff  from the remainder. If we have the last body
 * part, we must parse all closing stuff. 
 * Returns 1, there is still another body part in the multipart message
 *         0, if there is none
 *         -1, when parsing error.
 */
static int parse_body_part (Octstr **multipart, Octstr *boundary, 
                            Octstr **body_part)
{
    Octstr *part_delimiter,
           *close_delimiter;
    long boundary_pos,          /* start of the boundary */
         close_delimiter_pos,   /* start of the close delimiter */
         end_pos,               /* end of the message */
         next_part_pos,         /* start of the next part */
         epilogue_pos;          /* start of the epilogue */
 
    part_delimiter = make_part_delimiter(boundary);
    close_delimiter = make_close_delimiter(boundary);

    if ((close_delimiter_pos = octstr_search(*multipart, 
            close_delimiter, 0)) < 0) 
        goto error;

    boundary_pos = octstr_search(*multipart, part_delimiter, 0);
    if (boundary_pos == close_delimiter_pos) {
        *body_part = octstr_create("");
        octstr_split_by_pos(multipart, body_part, close_delimiter_pos);
        if ((epilogue_pos = 
                parse_close_delimiter(close_delimiter, *multipart, 0)) < 0)
            goto error;
        epilogue_pos = parse_transport_padding(*multipart, epilogue_pos);
        octstr_delete(*multipart, 0, epilogue_pos);
	goto last_part;
    }

    *body_part = octstr_create("");
    octstr_split_by_pos(multipart, body_part, boundary_pos);

    if (parse_tail(multipart, part_delimiter, 0, &next_part_pos) < 0) {
        goto error;
    }

    octstr_delete(*multipart, 0, next_part_pos);
    octstr_destroy(part_delimiter);
    octstr_destroy(close_delimiter);

    return 1;

error:
    octstr_destroy(part_delimiter);
    octstr_destroy(close_delimiter);
    return -1;

last_part:
    octstr_destroy(part_delimiter);
    octstr_destroy(close_delimiter);
    return 0;

}

/*
 * PAP, Chapter 8 states that PAP multipart message MUST have at least two 
 * parts, control entity (containing the pap control message) and content 
 * entity (containing the push message). So we must have at least one body
 * part here, and at most two (MIME grammar in rfc 2046, appendix A sets no 
 * limitations here).
 * Returns 1, if rdf content was present
 *         0, if it was absent
 *         -1, when error
 */
static int parse_encapsulation(Octstr **mime_content, Octstr *boundary, 
                               Octstr **push_data, List **content_headers,
                               Octstr **rdf_content)
{
    int ret;

    ret = -1;
    if ((ret = parse_body_part(mime_content, boundary, push_data)) < 0)
        return -1;
    if (pass_data_headers(push_data, content_headers) == 0)
        return -1;

    if (ret == 0) {
        *rdf_content = NULL;
        return 0;
    }

    if ((ret = parse_body_part(mime_content, boundary, rdf_content)) < 0 || 
            ret > 0)
        return -1;
    else if (ret == 0)
        return 1;
    
    return 1;
}

/*
 * Split os2 from os1, boundary being boundary_pos.
 */
static void octstr_split_by_pos(Octstr **os1, Octstr **os2, 
                                long boundary_pos)
{
    long i;
    
    for (i = 0; i < boundary_pos; i++) {
        octstr_format_append(*os2, "%c", octstr_get_char(*os1, i));   
    }         

    for (i = 0; i < boundary_pos; i++) {
        octstr_delete(*os1, 0, 1);
    }
}

static Octstr *make_close_delimiter(Octstr *boundary) 
{
    Octstr *close_delimiter;

    close_delimiter = make_part_delimiter(boundary);
    octstr_format_append(close_delimiter, "%s", "--");

    return close_delimiter;
}

static Octstr *make_part_delimiter(Octstr *dash_boundary)
{
    Octstr *part_delimiter;

    part_delimiter = octstr_create("");
    octstr_format_append(part_delimiter, "%c", '\r');
    octstr_format_append(part_delimiter, "%c", '\n');
    octstr_format_append(part_delimiter, "%s", "--");
    octstr_append(part_delimiter, dash_boundary);
    
    return part_delimiter;
}

static Octstr *make_start_delimiter(Octstr *dash_boundary)
{
    Octstr *start_delimiter;

    start_delimiter = octstr_create("");
    octstr_format_append(start_delimiter, "%s", "--");
    octstr_append(start_delimiter, dash_boundary);

    return start_delimiter;
}

/*
 * Control entity headers must contain Content-Type: application/xml headers.
 * Rfc 2045, Appendix A does not specify the order of entity headers and states
 * that all rfc 822 headers having a string "Content" in their field-name must
 * be accepted. Rfc 822 grammar is capitulated in appendix D.
 */
static int check_control_headers(Octstr **body_part)
{
    if (check_control_content_type_header(body_part) == 0)
        return 0;
    if (drop_optional_header(body_part, "Content-Transfer-Encoding:") == 0)
        return 0;
    if (drop_optional_header(body_part, "Content-ID:") == 0)
        return 0;
    if (drop_optional_header(body_part, "Content-Description:") == 0)
        return 0;
    if (drop_extension_headers(body_part) == 0)
        return 0;

    return 1;
}

static int check_control_content_type_header(Octstr **body_part)
{
    long content_pos;

    if ((content_pos = octstr_case_search(*body_part, 
            octstr_imm("Content-Type:"), 0)) < 0 || 
            octstr_case_search(*body_part, 
                octstr_imm("application/xml"), 0) < 0) {
        return 0;
    }

    if (drop_header_true(body_part, content_pos) < 0)
        return 0;
    
    return 1;
}

/*
 * This function actually removes a header (deletes corresponding part from
 * the octet string body_part), in addition of all stuff prepending it. So
 * deleting start from the octet 0. Content_pos tells where the header starts.
 */
static int drop_header_true(Octstr **body_part, long content_pos) 
{
    long next_header_pos;

    next_header_pos = -1;
    if ((next_header_pos = parse_field_value(*body_part, content_pos)) == 0)
        return 0;
    if ((next_header_pos = parse_terminator(*body_part, next_header_pos)) == 0)
        return 0;
    octstr_delete(*body_part, 0, next_header_pos);

    return 1;
}

static int drop_optional_header(Octstr **body_part, char *name)
{
    long content_pos;
         
    content_pos = -1;
    if ((content_pos = octstr_case_search(*body_part, 
             octstr_imm(name), 0)) < 0)
        return 1;
    
    if (drop_header_true(body_part, content_pos) < 0)
        return 0;

    return 1;
}

/*
 * Extension headers are defined in rfc 822, Appendix D, as fields. We must
 * parse all rfc 822 headers containing a string "Content". These headers 
 * are optional, too.
 */
static int drop_extension_headers(Octstr **body_part)
{
    long content_pos,
         next_header_pos;  

    do {
        if ((content_pos = octstr_case_search(*body_part, 
                 octstr_imm("Content"), 0)) < 0)
            return 1;
        if ((next_header_pos = parse_field_name(*body_part, content_pos)) < 0)
            return 0;
        if ((next_header_pos = parse_field_value(*body_part, 
                 next_header_pos)) < 0)
	    return 0;
        if ((next_header_pos = parse_terminator(*body_part, 
                 next_header_pos)) == 0)
            return 0;
    } while (islwspchar(octstr_get_char(*body_part, next_header_pos)));

    octstr_delete(*body_part, content_pos, next_header_pos - content_pos);
   
    return 1;
}

static long parse_field_value(Octstr *pap_content, long pos)
{
    int c;

    while (!is_cr(c = octstr_get_char(pap_content, pos)) &&
	     pos < octstr_len(pap_content)) {
         ++pos;
    }
 
    if (is_lf(c)) {
        if (is_lf(octstr_get_char(pap_content, pos))) {
	    ++pos;
        } else {
	    return -1;
        }
    }

    if (pos == octstr_len(pap_content)) {
        return -1;
    }

    return pos;
}

static long parse_field_name(Octstr *content, long pos)
{
    while (octstr_get_char(content, pos) != ':' && 
               pos < octstr_len(content))
           ++pos;

    if (pos == octstr_len(content))
        return -1;

    return pos;
}

/*
 * Transfer entity headers of a body part (it is, from the content entity) 
 * to a header list. Push Message, chapter 6.2.1.10 states that Content-Type
 * header is mandatory. 
 * Return 0 when error, 1 otherwise. In addition, return the modified body
 * part and content headers.
 */
static int pass_data_headers(Octstr **body_part, List **data_headers)
{
    *data_headers = http_create_empty_headers();

    if (check_data_content_type_header(body_part, data_headers) == 0) {
        warning(0, "MIME: pass_data_headers: Content-Type header missing"); 
        return 0;
    }
        
    if (pass_optional_header(body_part, "Content-Transfer-Encoding", 
                             data_headers) < 0)
        goto operror;
    if (pass_optional_header(body_part, "Content-ID", data_headers) < 0)
        goto operror;
    if (pass_optional_header(body_part, "Content-Description", 
                         data_headers) < 0)
        goto operror;
    if (pass_extension_headers(body_part, data_headers) == 0)
        goto operror;
   
    return 1;

operror:
    warning(0, "MIME: pass_data_headers: an unparsable optional header");
    return 0;
}

/*
 * Checks if body_part contains a Content-Type header. Tranfers this header to
 * a list content_headers.
 * Return 1, when Content-Type headers was found, 0 otherwise
 */
static int check_data_content_type_header(Octstr **body_part, 
                                          List **content_headers)
{
    long header_pos,
         next_header_pos;
    Octstr *content_header;

    header_pos = next_header_pos = -1;
    content_header = octstr_create("Content-Type");
    
    if ((header_pos = octstr_case_search(*body_part, content_header, 0)) < 0) {
        goto error;
    }
    if ((next_header_pos = pass_field_value(body_part, &content_header, 
	    header_pos + octstr_len(content_header))) < 0) {
        goto error;
    }
    if ((next_header_pos = 
	     parse_terminator(*body_part, next_header_pos)) < 0) {
        goto error;
    }

    octstr_delete(*body_part, header_pos, next_header_pos - header_pos);
    list_append(*content_headers, octstr_duplicate(content_header));
    octstr_destroy(content_header);

    return 1;

error:
    octstr_destroy(content_header);
    return 0;
}

/*
 * We try to find an optional header, so a failure to find one is not an 
 * error. Return -1 when error, 0 when header name not found, 1 otherwise.
 */
static int pass_optional_header(Octstr **body_part, char *name, 
                                List **content_headers)
{
    long content_pos,
         next_header_pos;
    Octstr *osname,
           *osvalue;

    content_pos = next_header_pos = -1;
    osname = octstr_create(name);
    osvalue = octstr_create("");

    if ((content_pos = octstr_case_search(*body_part, osname, 0)) < 0) 
        goto noheader;
    if ((next_header_pos = pass_field_value(body_part, &osvalue, 
	     content_pos + octstr_len(osname))) < 0)
        goto error;   
    if ((next_header_pos = 
	     parse_terminator(*body_part, next_header_pos)) == 0)
        goto error;

    drop_separator(&osvalue, &next_header_pos);
    http_header_add(*content_headers, name, octstr_get_cstr(osvalue));
    octstr_delete(*body_part, content_pos, next_header_pos - content_pos);

    octstr_destroy(osname);
    octstr_destroy(osvalue);
    return 1;

error:
    octstr_destroy(osvalue);
    octstr_destroy(osname);
    return -1;

noheader:
    octstr_destroy(osvalue);
    octstr_destroy(osname);
    return 0;
}

/*
 * Remove ':' plus spaces from the header value
 */
static void drop_separator(Octstr **header_value, long *pos)
{
   long count;

   octstr_delete(*header_value, 0, 1);            /* remove :*/
   count = octstr_drop_leading_blanks(header_value);
   pos = pos - 1 - count;
} 

/*
 * Return number of spaces dropped.
 */
static long octstr_drop_leading_blanks(Octstr **header_value)
{
    long count;

    count = 0;
    while (octstr_get_char(*header_value, 0) == ' ') {
        octstr_delete(*header_value, 0, 1);
        ++count;
    }

    return count;
}

/*
 * Extension headers are optional, see Push Message, chapter 6.2. Field struc-
 * ture is defined in rfc 822, chapter 3.2. Extension headers are defined in 
 * rfc 2045, chapter 9, grammar in appendix A.
 * Return 0 when error, 1 otherwise.
 */
static int pass_extension_headers(Octstr **body_part, List **content_headers)
{
    long next_field_part_pos,
         count;  
    Octstr *header_name,
           *header_value; 

    header_name = octstr_create("");
    header_value = octstr_create("");
    count = 0;
    next_field_part_pos = 0;

    do {
        if ((octstr_case_search(*body_part, octstr_imm("Content"), 0)) < 0)
            goto end; 
        if ((next_field_part_pos = pass_field_name(body_part, &header_name,
                 next_field_part_pos)) < 0)
            goto error;
        if ((next_field_part_pos = pass_field_value(body_part, &header_value, 
                 next_field_part_pos)) < 0)
            goto error;
        if ((next_field_part_pos = parse_terminator(*body_part, 
                 next_field_part_pos)) == 0)
            goto error;
        drop_separator(&header_value, &next_field_part_pos);
        http_header_add(*content_headers, octstr_get_cstr(header_name), 
            octstr_get_cstr(header_value));
    } while (islwspchar(octstr_get_char(*body_part, next_field_part_pos)));

    octstr_delete(*body_part, 0, next_field_part_pos);

/*
 * An intentional fall-through. We must eventually use a function for memory
 * cleaning.
 */
end:
    octstr_destroy(header_name);
    octstr_destroy(header_value);
    return 1;

error:
    octstr_destroy(header_name);
    octstr_destroy(header_value);
    return 0;
}

static long pass_field_value(Octstr **body_part, Octstr **header, 
                             long pos)
{
    int c;

    while (!is_cr(c = octstr_get_char(*body_part, pos)) &&
             pos < octstr_len(*body_part)) {
        octstr_format_append(*header, "%c", c);
        ++pos;
    }
 
    if (pos == octstr_len(*body_part))
        return -1;

    return pos;
}

static long pass_field_name(Octstr **body_part, Octstr **field_part, 
                            long pos)
{
    int c;

    while (((c = octstr_get_char(*body_part, pos)) != ':') &&
            pos < octstr_len(*body_part)) {
        octstr_format_append(*field_part, "%c", c);
        ++pos;
    }

    if (pos == octstr_len(*body_part))
        return -1;

    return pos;
}

/* This is actually CRLF epilogue. */
static int parse_epilogue(Octstr **mime_content)
{
    long pos;

    if (octstr_len(*mime_content) == 0)
        return 0;
    
    if ((pos = parse_terminator(*mime_content, 0)) < 0)
        return -1;

    octstr_delete(*mime_content, 0, octstr_len(*mime_content));
    return 0;
}

