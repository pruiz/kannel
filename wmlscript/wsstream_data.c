/*
 *
 * wsstream_data.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Implementation of the data streams.
 *
 */

#include <wsint.h>

/********************* Types and definitions ****************************/

struct WsStreamDataInputCtxRec
{
  const unsigned char *data;
  size_t data_len;
  size_t data_pos;
};

typedef struct WsStreamDataInputCtxRec WsStreamDataInputCtx;

/********************* Static method functions **************************/

static size_t
data_input(void *context, WsUInt32 *buf, size_t buflen)
{
  WsStreamDataInputCtx *ctx = (WsStreamDataInputCtx *) context;
  size_t read;

  for (read = 0;
       read < buflen && ctx->data_pos < ctx->data_len;
       read++, ctx->data_pos++)
    buf[read] = ctx->data[ctx->data_pos];

  return read;
}


static void
data_close(void *context)
{
  WsStreamDataInputCtx *ctx = (WsStreamDataInputCtx *) context;

  ws_free(ctx);
}

/********************* Global functions *********************************/

WsStream *
ws_stream_new_data_input(const unsigned char *data, size_t data_len)
{
  WsStreamDataInputCtx *ctx = ws_calloc(1, sizeof(*ctx));
  WsStream *stream;

  if (ctx == NULL)
    return NULL;

  ctx->data = data;
  ctx->data_len = data_len;

  stream = ws_stream_new(ctx, data_input, NULL, data_close);

  if (stream == NULL)
    /* The stream creation failed.  Close the stream context. */
    data_close(ctx);

  return stream;
}
