/*
 * gwlib/getopt.h - define a prototype for the getopt function
 *
 * Some systems have a <getopt.h> which defines the getopt stuff. On
 * those systems, we include that. On other systems, we provide our own
 * definitions.
 */

#ifndef GWLIB_GETOPT_H
#define GWLIB_GETOPT_H

#if HAVE_GETOPT_H
    #include <getopt.h>
#elif HAVE_GETOPT_IN_STDIO_H
    #include <stdio.h>
#elif HAVE_GETOPT_IN_UNISTD_H
    #include <unistd.h>
#else
    int getopt(int argc, char **argv, char *opts);
    extern int opterr;
    extern int optind;
    extern int optopt;
    extern char *optarg;
#endif

#endif
