/*
 * smsc_fake.c - implement interface to fakesmsc.c
 *
 * Lars Wirzenius for WapIT Ltd.
 */


#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "gwlib/gwlib.h"
#include "smsc.h"
#include "smsc_p.h"


/* do the handshake baby */
static int fake_open_connection(SMSCenter *smsc)
{
    smsc->socket = tcpip_connect_to_server(smsc->hostname, smsc->port);
    if (smsc->socket == -1)
        return -1;

    return 0;
}


SMSCenter *fake_open(char *hostname, int port)
{
    SMSCenter *smsc;

    smsc = smscenter_construct();
    if (smsc == NULL)
        goto error;

    smsc->type = SMSC_TYPE_FAKE;
    smsc->port = port;
    smsc->hostname = gw_strdup(hostname);
    if (fake_open_connection(smsc) < 0)
        goto error;

    sprintf(smsc->name, "FAKE:%s:%d", smsc->hostname, smsc->port);

    info(0, "Fake open successfully done");

    return smsc;

error:
    smscenter_destruct(smsc);
    return NULL;
}


int fake_reopen(SMSCenter *smsc)
{
    if (smsc->socket == -1) {
        info(0, "trying to close already closed fake, ignoring");
    } else if (close(smsc->socket) == -1) {
        error(errno, "Closing socket to server `%s' port `%d' failed.",
              smsc->hostname, smsc->port);
        return -1;
    }
    return fake_open_connection(smsc);
}


int fake_close(SMSCenter *smsc)
{
    if (smsc->socket == -1) {
        info(0, "trying to close already closed fake, ignoring");
        return 0;
    }
    if (close(smsc->socket) == -1) {
        error(errno, "Closing socket to server `%s' port `%d' failed.",
              smsc->hostname, smsc->port);
        return -1;
    }
    smscenter_destruct(smsc);
    return 0;
}


int fake_pending_smsmessage(SMSCenter *smsc)
{
    int ret;

    if (memchr(smsc->buffer, '\n', smsc->buflen) != NULL)
        return 1;

    ret = smscenter_read_into_buffer(smsc);
    if (ret == -1) {
        error(0, "fake_pending_smsmessage: read_into_buffer failed");
        return -1;
    }
    if (ret == 0)
        return 1;  /* yes, 1; next call to receive will signal EOF */

    if (memchr(smsc->buffer, '\n', smsc->buflen) != NULL)
        return 1;
    return 0;
}


int fake_submit_msg(SMSCenter *smsc, Msg *msg)
{
    if (msg_type(msg) == sms) {
        if (octstr_write_to_socket(smsc->socket, msg->sms.sender) == -1 ||
            write_to_socket(smsc->socket, " ") == -1 ||
            octstr_write_to_socket(smsc->socket, msg->sms.receiver) == -1 ||
            write_to_socket(smsc->socket, " ") == -1 ||
            octstr_write_to_socket(smsc->socket, msg->sms.msgdata) == -1 ||
            write_to_socket(smsc->socket, "\n") == -1)
            return -1;
    }
    return 0;
}

int fake_receive_msg(SMSCenter *smsc, Msg **msg)
{
    char *newline, *p, *sender, *receiver, *text;
    int ret;

    for (; ; ) {
        newline = memchr(smsc->buffer, '\n', smsc->buflen);
        if (newline != NULL)
            break;
        ret = smscenter_read_into_buffer(smsc);
        if (ret <= 0)
            return -1;
    }

    *newline = '\0';
    if (newline > smsc->buffer && newline[ -1] == '\r')
        newline[ -1] = '\0';

    sender = smsc->buffer;
    p = strchr(sender, ' ');
    if (p == NULL)
        receiver = text = "";
    else {
        *p++ = '\0';
        receiver = p;
        p = strchr(receiver, ' ');
        if (p == NULL)
            text = "";
        else {
            *p++ = '\0';
            text = p;
        }
    }

    *msg = msg_create(sms);
    if (*msg == NULL) return -1;

    (*msg)->sms.sender = octstr_create(sender);
    (*msg)->sms.receiver = octstr_create(receiver);
    (*msg)->sms.msgdata = octstr_create(text);

    smscenter_remove_from_buffer(smsc, newline - smsc->buffer + 1);
    return 1;
}
