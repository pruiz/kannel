
/*
 * wtls_machine-decl.h - macro call for generating WTLS server state machine. See 
 * the architecture document for guidance how to use and update it.
 *
 * By Nick Clarey <nclarey@3glab.com> (c) 3GLab Ltd.
 *
 * The WTLSMachine data structure preserves the state of the existing WTLS
 * transaction. The fields which are included;
 *
 * Machine identification: address four-tuple
 * Connection End : Server (1) or Client (2) This is always "server"
 *                                 (at least at the moment)
 * Bulk Cipher Algorithm : The algorithm to be used for stream or block encryption
 * Key Size : ?????
 * IV Size : The base IV used to calculate a record level IV for block ciphers running
 *                    in CBC mode for records sent by the server
 * MAC Algorithm : The algorithm identifier used for message authentication.
 * Master Secret : A shared secret between the two peers
 * Client Random : A random value supplied by the client
 * Server Random : A random value supplied by the server
 * Sequence Number Mode : Off (0), Implicit (1) or Explicit (2)
 * Key Refresh rate : New keys for MAC secret, IV and Encryption are calculated
 *                                   every "n", where n = 2^(RefreshRate)
 * Compression Method : The algorithm to compress data prior to encryption
 *
 */

#if !defined(MACHINE) 
#error "wtls_machine-decl.h: Macro MACHINE is missing."
#elif !defined(ENUM) 
#error "wtls_machine-decl.h: Macro ENUM is missing."
#elif !defined(ADDRTUPLE) 
#error "wtls_machine-decl.h: Macro ADDRTUPLE is missing."
#elif !defined(INTEGER) 
#error "wtls_machine-decl.h: Macro INTEGER is missing."
#elif !defined(OCTSTR) 
#error "wtls_machine-decl.h: Macro OCTSTR is missing."
#elif !defined(PDULIST) 
#error "wtls_machine-decl.h: Macro PDULIST is missing."
#endif

/* Need to add server sent and client received packets for sequence numbering */
/* Last received packet maybe needs to be hashed according to Alert message in
   case we need to send an alert. */

MACHINE(ENUM(state)
        ADDRTUPLE(addr_tuple) /* The source address/port and dest address/port */
        INTEGER(bulk_cipher_algorithm) /* Bulk Cipher Algorithm identifier */
        INTEGER(cipher_type)                    /* Cipher type */
        INTEGER(mac_algorithm)                  /* MAC Algorithm identifier */
        OCTSTR(client_random)                   /* The client's random number */
        OCTSTR(server_random)                   /* The server's random number */
        OCTSTR(master_secret)                   /* The master secret */
        INTEGER (key_size)              /* The "key size". Which key size, I have no idea */
        INTEGER (key_material_length)   /* and what might that be ? */
        INTEGER (is_exportable)                 /* exportable flag (?) */
        INTEGER(iv_size)                /* The IV size */
        INTEGER(mac_size)                               /* MAC size */
        INTEGER(mac_key_size)                   /* MAC key size */
        INTEGER(sequence_number_mode)   /* The sequence number mode */
        INTEGER(key_refresh)                    /* How often we should refresh our keys */
        OCTSTR(compression_method)              /* The compression algorithm */
        INTEGER(encrypted)				/* set if packets are encrypted */
		
        OCTSTR(client_write_MAC_secret) /*  */
        OCTSTR(client_write_enc_key)    /*  */
        OCTSTR(client_write_IV)                 /*  */
        OCTSTR(server_write_MAC_secret) /*  */
        OCTSTR(server_write_enc_key)    /*  */
        OCTSTR(server_write_IV)                 /*  */
        INTEGER(client_seq_num)                 /* incremented for each client msg */
        INTEGER(server_seq_num)                 /* incremented for each server msg */
                                
        OCTSTR(last_packet_checksum) /* The last received packet checksum */
        PDULIST(last_received_packet) /* The last received packet checksum */
        OCTSTR(handshake_data) /* All the handshake payloads, received or sent,
                                  concatenated in order */
        OCTSTR(packet_to_send) /* A packet we're preparing to send */
       )

#undef MACHINE
#undef ENUM
#undef ADDRTUPLE
#undef INTEGER
#undef OCTSTR
#undef PDULIST
