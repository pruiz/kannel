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

void report_versions(const char *boxname);

#endif
