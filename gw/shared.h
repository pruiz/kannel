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


#endif
