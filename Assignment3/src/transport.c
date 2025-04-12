/*
 * transport.c 
 *
 * This file implements the STCP layer that sits between the
 * mysocket and network layers. It handles the connection establishment,
 * data transfer, and connection termination processes.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <sys/time.h>
#include "mysock.h"
#include "stcp_api.h"
#include "transport.h"
#include <arpa/inet.h>

enum { 
    CSTATE_CLOSED,
    CSTATE_LISTEN,
    CSTATE_SYN_SENT,
    CSTATE_SYN_RCVD,
    CSTATE_ESTABLISHED,
    CSTATE_FIN_WAIT_1,
    CSTATE_FIN_WAIT_2,
    CSTATE_CLOSING,
    CSTATE_TIME_WAIT,
    CSTATE_CLOSE_WAIT,
    CSTATE_LAST_ACK
};

/* Receiver window size */
#define RECEIVER_WINDOW_SIZE 3072

/* this structure is global to a mysocket descriptor */
typedef struct
{
    bool_t done;                  /* TRUE once connection is closed */
    int connection_state;         /* state of the connection */
    
    tcp_seq initial_sequence_num; /* Initial sequence number */
    tcp_seq sequence_num;         /* Current sequence number for sending */
    tcp_seq ack_num;              /* Next sequence number expected from peer */
    
    tcp_seq fin_seq;              /* Sequence number of FIN packet */
    bool_t fin_sent;              /* Whether we've sent a FIN */
    bool_t fin_received;          /* Whether we've received a FIN */
    bool_t fin_acked;             /* Whether our FIN has been ACKed */
    
    uint16_t send_window;         /* Peer's receive window size */
    uint16_t recv_window;         /* Our receive window size */
} context_t;

static void generate_initial_seq_num(context_t *ctx);
static void control_loop(mysocket_t sd, context_t *ctx);

/* Create and send a packet */
static ssize_t send_packet(mysocket_t sd, context_t *ctx, const void *data, 
                          size_t data_len, uint8_t flags);

/* Initialize the transport layer and handle connection setup */
void transport_init(mysocket_t sd, bool_t is_active)
{
    context_t *ctx;
    STCPHeader *header;
    char buf[sizeof(STCPHeader)];
    ssize_t recv_len;

    ctx = (context_t *) calloc(1, sizeof(context_t));
    assert(ctx);

    /* Set the initial state */
    ctx->connection_state = CSTATE_CLOSED;
    ctx->done = FALSE;
    ctx->fin_sent = FALSE;
    ctx->fin_received = FALSE;
    ctx->fin_acked = FALSE;
    
    /* Initialize sequence number */
    generate_initial_seq_num(ctx);
    ctx->sequence_num = ctx->initial_sequence_num;
    
    /* Initialize windows */
    ctx->recv_window = RECEIVER_WINDOW_SIZE;
    ctx->send_window = RECEIVER_WINDOW_SIZE; /* Default, will be updated */
    
    /* Store context for future API calls */
    stcp_set_context(sd, ctx);

    if (is_active) {
        /* Active end: Send SYN */
        ctx->connection_state = CSTATE_SYN_SENT;
        dprintf("Active end: Sending SYN, seq=%u\n", ctx->sequence_num);
        send_packet(sd, ctx, NULL, 0, TH_SYN);
        ctx->sequence_num++; /* SYN consumes one byte in sequence space */

        /* Wait for SYN-ACK */
        unsigned int event = stcp_wait_for_event(sd, NETWORK_DATA, NULL);
        if (event & NETWORK_DATA) {
            recv_len = stcp_network_recv(sd, buf, sizeof(buf));
            if (recv_len < (ssize_t)sizeof(STCPHeader)) {
                errno = ECONNREFUSED;
                stcp_unblock_application(sd);
                return;
            }
            
            header = (STCPHeader *) buf;
            
            /* Check if we got a SYN-ACK */
            if ((header->th_flags & (TH_SYN|TH_ACK)) == (TH_SYN|TH_ACK) &&
                ntohl(header->th_ack) == ctx->sequence_num) {
                
                dprintf("Active end: Received SYN-ACK, seq=%u, ack=%u\n", 
                        ntohl(header->th_seq), ntohl(header->th_ack));
                
                /* Initialize ACK number to peer's sequence number + 1 */
                ctx->ack_num = ntohl(header->th_seq) + 1;
                
                /* Update receive window based on peer's advertised window */
                ctx->send_window = ntohs(header->th_win);
                
                /* Send ACK */
                dprintf("Active end: Sending ACK, seq=%u, ack=%u\n", 
                        ctx->sequence_num, ctx->ack_num);
                send_packet(sd, ctx, NULL, 0, TH_ACK);
                
                /* Connection established */
                ctx->connection_state = CSTATE_ESTABLISHED;
            } else {
                errno = ECONNREFUSED;
                stcp_unblock_application(sd);
                return;
            }
        } else {
            errno = ECONNREFUSED;
            stcp_unblock_application(sd);
            return;
        }
    } else {
        /* Passive end: Wait for SYN */
        unsigned int event = stcp_wait_for_event(sd, NETWORK_DATA, NULL);
        if (event & NETWORK_DATA) {
            recv_len = stcp_network_recv(sd, buf, sizeof(buf));
            if (recv_len < (ssize_t)sizeof(STCPHeader)) {
                errno = ECONNREFUSED;
                stcp_unblock_application(sd);
                return;
            }
            
            header = (STCPHeader *) buf;
            
            /* Check if we got a SYN */
            if (header->th_flags & TH_SYN) {
                dprintf("Passive end: Received SYN, seq=%u\n", 
                        ntohl(header->th_seq));
                
                /* Initialize ACK number to peer's sequence number + 1 */
                ctx->ack_num = ntohl(header->th_seq) + 1;
                ctx->connection_state = CSTATE_SYN_RCVD;
                
                /* Send SYN-ACK */
                dprintf("Passive end: Sending SYN-ACK, seq=%u, ack=%u\n", 
                        ctx->sequence_num, ctx->ack_num);
                send_packet(sd, ctx, NULL, 0, TH_SYN|TH_ACK);
                ctx->sequence_num++; /* SYN consumes one byte in sequence space */
                
                /* Wait for ACK */
                event = stcp_wait_for_event(sd, NETWORK_DATA, NULL);
                if (event & NETWORK_DATA) {
                    recv_len = stcp_network_recv(sd, buf, sizeof(buf));
                    if (recv_len < (ssize_t)sizeof(STCPHeader)) {
                        errno = ECONNREFUSED;
                        stcp_unblock_application(sd);
                        return;
                    }
                    
                    header = (STCPHeader *) buf;
                    
                    /* Check if we got an ACK */
                    if ((header->th_flags & TH_ACK) && 
                        ntohl(header->th_ack) == ctx->sequence_num) {
                        
                        dprintf("Passive end: Received ACK, seq=%u, ack=%u\n", 
                                ntohl(header->th_seq), ntohl(header->th_ack));
                        
                        /* Update send window based on peer's advertised window */
                        ctx->send_window = ntohs(header->th_win);
                        
                        /* Connection established */
                        ctx->connection_state = CSTATE_ESTABLISHED;
                    } else {
                        errno = ECONNREFUSED;
                        stcp_unblock_application(sd);
                        return;
                    }
                } else {
                    errno = ECONNREFUSED;
                    stcp_unblock_application(sd);
                    return;
                }
            } else {
                errno = ECONNREFUSED;
                stcp_unblock_application(sd);
                return;
            }
        } else {
            errno = ECONNREFUSED;
            stcp_unblock_application(sd);
            return;
        }
    }

    /* Connection established, unblock the application */
    stcp_unblock_application(sd);
    
    /* Enter the main control loop */
    control_loop(sd, ctx);
    
    /* Clean up */
    free(ctx);
}

/* Generate random initial sequence number for an STCP connection */
static void generate_initial_seq_num(context_t *ctx)
{
    assert(ctx);

#ifdef FIXED_INITNUM
    /* please don't change this! */
    ctx->initial_sequence_num = 1;
#else
    /* Generate random number between 0 and 255 */
    ctx->initial_sequence_num = rand() % 256;
#endif
}

/* Create and send a packet with the provided data and flags */
static ssize_t send_packet(mysocket_t sd, context_t *ctx, const void *data, 
                          size_t data_len, uint8_t flags)
{
    STCPHeader *header;
    char packet[sizeof(STCPHeader) + STCP_MSS];
    ssize_t bytes_sent;
    
    assert(data_len <= STCP_MSS);
    
    /* Prepare the packet */
    header = (STCPHeader *)packet;
    memset(header, 0, sizeof(STCPHeader));
    
    /* Fill in the header fields */
    header->th_seq = htonl(ctx->sequence_num);
    header->th_ack = htonl(ctx->ack_num);
    header->th_off = 5; /* Header size in 32-bit words (20 bytes) */
    header->th_flags = flags;
    header->th_win = htons(ctx->recv_window);
    
    /* Copy data if any */
    if (data && data_len > 0) {
        memcpy(packet + sizeof(STCPHeader), data, data_len);
    }
    
    /* Send the packet */
    bytes_sent = stcp_network_send(sd, packet, sizeof(STCPHeader) + data_len, NULL);
    
    return bytes_sent;
}

/* Handle data transfer and connection termination */
static void control_loop(mysocket_t sd, context_t *ctx)
{
    assert(ctx);
    
    /* Buffer for receiving data */
    char buf[STCP_MSS + sizeof(STCPHeader)];
    /* Buffer for application data */
    char app_buf[STCP_MSS];
    
    STCPHeader *header;
    ssize_t bytes_received, bytes_read;
    unsigned int event;
    
    while (!ctx->done)
    {
        /* Wait for events */
        event = stcp_wait_for_event(sd, ANY_EVENT, NULL);
        
        /* Handle application data */
        if (event & APP_DATA)
        {
            /* Get data from application */
            bytes_read = stcp_app_recv(sd, app_buf, STCP_MSS);
            if (bytes_read > 0) {
                dprintf("Sending %d bytes of data, seq=%u\n", 
                        (int)bytes_read, ctx->sequence_num);
                
                /* Send data to peer */
                send_packet(sd, ctx, app_buf, bytes_read, TH_ACK);
                
                /* Update sequence number */
                ctx->sequence_num += bytes_read;
            }
        }
        
        /* Handle network data */
        if (event & NETWORK_DATA) 
        {
            /* Get data from the network */
            bytes_received = stcp_network_recv(sd, buf, sizeof(buf));
            
            if (bytes_received <= 0) {
                /* Connection error or closed by peer */
                if (ctx->connection_state == CSTATE_ESTABLISHED) {
                    ctx->connection_state = CSTATE_CLOSED;
                    ctx->done = TRUE;
                }
                continue;
            }
            
            header = (STCPHeader *)buf;
            tcp_seq recv_seq = ntohl(header->th_seq);
            
            /* Update peer's advertised window */
            ctx->send_window = ntohs(header->th_win);
            
            /* Handle FIN flag */
            if (header->th_flags & TH_FIN) {
                dprintf("Received FIN, seq=%u\n", recv_seq);
                ctx->fin_received = TRUE;
                
                /* ACK the FIN */
                ctx->ack_num = recv_seq + 1;
                send_packet(sd, ctx, NULL, 0, TH_ACK);
                
                /* Notify application of connection close */
                stcp_fin_received(sd);
                
                /* Update state based on current state */
                if (ctx->connection_state == CSTATE_ESTABLISHED) {
                    ctx->connection_state = CSTATE_CLOSE_WAIT;
                } else if (ctx->connection_state == CSTATE_FIN_WAIT_1) {
                    /* Simultaneous close */
                    ctx->connection_state = CSTATE_CLOSING;
                } else if (ctx->connection_state == CSTATE_FIN_WAIT_2) {
                    ctx->connection_state = CSTATE_TIME_WAIT;
                    /* In real TCP, we would start a timeout here. 
                       For STCP, we can close immediately */
                    ctx->done = TRUE;
                }
            }
            
            /* Handle ACK flag */
            if (header->th_flags & TH_ACK) {
                tcp_seq recv_ack = ntohl(header->th_ack);
                
                if (ctx->connection_state == CSTATE_FIN_WAIT_1 && 
                    recv_ack == ctx->sequence_num) {
                    /* Our FIN has been ACKed */
                    ctx->fin_acked = TRUE;
                    ctx->connection_state = CSTATE_FIN_WAIT_2;
                } else if (ctx->connection_state == CSTATE_CLOSING &&
                          recv_ack == ctx->sequence_num) {
                    /* Our FIN has been ACKed in simultaneous close */
                    ctx->fin_acked = TRUE;
                    ctx->connection_state = CSTATE_TIME_WAIT;
                    /* In real TCP, we would start a timeout here.
                       For STCP, we can close immediately */
                    ctx->done = TRUE;
                } else if (ctx->connection_state == CSTATE_LAST_ACK &&
                          recv_ack == ctx->sequence_num) {
                    /* Our FIN has been ACKed after CLOSE_WAIT */
                    ctx->fin_acked = TRUE;
                    ctx->connection_state = CSTATE_CLOSED;
                    ctx->done = TRUE;
                }
            }
            
            /* Handle data packets */
            size_t data_len = bytes_received - TCP_DATA_START(buf);
            if (data_len > 0 && recv_seq == ctx->ack_num) {
                /* Data is in order and expected */
                dprintf("Received %d bytes of data, seq=%u\n", 
                        (int)data_len, recv_seq);
                
                /* Update ack number */
                ctx->ack_num += data_len;
                
                /* Send ACK */
                send_packet(sd, ctx, NULL, 0, TH_ACK);
                
                /* Pass data to application */
                stcp_app_send(sd, buf + TCP_DATA_START(buf), data_len);
            } else if (data_len > 0) {
                /* Out of order data or duplicate, send ACK with current expected seq */
                dprintf("Received out-of-order data, expected=%u, got=%u\n", 
                        ctx->ack_num, recv_seq);
                send_packet(sd, ctx, NULL, 0, TH_ACK);
            }
        }
        
        /* Handle connection close request from application */
        if (event & APP_CLOSE_REQUESTED) 
        {
            if (!ctx->fin_sent) {
                dprintf("Application requested close, sending FIN\n");
                ctx->fin_sent = TRUE;
                
                /* Send FIN */
                send_packet(sd, ctx, NULL, 0, TH_FIN | TH_ACK);
                ctx->fin_seq = ctx->sequence_num;
                ctx->sequence_num++;  /* FIN consumes one byte in sequence space */
                
                /* Update state based on current state */
                if (ctx->connection_state == CSTATE_ESTABLISHED) {
                    ctx->connection_state = CSTATE_FIN_WAIT_1;
                } else if (ctx->connection_state == CSTATE_CLOSE_WAIT) {
                    ctx->connection_state = CSTATE_LAST_ACK;
                }
            }
        }
    }
}

/**********************************************************************/
/* our_dprintf
 *
 * Send a formatted message to stdout.
 * 
 * format               A printf-style format string.
 *
 * This function is equivalent to a printf, but may be
 * changed to log errors to a file if desired.
 *
 * Calls to this function are generated by the dprintf amd
 * dperror macros in transport.h
 */
void our_dprintf(const char *format,...)
{
    va_list argptr;
    char buffer[1024];

    assert(format);
    va_start(argptr, format);
    vsnprintf(buffer, sizeof(buffer), format, argptr);
    va_end(argptr);
    fputs(buffer, stdout);
    fflush(stdout);
}