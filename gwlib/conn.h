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

/* Open a TCP/IP connection to the given host and port.  Return the
 * new Connection.  If the connection can not be made, return NULL
 * and log the problem. */
Connection *conn_open_tcp(Octstr *host, int port);

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

/* Try to write data in chunks of this size or more.  Set it to 0 to
 * get an unbuffered connection.  See the discussion on output buffering
 * at the top of this file for more information. */
void conn_set_output_buffering(Connection *conn, unsigned int size);

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
 * not block. */

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
