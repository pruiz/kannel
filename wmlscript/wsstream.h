/*
 *
 * wsstream.h
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Generic input / output stream.
 *
 */

#ifndef WSSTREAM_H
#define WSSTREAM_H

/********************* Types and definitions ****************************/

/* The generic input / output stream that is capable of handling
   ISO/IEC-10646 characters. */

#define WS_STREAM_BUFFER_SIZE	1024

/* Do IO to the stream instance `context'.  When reading, the function
   must read at most `buflen' characters to the buffer `buf' and it
   must return the number of characters read.  When writing, the
   buffer `buf' has `buflen' characters and the function must return
   the number of characters written.  In both operations, if the
   function reads or writes less that `buflen' characters, the EOF is
   assumed to been seen in the stream. */
typedef size_t (*WsStreamIOProc)(void *context, WsUInt32 *buf, size_t buflen);

/* Flush all buffered data of the stream instance `context'.  The
   function returns WS_TRUE if the flushing was successful or WS_FALSE
   otherwise. */
typedef WsBool (*WsStreamFlushProc)(void *context);

/* Close the stream instance `context'. */
typedef void (*WsStreamCloseProc)(void *context);

/* A stream handle. */
struct WsStreamRec
{
    /* The method functions of this stream. */
    WsStreamIOProc io;
    WsStreamFlushProc flush;
    WsStreamCloseProc close;

    /* The stream instance context. */
    void *context;

    /* The current buffered contents of the stream. */
    WsUInt32 buffer[WS_STREAM_BUFFER_SIZE];
    size_t buffer_pos;
    size_t data_in_buffer;

    /* The possible put-back character. */
    WsBool ungetch_valid;
    WsUInt32 ungetch;
};

typedef struct WsStreamRec WsStream;

/********************* Stream access functions **************************/

/* Get a character from the stream `stream'.  The character is
   returned in `ch_return'.  The function returns WS_FALSE if the end
   of stream has been encountered. */
WsBool ws_stream_getc(WsStream *stream, WsUInt32 *ch_return);

/* Put the character `ch' back to the stream `stream'. */
void ws_stream_ungetc(WsStream *stream, WsUInt32 ch);

/* Flush all buffered data to the stream back-end. */
WsBool ws_stream_flush(WsStream *stream);

/* Close the stream `stream'. */
void ws_stream_close(WsStream *stream);

/********************* Constructors for different streams ***************/

/* A generic constructor to create the actual WsStream from the
   context and from the method functions.  The function returns NULL
   if the stream creation failed. */
WsStream *ws_stream_new(void *context, WsStreamIOProc io,
                        WsStreamFlushProc flush, WsStreamCloseProc close);

/* Create a new file stream to the file handle `fp'.  The argument
   `output' specifies whether the stream is an output or an input
   stream, respectively.  The argument `close' specifies whether the
   file handle `fp' is closed when the stream is closed.  It is
   basicly a good idea to set this argument to WS_FALSE when wrapping
   the system streams (stdin, stdout, stderr) in a WsStream.  The
   function returns NULL if the stream could not be created. */
WsStream *ws_stream_new_file(FILE *fp, WsBool output, WsBool close);

/* Create a new data input stream for `data_len' bytes of ISO-8859/1
   data in `data'.  The function returns NULL if the stream could not
   be created. */
WsStream *ws_stream_new_data_input(const unsigned char *data, size_t data_len);

#endif /* not WSSTREAM_H */
