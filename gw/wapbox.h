/*
 * wapbox.h
 */


#ifndef WAPBOX_H
#define WAPBOX_H

#include "msg.h"

/* XXX these should be renamed and made into a proper WDP layer */
void init_queue(void);
void put_msg_in_queue(Msg *msg);
Msg *remove_msg_from_queue(void);

#endif
