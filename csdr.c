/*
 * CSD Router thread for bearer box (WAP/SMS Gateway)
 *
 * Kalle Marjola for Wapit ltd.
 */

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>


#include "config.h"
#include "bb_msg.h"
#include "csdr.h"


CSDRouter *csdr_open(ConfigGroup *grp)
{
    return NULL;
}

int csdr_close(CSDRouter *csdr)
{
    if (csdr == NULL)
	return 0;
    free(csdr);
    return 0;
}

RQueueItem *csdr_get_message(CSDRouter *csdr)
{
    return NULL;
}

int csdr_send_message(CSDRouter *csdr, RQueueItem *msg)
{
    return 0;
}
