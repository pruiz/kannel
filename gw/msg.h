/*
 * msg.h - declarations for message manipulation
 * 
 * This file declares the Msg data type and the functions to manipulate it.
 * 
 * Lars Wirzenius <liw@wapit.com> 
 */


#ifndef MSG_H
#define MSG_H

#include "octstr.h"

typedef long int32;

enum msg_type {
	#define MSG(type, stmt) type,
	#include "msg-decl.h"
};

typedef struct {
	enum msg_type type;

	#define INTEGER(name) int32 name
	#define OCTSTR(name) Octstr *name
	#define MSG(type, stmt) struct type stmt type;
	#include "msg-decl.h"
} Msg;


/*
 * Create a new, empty Msg object. Return NULL for failure, otherwise a
 * pointer to the object.
 */
Msg *msg_create(enum msg_type type);


/*
 * Return type of the message
 */
enum msg_type msg_type(Msg *msg);


/*
 * Destroy an Msg object. All fields are also destroyed.
 */
void msg_destroy(Msg *msg);


/*
 * For debugging: Output with `debug' (in wapitlib.h) the contents of
 * an Msg object.
 */
void msg_dump(Msg *msg);


/*
 * Pack an Msg into an Octstr. Return NULL for failure, otherwise a pointer
 * to the Octstr.
 */
Octstr *msg_pack(Msg *msg);


/*
 * Unpack an Msg from an Octstr. Return NULL for failure, otherwise a pointer
 * to the Msg.
 */
Msg *msg_unpack(Octstr *os);

#endif
