/*
 * conn.h - declare Connection type to wrap a file descriptor
 *
 * This file defines operations on the Connection type, which provides
 * input and output buffers for a two-way file descriptor, such as a
 * socket or a serial device.
 *
 * The operations are designed for non-blocking use.  Blocking can be
 * done explicitly with conn_wait() or conn_flush().  A thread that
 * blocks in these functions can be woken up with gwthread_wakeup.
 *
 * The write operations will queue the data for sending.  They will
 * try to send whatever data can be sent immediately, if there's enough
 * of it queued.  "Enough" is defined by a value which can be set with
 * conn_set_output_buffering.  The caller must call either conn_wait
 * or conn_flush to actually send the data.
 *
 * The read operations will return whatever data is immediately
 * available.  If none is, then the caller should not simply re-try
 * the request (that would cause a busy-loop); instead, it should
 * wait for more data with conn_wait().
 *
 * The Connection structure has internal locks, so it can be shared
 * safely between threads.  There is a race condition in the interface,
 * however, that can cause threads to wait unnecessarily if there are
 * multiple readers.  But in that case there will always be at least one
 * thread busy reading.
 *
 * The overhead of locking can be avoided by "claiming" a Connection.
 * This means that only one thread will ever do operations on that
 * Connection; the caller must guarantee this.
 *
 * If any operation returns a code that indicates that the connection
 * is broken (due to an I/O error, normally), it will also have closed
 * the connection.  Most operations work only on open connections;
 * not much can be done with a closed connection except destroy it.
 */

typedef struct Connection Connection;

/* If conn_register was called for this connection, a callback function
 * of type conn_callback_t will be called when new input is available,
 * or when all data that was previously queued for output is sent.
 * The data pointer is the one supplied by the caller of conn_register.
 * NOTE: Beware of concurrency issues.  The callback function will run
 * in the fdset's private thread, not in the caller's thread.
 * This also means that if the callback does a lot of work it will slow
 * down the polling process.  This may be good or bad. */
typedef void conn_callback_t(Connection *conn, void *data);

#ifdef HAVE_LIBSSL
/* Open an SSL connection to the given host and port.  Same behavior
 * as conn_open_tcp() below. 'certkeyfile' specifies a PEM-encoded
 * file where OpenSSL looks for a private key and a certificate. 
 */
Connection *conn_open_ssl(Octstr *host, int port, Octstr *certkeyfile);
#endif /* HAVE_LIBSSL */

/* Open a TCP/IP connection to the given host and port.  Return the
 * new Connection.  If the connection can not be made, return NULL
 * and log the problem. */
Connection *conn_open_tcp(Octstr *host, int port);

/* As above, but binds our end to 'our_port'. If 'our_port' is 0, uses
 * any port like conn_open_tcp. */
Connection *conn_open_tcp_with_port(Octstr *host, int port, int our_port);

/* Create a Connection structure around the given file descriptor.
 * The file descriptor must not be used for anything else after this;
 * it must always be accessed via the Connection operations.  This
 * operation cannot fail. */
Connection *conn_wrap_fd(int fd);

/* Close and deallocate a Connection.  Log any errors reported by
 * the close operation. */
void conn_destroy(Connection *conn);

/* Assert that the calling thread will be the only one to ever
 * use this Connection.  From now on no locking will be done
 * on this Connection.
 * It is a fatal error for two threads to try to claim one Connection,
 * or for another thread to try to use a Connection that has been claimed.
 */
void conn_claim(Connection *conn);

/* Return the length of the unsent data queued for sending, in octets. */
long conn_outbuf_len(Connection *conn);

/* Return the length of the unprocessed data ready for reading, in octets. */
long conn_inbuf_len(Connection *conn);

/* Return 1 iff there was an end-of-file indication from the last read or
 * wait operation. */
int conn_eof(Connection *conn);

/* Return 1 iff there was an error indication from the last read or wait
 * operation. */
int conn_read_error(Connection *conn);

/* Try to write data in chunks of this size or more.  Set it to 0 to
 * get an unbuffered connection.  See the discussion on output buffering
 * at the top of this file for more information. */
void conn_set_output_buffering(Connection *conn, unsigned int size);

/* Register this connection with an FDSet.  This will make it unnecessary
 * to call conn_wait.  Instead, the callback function will be called when
 * there is new data available, or when all data queued for output is
 * sent (note that small amounts are usually sent immediately without
 * queuing, and thus won't trigger the callback).  A connection can be
 * registered with only one FDSet at a time.  Return -1 if it was
 * already registered with a different FDSet, otherwise return 0.
 * A connection can be re-registered with the same FDSet.  This will
 * change only the callback information, and is much more efficient
 * than calling conn_unregister first.
 * NOTE: Using conn_register will always mean that the Connection will be
 * used by more than one thread, so don't also call conn_claim. */
int conn_register(Connection *conn, FDSet *fdset,
                  conn_callback_t callback, void *data);

/* Remove the current registration. */
void conn_unregister(Connection *conn);

/* Block the thread until one of the following is true:
 *   - The timeout expires
 *   - New data is available for reading
 *   - Some data queued for output is sent (if there was any)
 *   - The thread is woken up via the wakeup interface (in gwthread.h)
 * Return 1 if the timeout expired.  Return 0 otherwise, if the
 * connection is okay.  Return -1 if the connection is broken.
 * If the timeout is 0 seconds, check for the conditions above without
 * actually blocking.  If it is negative, block indefinitely.
 */
int conn_wait(Connection *conn, double seconds);

/* Try to send all data currently queued for output.  Block until this
 * is done, or until the thread is interrupted or woken up.  Return 0
 * if it worked, 1 if there was an interruption, or -1 if the connection
 * is broken. */
int conn_flush(Connection *conn);

/* Output functions.  Each of these takes an open connection and some
 * data, formats the data and queues it for sending.  It may also
 * try to send the data immediately.  The current implementation always
 * does so.
 * Return 0 if the data was sent, 1 if it was queued for sending,
 * and -1 if the connection is broken.
 */
int conn_write(Connection *conn, Octstr *data);
int conn_write_data(Connection *conn, unsigned char *data, long length);
/* Write the length of the octstr as a standard network long, then
 * write the octstr itself. */
int conn_write_withlen(Connection *conn, Octstr *data);

/* Input functions.  Each of these takes an open connection and
 * returns data if it's available, or NULL if it's not.  They will
 * not block.  They will try to read in more data if there's not
 * enough in the buffer to fill the request. */

/* Return whatever data is available. */
Octstr *conn_read_everything(Connection *conn);

/* Return exactly "length" octets of data, if at least that many
 * are available.  Otherwise return NULL.
 */
Octstr *conn_read_fixed(Connection *conn, long length);

/* If the input buffer starts with a full line of data (terminated by
 * LF or CR LF), then return that line as an Octstr and remove it
 * from the input buffer.  Otherwise return NULL.
 */
Octstr *conn_read_line(Connection *conn);

/* Read a standard network long giving the length of the following
 * data, then read the data itself, and pack it into an Octstr and
 * remove it from the input buffer.  Otherwise return NULL.
 */
Octstr *conn_read_withlen(Connection *conn);

/* If the input buffer contains a packet delimited by the "startmark"
 * and "endmark" characters, then return that packet (including the marks)
 * and delete everything up to the end of that packet from the input buffer.
 * Otherwise return NULL.
 * Everything up to the first startmark is discarded.
  */
Octstr *conn_read_packet(Connection *conn, int startmark, int endmark);

#ifdef HAVE_LIBSSL

#include <openssl/x509.h>

/* Returns the SSL peer certificate for the given Connection or NULL
 * if none. 
 */
X509 *get_peer_certificate(Connection *conn);

/* These must be called if SSL is used. Currently http.c calls 
 * conn_init_ssl from http_init and conn_shutdown_ssl from http_shutdown. 
 */
void conn_init_ssl(void);
void conn_shutdown_ssl(void);

/* Specifies a global PEM-encoded certificate and a private key file 
 * to be used with SSL connections. conn_init_ssl() must be called 
 * first. This checks that the private key matches with the certificate
 * and will panic if it doesn't.
 */
void use_global_certkey_file(Octstr *certkeyfile);
#endif /* HAVE_LIBSSL */
