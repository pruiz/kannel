/*
 * wap_push_pap_mime.h: Header for a (gateway oriented) mime parser for pap 
 * module.
 *
 * By Aarno Syvänen for Wapit Ltd 
 */

#ifndef WAP_PUSH_PAP_MIME_H
#define WAP_PUSH_PAP_MIME_H

#include "gwlib/gwlib.h"

/*
 * Implementation of the external function, PAP uses MIME type multipart/
 * related to communicate a push message and related control information from 
 * pi to ppg. Mime_parse separates parts of message and in addition returns
 * MIME-part-headers of the content entity. Preamble and epilogue of are dis-
 * carded from control messages, but not from a multipart content entity.
 * Multipart/related content type is defined in rfc 2046, chapters 5.1, 5.1.1, 
 * and 5.1.7. Grammar is capitulated in rfc 2046 appendix A and in rfc 822, 
 * appendix D. 
 */

int mime_parse(Octstr *boundary, Octstr *mime_content, Octstr **pap_content, 
               Octstr **push_data, List **content_headers, 
               Octstr **rdf_content);

#endif
