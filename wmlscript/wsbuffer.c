/*
 *
 * wsbuffer.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 Markku Rossi, etc.
 *		 All rights reserved.
 *
 * A multipurpose buffer.
 *
 */

#include <wsint.h>

/*
 * Global functions.
 */

void
ws_buffer_init(WsBuffer *buffer)
{
  buffer->len = 0;
  buffer->data = NULL;
}


void
ws_buffer_uninit(WsBuffer *buffer)
{
  ws_free(buffer->data);
  buffer->len = 0;
  buffer->data = NULL;
}


WsBuffer *
ws_buffer_alloc()
{
  return ws_calloc(1, sizeof(WsBuffer));
}


void
ws_buffer_free(WsBuffer *buffer)
{
  ws_free(buffer->data);
  ws_free(buffer);
}


WsBool
ws_buffer_append(WsBuffer *buffer, unsigned char *data, size_t len)
{
  unsigned char *p;

  if (!ws_buffer_append_space(buffer, &p, len))
    return WS_FALSE;

  memcpy(p, data, len);

  return WS_TRUE;
}


WsBool
ws_buffer_append_space(WsBuffer *buffer, unsigned char **p, size_t size)
{
  unsigned char *ndata = ws_realloc(buffer->data, buffer->len + size);

  if (ndata == NULL)
    return WS_FALSE;

  buffer->data = ndata;

  if (p)
    *p = buffer->data + buffer->len;

  buffer->len += size;

  return WS_TRUE;
}


unsigned char *
ws_buffer_ptr(WsBuffer *buffer)
{
  return buffer->data;
}


size_t
ws_buffer_len(WsBuffer *buffer)
{
  return buffer->len;
}


unsigned char *
ws_buffer_steal(WsBuffer *buffer)
{
  unsigned char *p = buffer->data;

  buffer->data = NULL;
  buffer->len = 0;

  return p;
}
