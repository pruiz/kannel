/*
 *
 * wsstream_file.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999 Markku Rossi, etc.
 *		 All rights reserved.
 *
 * Implementation of the file stream.
 *
 */

#include <wsint.h>

/********************* Types and definitions ****************************/

struct WsStreamFileCtxRec
{
  FILE *fp;

  /* Should the `fp' be closed when the stream is closed. */
  WsBool close_fp;

  /* A temporary buffer for the raw file data. */
  unsigned char buf[WS_STREAM_BUFFER_SIZE];

  /* Other fields (like character set conversion information) might be
     defined later. */
};

typedef struct WsStreamFileCtxRec WsStreamFileCtx;

/********************* Static method functions **************************/

static size_t
file_input(void *context, WsUInt32 *buf, size_t buflen)
{
  WsStreamFileCtx *ctx = (WsStreamFileCtx *) context;
  size_t read = 0;

  while (buflen > 0)
    {
      size_t toread = buflen < sizeof(ctx->buf) ? buflen : sizeof(ctx->buf);
      size_t got, i;

      got = fread(ctx->buf, 1, toread, ctx->fp);

      /* Convert the data to the stream's IO buffer. */
      for (i = 0; i < got; i++)
	buf[i] = ctx->buf[i];

      buflen -= got;
      buf += got;
      read += got;

      if (got < toread)
	/* EOF seen. */
	break;
    }

  return read;
}


static size_t
file_output(void *context, WsUInt32 *buf, size_t buflen)
{
  /* XXX implement */

  return 0;
}


static WsBool
file_flush(void *context)
{
  WsStreamFileCtx *ctx = (WsStreamFileCtx *) context;

  return fflush(ctx->fp) == 0;
}


static void
file_close(void *context)
{
  WsStreamFileCtx *ctx = (WsStreamFileCtx *) context;

  if (ctx->close_fp)
    fclose(ctx->fp);

  ws_free(ctx);
}

/********************* Global functions *********************************/

WsStream *
ws_stream_new_file(FILE *fp, WsBool output, WsBool close)
{
  WsStreamFileCtx *ctx = ws_calloc(1, sizeof(*ctx));
  WsStream *stream;

  if (ctx == NULL)
    return NULL;

  ctx->fp = fp;
  ctx->close_fp = close;

  if (output)
    stream = ws_stream_new(ctx, file_output, file_flush, file_close);
  else
    stream = ws_stream_new(ctx, file_input, file_flush, file_close);

  if (stream == NULL)
    /* The stream creation failed.  Close the stream context. */
    file_close(ctx);

  return stream;
}
