/*
 *
 * wsbuffer.h
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * A multipurpose buffer.
 *
 */

#ifndef WSBUFFER_H
#define WSBUFFER_H

/********************* Types and defintions *****************************/

/* A multipurpose buffer.  The contents of the buffer handle is
   visible but its internals should not be modified directly. */
struct WsBufferRec
{
    size_t len;
    unsigned char *data;
};

typedef struct WsBufferRec WsBuffer;

/********************* Global functions *********************************/

/* Initialize the buffer `buffer'.  The buffer is not allocated; the
   argument `buffer' must point to allocated buffer. */
void ws_buffer_init(WsBuffer *buffer);

/* Uninitialize buffer `buffer'.  The actual buffer structure is not
   freed; only its internally allocated buffer is freed. */
void ws_buffer_uninit(WsBuffer *buffer);

/* Allocate and initialize a new buffer.  The function returns NULL if
   the allocation failed. */
WsBuffer *ws_buffer_alloc(void);

/* Free the buffer `buffer' and all its resources. */
void ws_buffer_free(WsBuffer *buffer);

/* Append `size' bytes of data from `data' to the buffer `buffer'.
   The function returns WS_TRUE if the operation was successful or
   WS_FALSE otherwise. */
WsBool ws_buffer_append(WsBuffer *buffer, unsigned char *data, size_t len);

/* Append `size' bytes of space to the buffer `buffer'.  If the
   argument `p' is not NULL, it is set to point to the beginning of
   the appended space.  The function returns WS_TRUE if the operation
   was successful of WS_FALSE otherwise.  */
WsBool ws_buffer_append_space(WsBuffer *buffer, unsigned char **p, size_t size);

/* Return a pointer to the beginning of the buffer's data. */
unsigned char *ws_buffer_ptr(WsBuffer *buffer);

/* Return the length of the buffer `buffer'. */
size_t ws_buffer_len(WsBuffer *buffer);

/* Steal the buffer's data.  The function returns a pointer to the
   beginning of the buffer's data and re-initializes the buffer to
   empty data.  The returned data must be with the ws_free() function
   by the caller. */
unsigned char *ws_buffer_steal(WsBuffer *buffer);

#endif /* not WSBUFFER_H */
