/*
 *
 * wsstream_file.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Implementation of the file stream.
 *
 */

#include "wsint.h"

/********************* Types and definitions ****************************/

struct WsStreamFileCtxRec
{
  FILE *fp;

  /* Should the `fp' be closed when the stream is closed. */
  WsBool close_fp;

  /* A temporary buffer for the raw file data. */
  unsigned char buf[WS_STREAM_BUFFER_SIZE];

  /* For file output streams, this variable holds the number of data
     in `buf'. */
  size_t data_in_buf;

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
  WsStreamFileCtx *ctx = (WsStreamFileCtx *) context;
  size_t wrote = 0;
  unsigned char ch;

  while (buflen)
    {
      /* Do we have any space in the stream's internal IO buffer? */
      if (ctx->data_in_buf >= WS_STREAM_BUFFER_SIZE)
	{
	  size_t w;

	  /* No, flush something to our file stream. */
	  w = fwrite(ctx->buf, 1, ctx->data_in_buf, ctx->fp);
	  if (w < ctx->data_in_buf)
	    {
	      /* Write failed.  As a result code we return the number
                 of characters written from our current write
                 request. */
	      ctx->data_in_buf = 0;
	      return wrote;
	    }

	  ctx->data_in_buf = 0;
	}
      /* Now we have space in the internal buffer. */

      /* Here we could perform some sort of conversions from ISO 10646
         to the output character set.  Currently we just support
         ISO-8859/1 and all unknown characters are replaced with
         '?'. */

      if (*buf > 0xff)
	ch = '?';
      else
	ch = (unsigned char) *buf;

      ctx->buf[ctx->data_in_buf++] = ch;

      /* Move forward. */
      buf++;
      buflen--;
      wrote++;
    }

  return wrote;
}


static WsBool
file_flush(void *context)
{
  WsStreamFileCtx *ctx = (WsStreamFileCtx *) context;

  /* If the internal buffer has any data, then this stream must be an
     output stream.  The variable `data_in_buf' is not updated on
     input streams. */
  if (ctx->data_in_buf)
    {
      if (fwrite(ctx->buf, 1, ctx->data_in_buf, ctx->fp) != ctx->data_in_buf)
	{
	  /* The write failed. */
	  ctx->data_in_buf = 0;
	  return WS_FALSE;
	}

      /* The temporary buffer is not empty. */
      ctx->data_in_buf = 0;
    }

  /* Flush the underlying file stream. */
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
