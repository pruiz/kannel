/*
 * wapbox.h
 */


#ifndef WAPBOX_H
#define WAPBOX_H

#include "msg.h"

void put_msg_in_queue(Msg *msg);
Msg *remove_msg_from_queue(void);

#endif
