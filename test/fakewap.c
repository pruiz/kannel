/*
 * fakewap.c - simulate wap client talking directly to wap gw.
 *
 * This module can be built also in Windows. There you should
 * add ".\wininc" to your include directories.
 *
 * The protocol:
 *
 *    
 *    A)    Fakewap -> Gateway
 *
 *        WTP: Invoke PDU
 *        WSP: Connect PDU
 *    
 *    B)    Gateway -> Fakewap
 *    
 *        WTP: Result PDU
 *        WSP: ConnectReply PDU
 *    
 *    C)    Fakewap -> Gateway
 *    
 *        WTP: Ack PDU
 *    
 *    D)    Fakewap -> Gateway
 *    
 *        WTP: Invoke PDU
 *        WSP: Get PDU (data: URL)
 *        
 *    E)    Gateway -> Fakewap
 *    
 *        WTP: Result PDU (data: WML page)
 *        WSP: Reply PDU
 *        
 *    F)    Fakewap -> Gateway
 *    
 *        WTP: Ack PDU
 *        
 *    G)    Fakewap -> Gateway
 *    
 *        WTP: Invoke PDU
 *        WSP: Disconnect PDU
 *    
 *
 *    Packets A-C open a WAP session. Packets D-F fetch a WML page.
 *    Packet G closes the session.
 *
 * The test terminates when all packets have been sent.
 *
 *
 * Antti Saarenheimo for WapIT Ltd.
 */
 
#define MAX_SEND (0)

static char usage[] = 
"Usage: fakewap [-v] <our port> <host> <port> <max> <interval> <url1> <url2> ...\n\
\n\
where [-v] enables optional verbose mode, \n\
<out port> is the port used in this machine, \n\
host> and <port> is the host and port to connect to, \n\
<max> is the maximum number of messages to send (0 means infinitum), \n\
<interval>, is the interval in seconds (floating point allowed), \n\
between automatically generated messages,\n\
<url> is the url to be requested. If there are several urls, they are \n\
sent in random order.\n\
\n\
For example: fakewap -v 10008 my_host 9201 10 0 http://www.wapit.com/~liw/hello.wml\n";

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <netinet/tcp.h>

#include <sys/param.h>

#ifdef _WINSOCKAPI_
#undef DEBUG
#undef ERROR
#undef PANIC
#undef WARNING
#undef INFO
#else
typedef int SOCKET;
#endif


#include "gwlib.h"

#define GET_WTP_PDU_TYPE(hdr)  (hdr[0] >> 3)

#define WTP_PDU_INVOKE  1
#define WTP_PDU_RESULT  2
#define WTP_PDU_ACK     3
#define WTP_PDU_ABORT   4

int verbose = 0;

unsigned char WSP_Connect[] = {0x0E, 0x00, 0x00, 0x02, 0x01, 0x10, 0x00, 0x00 };
unsigned char WSP_ConnectReply[] = {0x16, 0x80, 0x00, 0x02 };
unsigned char WTP_Ack[] =          {0x18, 0x00, 0x00 };
unsigned char WTP_Abort[] =        {0x20, 0x00, 0x00, 0x00 };
unsigned char WSP_Get[] =          {0x0E, 0x00, 0x00, 0x02, 0x40 };
unsigned char WSP_Reply[] =        {0x16, 0x80, 0x00, 0x04, 0x20, 0x01, 0x94 };
unsigned char WSP_Disconnect[] =   {0x0E, 0x00, 0x00, 0x00, 0x05 };

/*
**  In this case it does not matter what is the byte order
*/
#define SET_GTR( hdr ) hdr[0] |= 0x04
#define SET_TID( hdr, tid) hdr[1] |= (0x7f & ((tid) >> 8)); hdr[2] = (char)(tid)
#define GET_TID( hdr ) (((hdr[1] & 0x7f) << 8) + hdr[2])
#define CONSTRUCT_EXPECTED_REPLY_HDR( dest, template, tid ) \
    if (sizeof(dest) < sizeof(template)) panic(0,"buffer overflow.");\
    memcpy( dest, template, sizeof(template));\
    SET_TID( dest, tid )

#ifndef min
#define min(a,b) (a < b ? a : b)
#endif


/* Choose a random message from a table of messages. */
static char *choose_message(char **urls, int num_urls) {
    /* the following doesn't give an even distribution, but who cares */
    return urls[rand() % num_urls];
}
/* returns TID */
static unsigned short get_tid() {
    static  unsigned tid = 0;
    return tid++ & 0x7fff;
}    

/* 
**  if -v option has been defined, function prints the trace message and  
**  the first bytes in the message header 
*/
void print_msg( const char * trace, unsigned char * msg, int msg_len ) {
    int i;
    if (verbose) {
        printf( "%s (len %d): ", trace, msg_len );
        for (i = 0; i < msg_len && i < 16; i++) printf( "%02X ", msg[i] );
        printf( "\n");
    }
}

/*
**  Function stores WAP/WSP variable length integer to buffer and returns actual len
*/
int StoreVarInt( unsigned char *buf, unsigned long varInt )
{
    int i, len = 1, non_zero_bits = 7;

    /*
    **    Skip all zero high bits
    */
    while ((varInt >> non_zero_bits) != 0) {
        non_zero_bits += 7;
        len++;
    }
    /*
    **    Read the higest bits first.
    */
    for (i = 0; i < len; i++)
    {
        buf[i] = (unsigned char)(varInt >> (non_zero_bits-7)) & 0x7f;
        non_zero_bits -= 7;
    }
    buf[len-1] &= 0x7f;
    return len;
}


/*
**  Function length of WAP/WSP variable length integer in the buffer
*/
int ReadVarIntLen( const unsigned char *buf )
{
    int    len = 1;

    while (buf[len-1] & 0x80) len++;
    return len;
}


/*
**  Function sends message to WAP GW
*/  
int
WapMsgSend( SOCKET fd, const unsigned char * hdr, int hdr_len, 
            const unsigned short * tid, unsigned char * data, int data_len )
{
    int ret;
    unsigned char msg[1024*64];
    int msg_len = 0;

    if (hdr != NULL) {
        memcpy( msg, hdr, hdr_len );
        msg_len = hdr_len;
    }
    if (tid != NULL) {
        SET_TID( msg, *tid );
        if (GET_WTP_PDU_TYPE(msg) == WTP_PDU_INVOKE)
        {
            msg[3] |= 0x10; /* request ack every time */
            if (*tid == 0) msg[3] |= 0x20; /* set newtid flag */
        }
    }
    if (data != NULL) {
        memcpy( msg+msg_len, data, data_len );
        msg_len += data_len;
    }
    ret = send( fd, msg, msg_len, 0 );
    if (ret == -1) {
        error(errno, "Sending to socket failed");
        return -1;
    }
    else if (verbose) {
        print_msg( "Sent wap message", msg, msg_len );
    }
    return ret;
}              

/*
**  Function receives a wap wtl/wsp message. If the headers has been 
**  given, it must match with the received message.
**  Return value:
**      >  0 => length of received data
**      == 0 => got acknowlengement or abort but not the expected data
**      < 0  => error, 
*/
int
WapMsgRecv( SOCKET fd, const char * hdr, int hdr_len, unsigned short tid, 
            unsigned char * data, int data_len, int timeout )
{
    int ret;
    unsigned char msg[1024*64];
    int msg_len = 0;
    struct timeval  tv;
    fd_set readset;
    int    fResponderIsDead = 1;  /* assume this by default */

    /*
    **  Loop until we get the expected response or do timeout
    */
    for (;;)
    {
        if (timeout != 0)
        {
            tv.tv_sec = timeout;
            tv.tv_usec = 0;

            FD_ZERO(&readset);
            FD_SET( fd, &readset);

            ret = select(fd+1, &readset, NULL, NULL, &tv);
            if (ret == -1 || FD_ISSET(fd,&readset)==0) {
                info(0, "Timeout while receiving from socket.\n");
                return fResponderIsDead ? -1 : 0;  /* continue if we got ack? */
            }
        }
        ret = recv( fd, msg, sizeof(msg), 0 );
    
        if (ret == -1) {
            error(errno, "recv() from socket failed");
            return -1;
        }
        msg_len = ret;
        
        if (hdr != NULL) {
            /* 
            **  Ignore extra header bits, WAP GWs return different values 
            */
            if (msg_len >= hdr_len && 
                GET_WTP_PDU_TYPE(msg) == GET_WTP_PDU_TYPE(hdr) &&
                (hdr_len <= 3 || !memcmp( msg+3, hdr+3, hdr_len-3 ))) {
                break;
            }
            /*
            **  Handle TID test, the answer is: Yes, we have an outstanding 
            **  transation with this tid
            */
            else if (GET_WTP_PDU_TYPE(msg) == WTP_PDU_ACK && 
                     GET_TID(msg) == tid) {
                print_msg( "Received tid verification", msg, msg_len );
                WapMsgSend( fd, WTP_Ack, sizeof(WTP_Ack), &tid, NULL, 0 );
            } 
            else if (GET_WTP_PDU_TYPE(msg) == WTP_PDU_ABORT) {
                print_msg( "Received WTP Abort", msg, msg_len );
            }
            else {
                print_msg( "Received unexpected message", msg, msg_len );
            }
            fResponderIsDead = 0;
        }
        else {
            hdr_len = 0;
            break;
        }
    }
    print_msg( "Received wap message", msg, msg_len );

    if (data != NULL && msg_len > hdr_len) {
        data_len = min( data_len, msg_len - hdr_len );
        memcpy( data, msg+hdr_len, data_len);
    }
    else  data_len = 0;
    return data_len;
}


/*
**  Function initializes and binds datagram socket.
*/

SOCKET connect_to_server_with_port(char *hostname, unsigned short port, 
                                         unsigned short our_port) 
{
    struct sockaddr_in addr;
    struct sockaddr_in o_addr;
    struct hostent * hostinfo;
    struct linger dontlinger;
    SOCKET s;
    
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == -1)
        goto error;

    dontlinger.l_onoff = 1;
    dontlinger.l_linger = 0;
#ifdef BSD
    setsockopt(s, SOL_TCP, SO_LINGER, &dontlinger, sizeof(dontlinger));
#else
{ 
    /* XXX no error trapping */
    struct protoent *p = getprotobyname("tcp");
    setsockopt(s, p->p_proto, SO_LINGER, (void *)&dontlinger, sizeof(dontlinger));
}
#endif
    hostinfo = gethostbyname(hostname);
    if (hostinfo == NULL)
        goto error;
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr = *(struct in_addr *) hostinfo->h_addr;
    addr.sin_addr = *(struct in_addr *) hostinfo->h_addr;

    if (our_port > 0) {
        o_addr.sin_family = AF_INET;
        o_addr.sin_port = htons(our_port);
        o_addr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(s, (const void *)&o_addr, sizeof(o_addr)) == -1) {
            error(0, "bind to local port %d failed", our_port);
            goto error;
        }
    }
    if (connect(s, (struct sockaddr *) &addr, sizeof(addr)) == -1)
        goto error;
    
    return s;

error:
    error(errno, "error connecting to server `%s' at port `%d'",
        hostname, port);
    if (s >= 0)
        close(s);
    return -1;
}

#ifdef _WINSOCKAPI_
/*
**  We cannot use wapit library in Windows
*/
void panic(int level, const char * args, ...) 
{
    va_list ap;
    
    va_start(ap, args);
    vprintf( args, ap );
    va_end( ap );
    exit(level);
}

void error(int level, const char * args, ...) 
{
    va_list ap;
    
    printf("Last socket error=%d\n", WSAGetLastError());
    va_start(ap, args);
    vprintf( args, ap );
    va_end( ap );
}

void info(int level, const char * args, ...) 
{
    va_list ap;
    
    va_start(ap, args);
    vprintf( args, ap );
    va_end( ap );
}
#endif


/* The main program. */
int main(int argc, char **argv) {
    int ret;
    unsigned short port, our_port;
    SOCKET fd;
    struct timeval now;
    char **urls;
    int num_urls, url_len, url_off;
    double interval, nowsec, lastsec;
    int num_sent;
    struct timezone tz;
    int max_send;
    time_t first_sent_at, last_sent_at;
    time_t start_time, end_time;
    double delta;
    char * hostname;
    char * url;
    unsigned char  sid[20];
    int            sid_len;
    unsigned char  buf[64*1024];
    unsigned char reply_hdr[32];
    long timeout = 10;  /* wap gw is broken if no input */
    double besttime = -1,  worsttime = 0;
    unsigned short tid;
    int connection_retries = 0;

    if (argc > 2 && argv[1][0] == '-' && argv[1][1] == 'v') {
        verbose = 1;
        argv++;
        argc--;
    }
    if (argc < 6)
        panic(0, "%s", usage);

#ifdef _WINSOCKAPI_
    {
        WORD wVersionRequested = MAKEWORD( 2, 0 );
        WSADATA wsaData;
 
        ret = WSAStartup( wVersionRequested, &wsaData );
        if ( ret != 0 ) {
            panic( 0, "Windows socket api version is not supported, v2,0 is required\n");
            /* Tell the user that we could not find a usable */
            /* WinSock DLL.                                  */
            return 0;
        }
    }
#endif
    time(&start_time);

    our_port = (unsigned short)atoi(argv[1]);
    hostname = argv[2];
    port = (unsigned short)atoi(argv[3]);
    max_send = atoi(argv[4]);
    interval = atof(argv[5]);

    urls = argv + 6;
    num_urls = argc - 6;
    srand((unsigned int) time(NULL));

    info(0, "fakewap starting...\n");
    fd = connect_to_server_with_port( hostname, port, our_port);
    if (fd == -1)
        panic(0, "couldn't connect host ");

    num_sent = 0;

    first_sent_at = 0;
    last_sent_at = 0;

    lastsec = 0;

    /*
    **  Loop until all URLs have been requested
    */
    while (max_send == MAX_SEND || num_sent < max_send) {
                
        /*
        **  Connect, save sid from reply and finally ack the reply
        */
        tid = get_tid();
        ret = WapMsgSend( fd, WSP_Connect, sizeof(WSP_Connect), 
                          &tid, NULL, 0 );

        if (ret == -1) panic(0, "Send WSP_Connect failed");

        CONSTRUCT_EXPECTED_REPLY_HDR( reply_hdr, WSP_ConnectReply, tid );
        ret = WapMsgRecv( fd, reply_hdr, sizeof(WSP_ConnectReply), 
                          tid, buf, sizeof(buf), timeout );
        
        if (ret == -1) panic(0, "Receive WSP_ConnectReply failed");
        
        if (ret > 2)
        {
            sid_len = ReadVarIntLen(buf);
            memcpy( sid, buf, sid_len);
        }
        /*
        **  Send abort and continue if we get an unexpected reply
        */
        if (ret == 0)  {
            if (connection_retries++ > 3) {
                panic(0, "Cannot connect WAP GW!");
            }
            WapMsgSend( fd, WTP_Abort, sizeof(WTP_Abort), &tid, NULL, 0 );
            continue;
        } 
        else {
            connection_retries = 0;
        }
        ret = WapMsgSend( fd, WTP_Ack, sizeof(WTP_Ack), &tid, NULL, 0 );

        if (ret == -1) panic(0, "Send WTP_Ack failed");

        /*
        **  Request WML page with the given URL
        */
        tid = get_tid();
        url = choose_message(urls, num_urls);
        url_len = strlen(url);
        url_off = StoreVarInt( buf, url_len );
        memcpy( buf+url_off, url, url_len );
        ret = WapMsgSend( fd, WSP_Get, sizeof(WSP_Get), &tid, buf, url_len+3 );
        if (ret == -1) break;

        CONSTRUCT_EXPECTED_REPLY_HDR( reply_hdr, WSP_Reply, tid );
        ret = WapMsgRecv( fd, reply_hdr, sizeof(WSP_Reply), 
                          tid, buf, sizeof(buf), timeout );
        if (ret == -1) break;

        ret = WapMsgSend( fd, WTP_Ack, sizeof(WTP_Ack), &tid, NULL, 0 );

        if (ret == -1) break;

        /*
        **  Finally disconnect with the sid returned by connect reply
        */
        ret = WapMsgSend( fd, WSP_Disconnect, sizeof(WSP_Disconnect), 
                          &tid, sid, sid_len );

        if (ret == -1) break;

        if (num_sent < max_send) {
            ++num_sent;
            gettimeofday(&now, &tz);
            nowsec = (double) now.tv_sec + now.tv_usec / 1e6;
            
            if (lastsec != 0)
            {
                double tmp = nowsec - lastsec;
                if (tmp < besttime) besttime = tmp;
                if (tmp > worsttime) worsttime = tmp;
               
                tmp *= 1e6;
                if (tmp < (double)interval)
                {
                    usleep( (long)((double)interval - tmp) );
                }
            }
            if (interval > 0.01)
                info(0, "fakewap: sent message %d", num_sent);
                        
            lastsec = nowsec;
            time(&last_sent_at);
            if (first_sent_at == 0)
               first_sent_at = last_sent_at;
        }
    }
    close(fd);

    time(&end_time);

    info( 0, "fakewap: %d transactions", num_sent );
    info( 0, "fakewap: total running time %.1f seconds", 
          difftime(end_time, start_time));

    delta = difftime(last_sent_at, first_sent_at);
    info(0, "fakewap: from first to last transaction %.1f s, "
            "%.1f urls/s", delta, num_sent / delta);
    info(0, "fakewap: the best and worst transaction time %.1f s, "
            "%.1f urls/s", besttime, worsttime );
    info(0, "fakewap: terminating");
    return 0;
}

