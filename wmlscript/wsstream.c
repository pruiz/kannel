/*
 *
 * wsstream.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Generic input / output stream.
 *
 */

#include "wsint.h"

/********************* Global functions *********************************/

WsBool ws_stream_getc(WsStream *stream, WsUInt32 *ch_return)
{
    if (stream->ungetch_valid) {
        *ch_return = stream->ungetch;
        stream->ungetch_valid = WS_FALSE;

        return WS_TRUE;
    }

    if (stream->buffer_pos >= stream->data_in_buffer) {
        /* Read more data to the buffer. */
        stream->buffer_pos = 0;
        stream->data_in_buffer = (*stream->io)(stream->context,
                                               stream->buffer,
                                               WS_STREAM_BUFFER_SIZE);
        if (stream->data_in_buffer == 0)
            /* EOF reached. */
            return WS_FALSE;
    }

    /* Return the next character. */
    *ch_return = stream->buffer[stream->buffer_pos++];

    return WS_TRUE;
}


void ws_stream_ungetc(WsStream *stream, WsUInt32 ch)
{
    stream->ungetch = ch;
    stream->ungetch_valid = WS_TRUE;
}


WsBool ws_stream_flush(WsStream *stream)
{
    if (stream->flush)
        return (*stream->flush)(stream->context);

    return WS_TRUE;
}


void ws_stream_close(WsStream *stream)
{
    if (stream->close)
        (*stream->close)(stream->context);

    ws_free(stream);
}


WsStream *ws_stream_new(void *context, WsStreamIOProc io,
                        WsStreamFlushProc flush, WsStreamCloseProc close)
{
    WsStream *stream = ws_calloc(1, sizeof(*stream));

    if (stream == NULL)
        return NULL;

    stream->io = io;
    stream->flush = flush;
    stream->close = close;
    stream->context = context;

    return stream;
}
