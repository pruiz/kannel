/*
 * This is general header file to include all gwlib subparts.
 * As they are usually all needed, this eases the need of
 * lots of includes in modules
 *
 * Kalle Marjola for WapIT Ltd 1999
 */
#ifndef _GWLIB_H
#define _GWLIB_H

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#include "config.h"

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include "gwstr.h"
#include "gwmem.h"
#include "utils.h"
#include "log.h"
#include "thread.h"
#include "socket.h"
#include "conffile.h"
#include "http.h"
#include "http2.h"
#include "octstr.h"
#include "list.h"
#include "gwassert.h"
#include "counter.h"


#endif
