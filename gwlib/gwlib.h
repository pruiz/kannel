/*
 * gwlib.h - public interface to gwlib
 *
 * This is general header file to include all gwlib subparts.
 * As they are usually all needed, this eases the need of
 * lots of includes in modules
 *
 * Kalle Marjola for WapIT Ltd 1999
 */

#ifndef GWLIB_H
#define GWLIB_H

#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "config.h"

#include "gw-getopt.h"
#include "gwpoll.h"

#include "utils.h"
#include "log.h"
#include "thread.h"
#include "gwthread.h"
#include "gwmem.h"
#include "socket.h"
#include "cfg.h"
#include "date.h"
#include "http.h"
#include "octstr.h"
#include "list.h"
#include "fdset.h"
#include "gwassert.h"
#include "counter.h"
#include "charset.h"
#include "conn.h"
#include "ssl.h"
#include "parse.h"
#include "protected.h"
#include "accesslog.h"
#include "dict.h"
#include "semaphore.h"
#include "xmlrpc.h"

void gwlib_assert_init(void);
void gwlib_init(void);
void gwlib_shutdown(void);

#ifdef NO_GWASSERT
#define gwlib_assert_init() ((void) 0)
#endif

#endif
