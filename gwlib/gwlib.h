/*
 * gwlib.h - public interface to gwlib
 *
 * This is general header file to include all gwlib subparts.
 * As they are usually all needed, this eases the need of
 * lots of includes in modules
 *
 * Kalle Marjola for WapIT Ltd 1999
 */
#ifndef _GWLIB_H
#define _GWLIB_H

#include <stdlib.h>
#include <stddef.h>

#include "config.h"

#include "gw-getopt.h"
#include "gwpoll.h"

#include "gwstr.h"
#include "utils.h"
#include "log.h"
#include "thread.h"
#include "gwthread.h"
#include "gwmem.h"
#include "socket.h"
#include "conffile.h"
#include "date.h"
#include "http.h"
#include "octstr.h"
#include "list.h"
#include "fdset.h"
#include "gwassert.h"
#include "counter.h"
#include "charset.h"
#include "conn.h"
#include "parse.h"
#include "protected.h"
#include "accesslog.h"
#include "dict.h"

void gwlib_assert_init(void);
void gwlib_init(void);
void gwlib_shutdown(void);

#ifdef NDEBUG
#define gwlib_assert_init() ((void) 0)
#endif

#endif
