/*
 * shared.h - utility functions shared by Kannel boxes
 *
 * The functions declared here are not part of any box in particular, but
 * are quite specific to Kannel, so they are not suitable for gwlib, either.
 *
 * Lars Wirzenius
 */

#ifndef SHARED_H
#define SHARED_H


#include "msg.h"


/*
 * Program status. Set this to shutting_down to make read_from_bearerbox
 * return even if the bearerbox hasn't closed the connection yet.
 */
extern enum program_status {
    starting_up,
    running,
    shutting_down
} program_status;


/*
 * Return an octet string with information about Kannel version,
 * operating system, and libxml version. The caller must take care to
 * destroy the string when done.
 */
Octstr *version_report_string(const char *boxname);


/*
 * Output the information returned by version_report_string to the log
 * files.
 */
void report_versions(const char *boxname);


/*
 * Open a connection to the bearerbox.
 */
void connect_to_bearerbox(Octstr *host, int port);


/*
 * Close connection to the bearerbox.
 */
void close_connection_to_bearerbox(void);


/*
 * Receive Msg from bearerbox. Return NULL if connection broke.
 */
Msg *read_from_bearerbox(void);


/*
 * Send an Msg to the bearerbox, and destroy the Msg.
 */
void write_to_bearerbox(Msg *msg);

/*
 * Validates an OSI date.
 */
Octstr *parse_date(Octstr *date);

#endif
