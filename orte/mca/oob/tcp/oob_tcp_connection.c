/*
 * Copyright (c) 2004-2010 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2011 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart, 
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2006-2013 Los Alamos National Security, LLC. 
 *                         All rights reserved.
 * Copyright (c) 2009      Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2011      Oak Ridge National Labs.  All rights reserved.
 * Copyright (c) 2013      Intel, Inc.  All rights reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */

#include "orte_config.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <fcntl.h>
#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif
#ifdef HAVE_NET_UIO_H
#include <net/uio.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#include "opal/opal_socket_errno.h"
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#ifdef HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif

#include "opal/types.h"
#include "opal_stdint.h"
#include "opal/mca/backtrace/backtrace.h"
#include "opal/mca/base/mca_base_var.h"
#include "opal/util/output.h"
#include "opal/util/net.h"
#include "opal/util/error.h"
#include "opal/class/opal_hash_table.h"
#include "opal/mca/event/event.h"

#include "orte/util/name_fns.h"
#include "orte/mca/state/state.h"
#include "orte/runtime/orte_globals.h"
#include "orte/mca/errmgr/errmgr.h"
#include "orte/mca/ess/ess.h"
#include "orte/runtime/orte_wait.h"

#include "oob_tcp.h"
#include "orte/mca/oob/tcp/oob_tcp_component.h"
#include "orte/mca/oob/tcp/oob_tcp_peer.h"
#include "orte/mca/oob/tcp/oob_tcp_common.h"
#include "orte/mca/oob/tcp/oob_tcp_connection.h"

static void tcp_peer_event_init(mca_oob_tcp_module_t *mod,
                                 mca_oob_tcp_peer_t* peer);
static int  tcp_peer_send_connect_ack(mca_oob_tcp_module_t *mod,
                                       mca_oob_tcp_peer_t* peer);
static int tcp_peer_send_blocking(mca_oob_tcp_module_t *mod,
                                   mca_oob_tcp_peer_t* peer,
                                   void* data, size_t size);
static bool tcp_peer_recv_blocking(mca_oob_tcp_module_t *mod,
                                    mca_oob_tcp_peer_t* peer,
                                    void* data, size_t size);
static void tcp_peer_connected(mca_oob_tcp_peer_t* peer);

static int tcp_peer_create_socket(mca_oob_tcp_module_t *md,
                                   mca_oob_tcp_peer_t* peer)
{
    int flags;

    if (peer->sd > 0) {
        return ORTE_SUCCESS;
    }

    OPAL_OUTPUT_VERBOSE((1, orte_oob_base_framework.framework_output,
                         "%s oob:tcp:peer creating socket to %s",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         ORTE_NAME_PRINT(&(peer->name))));
    
    peer->sd = socket(AF_INET, SOCK_STREAM, 0);

    if (peer->sd < 0) {
        opal_output(0, "%s-%s tcp_peer_create_socket: socket() failed: %s (%d)\n",
                    ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                    ORTE_NAME_PRINT(&(peer->name)),
                    strerror(opal_socket_errno),
                    opal_socket_errno);
        return ORTE_ERR_UNREACH;
    }

    /* setup socket options */
    orte_oob_tcp_set_socket_options(peer->sd);

    /* setup event callbacks */
    tcp_peer_event_init(md, peer);

    /* setup the socket as non-blocking */
    if (peer->sd >= 0) {
        if((flags = fcntl(peer->sd, F_GETFL, 0)) < 0) {
            opal_output(0, "%s-%s tcp_peer_connect: fcntl(F_GETFL) failed: %s (%d)\n", 
                        ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                        ORTE_NAME_PRINT(&(peer->name)),
                        strerror(opal_socket_errno),
                        opal_socket_errno);
        } else {
            flags |= O_NONBLOCK;
            if(fcntl(peer->sd, F_SETFL, flags) < 0)
                opal_output(0, "%s-%s tcp_peer_connect: fcntl(F_SETFL) failed: %s (%d)\n", 
                            ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                            ORTE_NAME_PRINT(&(peer->name)),
                            strerror(opal_socket_errno),
                            opal_socket_errno);
        }
    }

    return ORTE_SUCCESS;
}


/*
 * Try connecting to a peer - cycle across all known addresses
 * until one succeeds. 
 */
void mca_oob_tcp_peer_try_connect(int fd, short args, void *cbdata)
{
    mca_oob_tcp_conn_op_t *op = (mca_oob_tcp_conn_op_t*)cbdata;
    mca_oob_tcp_peer_t *peer = op->peer;
    mca_oob_tcp_module_t *mod = (mca_oob_tcp_module_t*)op->mod;
    int rc;
    opal_socklen_t addrlen = 0;
    mca_oob_tcp_addr_t *addr;
    char *host;
    mca_oob_tcp_send_t *snd;
    bool connected = false;

    opal_output_verbose(OOB_TCP_DEBUG_CONNECT, orte_oob_base_framework.framework_output,
                        "%s orte_tcp_peer_try_connect: "
                        "attempting to connect to proc %s via interface %s",
                        ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                        ORTE_NAME_PRINT(&(peer->name)), mod->if_name);

    rc = tcp_peer_create_socket(op->mod, peer);
    if (ORTE_SUCCESS != rc) {
        /* we cannot create a TCP socket - this spans
         * all interfaces, so all we can do is report
         * back to the component that this peer is
         * unreachable so it can remove the peer
         * from its list and report back to the base
         * NOTE: this could be a reconnect attempt,
         * so we also need to mark any queued messages
         * and return them as "unreachable"
         */
    }

    opal_output_verbose(OOB_TCP_DEBUG_CONNECT, orte_oob_base_framework.framework_output,
                        "%s orte_tcp_peer_try_connect: "
                        "attempting to connect to proc %s via interface %s on socket %d",
                        ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                        ORTE_NAME_PRINT(&(peer->name)), mod->if_name, peer->sd);

    addrlen = sizeof(struct sockaddr_in);
    OPAL_LIST_FOREACH(addr, &peer->addrs, mca_oob_tcp_addr_t) {
        opal_output_verbose(OOB_TCP_DEBUG_CONNECT, orte_oob_base_framework.framework_output,
                            "%s orte_tcp_peer_try_connect: "
                            "attempting to connect to proc %s on %s:%d - %d retries",
                            ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                            ORTE_NAME_PRINT(&(peer->name)),
                            opal_net_get_hostname((struct sockaddr*)&addr->addr),
                            opal_net_get_port((struct sockaddr*)&addr->addr),
                            addr->retries);
        if (MCA_OOB_TCP_FAILED == addr->state) {
            opal_output_verbose(OOB_TCP_DEBUG_CONNECT, orte_oob_base_framework.framework_output,
                                "%s orte_tcp_peer_try_connect: %s:%d is down",
                                ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                opal_net_get_hostname((struct sockaddr*)&addr->addr),
                                opal_net_get_port((struct sockaddr*)&addr->addr));
            continue;
        }
        if (mca_oob_tcp_component.max_retries < addr->retries) {
            opal_output_verbose(OOB_TCP_DEBUG_CONNECT, orte_oob_base_framework.framework_output,
                                "%s orte_tcp_peer_try_connect: %s:%d retries exceeded",
                                ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                opal_net_get_hostname((struct sockaddr*)&addr->addr),
                                opal_net_get_port((struct sockaddr*)&addr->addr));
            continue;
        }
        peer->active_addr = addr;  // record the one we are using
    retry_connect:
        addr->retries++;
        if (connect(peer->sd, (struct sockaddr*)&addr->addr, addrlen) < 0) {
            /* non-blocking so wait for completion */
            if (opal_socket_errno == EINPROGRESS || opal_socket_errno == EWOULDBLOCK) {
                /* just ensure the send_event is active */
                if (!peer->send_ev_active) {
                    opal_event_add(&peer->send_event, 0);
                    peer->send_ev_active = true;
                }
                OBJ_RELEASE(op);
                return;
            }

            /* Some kernels (Linux 2.6) will automatically software
               abort a connection that was ECONNREFUSED on the last
               attempt, without even trying to establish the
               connection.  Handle that case in a semi-rational
               way by trying twice before giving up */
            if (ECONNABORTED == opal_socket_errno) {
                if (addr->retries < mca_oob_tcp_component.max_retries) {
                    goto retry_connect;
                } else {
                    /* We were unsuccessful in establishing this connection, and are
                     * not likely to suddenly become successful, so rotate to next option
                     */
                    addr->state = MCA_OOB_TCP_FAILED;
                    continue;
                }
            }
        } else {
            /* connection succeeded */
            addr->retries = 0;
            connected = true;
            break;
        }
    }

    if (!connected) {
        /* no address succeeded, so we cannot reach this peer */
        peer->state = MCA_OOB_TCP_FAILED;
        host = orte_get_proc_hostname(&(peer->name));
        opal_output_verbose(OOB_TCP_DEBUG_CONNECT, orte_oob_base_framework.framework_output,
                            "%s orte_tcp_peer_try_connect: "
                            "Connection across interface %s to proc %s on node %s failed",
                            ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                            ORTE_NAME_PRINT(&peer->name),
                            op->mod->if_name,
                            (NULL == host) ? "NULL" : host);
        /* let the TCP component know that this module failed to make
         * the connection so it can try other modules, and/or fail back
         * to the OOB level so another component can try. This will activate
         * an event in the component event base, and so it will fire async
         * from us if we are in our own progress thread
         */
        ORTE_ACTIVATE_TCP_CMP_OP(mod, &peer->name, mca_oob_tcp_component_failed_to_connect);
        /* post any messages in the send queue back to the OOB
         * level for reassignment
         */
        if (NULL != peer->send_msg) {
        }
        while (NULL != (snd = (mca_oob_tcp_send_t*)opal_list_remove_first(&peer->send_queue))) {
        }
        goto cleanup;
    }

    /* send our globally unique process identifier to the peer */
    if (ORTE_SUCCESS == (rc = tcp_peer_send_connect_ack(op->mod, peer))) {
        peer->state = MCA_OOB_TCP_CONNECT_ACK;
    } else {
        opal_output(0, 
                    "%s orte_tcp_peer_try_connect: "
                    "tcp_peer_send_connect_ack to proc %s on %s:%d failed: %s (%d)",
                    ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                    ORTE_NAME_PRINT(&(peer->name)),
                    opal_net_get_hostname((struct sockaddr*)&addr->addr),
                    opal_net_get_port((struct sockaddr*)&addr->addr),
                    opal_strerror(rc),
                    rc);
        ORTE_FORCED_TERMINATE(1);
    }

 cleanup:
    OBJ_RELEASE(op);
}

static int tcp_peer_send_connect_ack(mca_oob_tcp_module_t *mod,
                                      mca_oob_tcp_peer_t* peer)
{
    mca_oob_tcp_hdr_t hdr;

    opal_output_verbose(OOB_TCP_DEBUG_CONNECT, orte_oob_base_framework.framework_output,
                        "%s SEND CONNECT ACK", ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));

    /* send a handshake that includes our process identifier
     * to ensure we are talking to another OMPI process
    */
    hdr.origin = *ORTE_PROC_MY_NAME;
    hdr.dst = peer->name;
    hdr.type = MCA_OOB_TCP_IDENT;
    hdr.tag = 0;
    MCA_OOB_TCP_HDR_HTON(&hdr);
    if (0 > tcp_peer_send_blocking(mod, peer, &hdr, sizeof(hdr))) {
        ORTE_ERROR_LOG(ORTE_ERR_UNREACH);
        return ORTE_ERR_UNREACH;
    }
    return ORTE_SUCCESS;
}

/*
 * Initialize events to be used by the peer instance for TCP select/poll callbacks.
 */
static void tcp_peer_event_init(mca_oob_tcp_module_t *mod,
                                 mca_oob_tcp_peer_t* peer)
{
    if (peer->sd >= 0) {
        peer->mod = mod;
        opal_event_set(mod->ev_base,
                       &peer->recv_event,
                       peer->sd,
                       OPAL_EV_READ|OPAL_EV_PERSIST,
                       mca_oob_tcp_recv_handler,
                       peer);
        opal_event_set_priority(&peer->recv_event, ORTE_MSG_PRI);
        if (peer->recv_ev_active) {
            opal_event_del(&peer->recv_event);
            peer->recv_ev_active = false;
        }
        
        opal_event_set(mod->ev_base,
                       &peer->send_event,
                       peer->sd,
                       OPAL_EV_WRITE|OPAL_EV_PERSIST,
                       mca_oob_tcp_send_handler,
                       peer);
        opal_event_set_priority(&peer->send_event, ORTE_MSG_PRI);
        if (peer->send_ev_active) {
            opal_event_del(&peer->send_event);
            peer->send_ev_active = false;
        }
    }
}

/*
 * Check the status of the connection. If the connection failed, will retry
 * later. Otherwise, send this processes identifier to the peer on the
 * newly connected socket.
 */
void mca_oob_tcp_peer_complete_connect(mca_oob_tcp_module_t *mod,
                                        mca_oob_tcp_peer_t *peer)
{
    int so_error = 0;
    opal_socklen_t so_length = sizeof(so_error);

    opal_output_verbose(OOB_TCP_DEBUG_CONNECT, orte_oob_base_framework.framework_output,
                        "%s:tcp:complete_connect called for peer %s on socket %d",
                        ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                        ORTE_NAME_PRINT(&peer->name), peer->sd);

    /* check connect completion status */
    if (getsockopt(peer->sd, SOL_SOCKET, SO_ERROR, (char *)&so_error, &so_length) < 0) {
        opal_output(0, "%s-%s tcp_peer_complete_connect: getsockopt() failed: %s (%d)\n", 
                    ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                    ORTE_NAME_PRINT(&(peer->name)),
                    strerror(opal_socket_errno),
                    opal_socket_errno);
        peer->state = MCA_OOB_TCP_FAILED;
        mca_oob_tcp_peer_close(mod, peer);
        return;
    }

    if (so_error == EINPROGRESS) {
        opal_output_verbose(OOB_TCP_DEBUG_CONNECT, orte_oob_base_framework.framework_output,
                            "%s:tcp:send:handler still in progress",
                            ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
        return;
    } else if (so_error == ECONNREFUSED || so_error == ETIMEDOUT) {
        opal_output_verbose(OOB_TCP_DEBUG_CONNECT, orte_oob_base_framework.framework_output,
                            "%s-%s tcp_peer_complete_connect: connection failed: %s (%d)",
                            ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                            ORTE_NAME_PRINT(&(peer->name)),
                            strerror(so_error),
                            so_error);
        mca_oob_tcp_peer_close(mod, peer);
        return;
    } else if (so_error != 0) {
        /* No need to worry about the return code here - we return regardless
           at this point, and if an error did occur a message has already been
           printed for the user */
        mca_oob_tcp_peer_close(mod, peer);
        return;
    }

    opal_output_verbose(OOB_TCP_DEBUG_CONNECT, orte_oob_base_framework.framework_output,
                        "%s-%s tcp_peer_complete_connect: "
                        "sending ack, %d",
                        ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                        ORTE_NAME_PRINT(&(peer->name)), so_error);

    if (tcp_peer_send_connect_ack(mod, peer) == ORTE_SUCCESS) {
        peer->state = MCA_OOB_TCP_CONNECT_ACK;
        opal_output_verbose(OOB_TCP_DEBUG_CONNECT, orte_oob_base_framework.framework_output,
                            "%s-%s tcp_peer_complete_connect: "
                            "setting read event",
                            ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                            ORTE_NAME_PRINT(&(peer->name)));
        
        if (!peer->recv_ev_active) {
            opal_event_add(&peer->recv_event, 0);
            peer->recv_ev_active = true;
        }
    } else {
        opal_output(0, "%s-%s tcp_peer_complete_connect: unable to send connect ack.",
                    ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                    ORTE_NAME_PRINT(&(peer->name)));
        peer->state = MCA_OOB_TCP_FAILED;
        mca_oob_tcp_peer_close(mod, peer);
    }
}

/*
 * A blocking send on a non-blocking socket. Used to send the small amount of connection
 * information that identifies the peers endpoint.
 */
static int tcp_peer_send_blocking(mca_oob_tcp_module_t *mod,
                                   mca_oob_tcp_peer_t* peer,
                                   void* data, size_t size)
{
    unsigned char* ptr = (unsigned char*)data;
    size_t cnt = 0;
    int retval;

    while (cnt < size) {
        retval = send(peer->sd, (char*)ptr+cnt, size-cnt, 0);
        if (retval < 0) {
            if (opal_socket_errno != EINTR && opal_socket_errno != EAGAIN && opal_socket_errno != EWOULDBLOCK) {
                opal_output(0, "%s-%s tcp_peer_send_blocking: send() failed: %s (%d)\n",
                    ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                    ORTE_NAME_PRINT(&(peer->name)),
                    strerror(opal_socket_errno),
                    opal_socket_errno);
                peer->state = MCA_OOB_TCP_FAILED;
                mca_oob_tcp_peer_close(mod, peer);
                return -1;
            }
            continue;
        }
        cnt += retval;
    }
    return cnt;
}

/*
 *  Receive the peers globally unique process identification from a newly
 *  connected socket and verify the expected response. If so, move the
 *  socket to a connected state.
 */
int mca_oob_tcp_peer_recv_connect_ack(mca_oob_tcp_module_t *mod,
                                       mca_oob_tcp_peer_t* peer)
{
    mca_oob_tcp_hdr_t hdr;

    opal_output_verbose(OOB_TCP_DEBUG_CONNECT, orte_oob_base_framework.framework_output,
                        "%s RECV CONNECT ACK FROM %s ON SOCKET %d",
                        ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                        ORTE_NAME_PRINT(&peer->name), peer->sd);

    if (tcp_peer_recv_blocking(mod, peer, &hdr, sizeof(hdr))) {
        /* If the peer state is CONNECT_ACK, then we were waiting for
         * the connection to be ack'd
         */
        if (peer->state != MCA_OOB_TCP_CONNECT_ACK) {
            /* handshake broke down - abort this connection */
            opal_output(0, "%s RECV CONNECT BAD HANDSHAKE FROM %s ON SOCKET %d",
                        ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                        ORTE_NAME_PRINT(&peer->name), peer->sd);
            mca_oob_tcp_peer_close(mod, peer);
            return ORTE_ERR_UNREACH;
        }
    } else {
        /* unable to complete the recv */
        return ORTE_ERR_UNREACH;
    }

    MCA_OOB_TCP_HDR_NTOH(&hdr);
    if (hdr.type != MCA_OOB_TCP_IDENT) {
        opal_output(0, "tcp_peer_recv_connect_ack: invalid header type: %d\n", 
                    hdr.type);
        peer->state = MCA_OOB_TCP_FAILED;
        mca_oob_tcp_peer_close(mod, peer);
        return ORTE_ERR_UNREACH;
    }

    /* compare the peers name to the expected value */
    if (OPAL_EQUAL != orte_util_compare_name_fields(ORTE_NS_CMP_ALL, &peer->name, &hdr.origin)) {
        opal_output(0, "%s-%s tcp_peer_recv_connect_ack: "
            "received unexpected process identifier %s\n",
            ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
            ORTE_NAME_PRINT(&(peer->name)),
            ORTE_NAME_PRINT(&(hdr.origin)));
        peer->state = MCA_OOB_TCP_FAILED;
        mca_oob_tcp_peer_close(mod, peer);
        return ORTE_ERR_UNREACH;
    }

    /* set the peer into the component and OOB-level peer tables to indicate
     * that we know this peer and we will be handling him
     */
    ORTE_ACTIVATE_TCP_CMP_OP(mod, &peer->name, mca_oob_tcp_component_set_module);

    /* connected */
    tcp_peer_connected(peer);
    if (OOB_TCP_DEBUG_CONNECT <= opal_output_get_verbosity(orte_oob_base_framework.framework_output)) {
        mca_oob_tcp_peer_dump(peer, "connected");
    }
    return ORTE_SUCCESS;
}

/*
 *  Setup peer state to reflect that connection has been established,
 *  and start any pending sends.
 */
static void tcp_peer_connected(mca_oob_tcp_peer_t* peer)
{
    opal_output_verbose(OOB_TCP_DEBUG_CONNECT, orte_oob_base_framework.framework_output,
                        "%s-%s tcp_peer_connected on socket %d",
                        ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                        ORTE_NAME_PRINT(&(peer->name)), peer->sd);

    if (peer->timer_ev_active) {
        opal_event_del(&peer->timer_event);
        peer->timer_ev_active = false;
    }
    peer->state = MCA_OOB_TCP_CONNECTED;
    if (NULL != peer->active_addr) {
        peer->active_addr->retries = 0;
    }

    /* initiate send of first message on queue */
    if (NULL == peer->send_msg) {
        peer->send_msg = (mca_oob_tcp_send_t*)
            opal_list_remove_first(&peer->send_queue);
    }
    if (NULL != peer->send_msg && !peer->send_ev_active) {
        opal_event_add(&peer->send_event, 0);
        peer->send_ev_active = true;
    }
}

/*
 * Remove any event registrations associated with the socket
 * and update the peer state to reflect the connection has
 * been closed.
 */
void mca_oob_tcp_peer_close(mca_oob_tcp_module_t *mod,
                            mca_oob_tcp_peer_t *peer)
{
    mca_oob_tcp_send_t *snd;

    opal_output_verbose(OOB_TCP_DEBUG_CONNECT, orte_oob_base_framework.framework_output,
                        "%s-%s tcp_peer_close sd %d state %s",
                        ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                        ORTE_NAME_PRINT(&(peer->name)),
                        peer->sd, mca_oob_tcp_state_print(peer->state));

    peer->state = MCA_OOB_TCP_CLOSED;
    if (NULL != peer->active_addr) {
        peer->active_addr->state = MCA_OOB_TCP_CLOSED;
    }

    /* release the socket */
    close(peer->sd);

    /* inform the component-level that we have lost a connection so
     * it can decide what to do about it.
     */
    ORTE_ACTIVATE_TCP_CMP_OP(mod, &peer->name, mca_oob_tcp_component_lost_connection);

    if (orte_orteds_term_ordered || orte_finalizing || orte_abnormal_term_ordered) {
        /* nothing more to do */
        return;
    }

    /* push any queued messages back onto the OOB for retry - note that
     * this must be done after the prior call to ensure that the component
     * processes the "lost connection" notice before the OOB begins to
     * handle these recycled messages. This prevents us from unintentionally
     * attempting to send the message again across the now-failed interface
     */
    if (NULL != peer->send_msg) {
    }
    while (NULL != (snd = (mca_oob_tcp_send_t*)opal_list_remove_first(&peer->send_queue))) {
    }
}

/*
 * A blocking recv on a non-blocking socket. Used to receive the small amount of connection
 * information that identifies the peers endpoint.
 */
static bool tcp_peer_recv_blocking(mca_oob_tcp_module_t *mod,
                                    mca_oob_tcp_peer_t* peer,
                                    void* data, size_t size)
{
    unsigned char* ptr = (unsigned char*)data;
    size_t cnt = 0;
    while (cnt < size) {
        int retval = recv(peer->sd, (char *)ptr+cnt, size-cnt, 0);

        /* remote closed connection */
        if (retval == 0) {
            opal_output_verbose(OOB_TCP_DEBUG_CONNECT, orte_oob_base_framework.framework_output,
                                "%s-%s tcp_peer_recv_blocking: "
                                "peer closed connection: peer state %d",
                                ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                ORTE_NAME_PRINT(&(peer->name)),
                                peer->state);
            mca_oob_tcp_peer_close(mod, peer);
            return false;
        }

        /* socket is non-blocking so handle errors */
        if (retval < 0) {
            if (opal_socket_errno != EINTR && 
                opal_socket_errno != EAGAIN && 
                opal_socket_errno != EWOULDBLOCK) {
                if (peer->state == MCA_OOB_TCP_CONNECT_ACK) {
                    /* If we overflow the listen backlog, it's
                       possible that even though we finished the three
                       way handshake, the remote host was unable to
                       transition the connection from half connected
                       (received the initial SYN) to fully connected
                       (in the listen backlog).  We likely won't see
                       the failure until we try to receive, due to
                       timing and the like.  The first thing we'll get
                       in that case is a RST packet, which receive
                       will turn into a connection reset by peer
                       errno.  In that case, leave the socket in
                       CONNECT_ACK and propogate the error up to
                       recv_connect_ack, who will try to establish the
                       connection again */
                    return false;
                } else {
                    opal_output(0, 
                                "%s-%s tcp_peer_recv_blocking: "
                                "recv() failed: %s (%d)\n",
                                ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                ORTE_NAME_PRINT(&(peer->name)),
                                strerror(errno),
                                errno);
                    peer->state = MCA_OOB_TCP_FAILED;
                    mca_oob_tcp_peer_close(mod, peer);
                    return false;
                }
            }
            continue;
        }
        cnt += retval;
    }
    return true;
}

/*
 * Routine for debugging to print the connection state and socket options
 */
void mca_oob_tcp_peer_dump(mca_oob_tcp_peer_t* peer, const char* msg)
{
    char src[64];
    char dst[64];
    char buff[255];
    int sndbuf,rcvbuf,nodelay,flags;
    struct sockaddr_storage inaddr;
    opal_socklen_t addrlen = sizeof(struct sockaddr_storage);
    opal_socklen_t optlen;
                                                                                                            
    getsockname(peer->sd, (struct sockaddr*)&inaddr, &addrlen);
    snprintf(src, sizeof(src), "%s", opal_net_get_hostname((struct sockaddr*) &inaddr));
    getpeername(peer->sd, (struct sockaddr*)&inaddr, &addrlen);
    snprintf(dst, sizeof(dst), "%s", opal_net_get_hostname((struct sockaddr*) &inaddr));
                                                                                                            
    if ((flags = fcntl(peer->sd, F_GETFL, 0)) < 0) {
        opal_output(0, "tcp_peer_dump: fcntl(F_GETFL) failed: %s (%d)\n",
                    strerror(opal_socket_errno),
                    opal_socket_errno);
    }
                                                                                                            
#if defined(SO_SNDBUF)
    optlen = sizeof(sndbuf);
    if(getsockopt(peer->sd, SOL_SOCKET, SO_SNDBUF, (char *)&sndbuf, &optlen) < 0) {
        opal_output(0, "tcp_peer_dump: SO_SNDBUF option: %s (%d)\n", 
                    strerror(opal_socket_errno),
                    opal_socket_errno);
    }
#else
    sndbuf = -1;
#endif
#if defined(SO_RCVBUF)
    optlen = sizeof(rcvbuf);
    if (getsockopt(peer->sd, SOL_SOCKET, SO_RCVBUF, (char *)&rcvbuf, &optlen) < 0) {
        opal_output(0, "tcp_peer_dump: SO_RCVBUF option: %s (%d)\n", 
                    strerror(opal_socket_errno),
                    opal_socket_errno);
    }
#else
    rcvbuf = -1;
#endif
#if defined(TCP_NODELAY)
    optlen = sizeof(nodelay);
    if (getsockopt(peer->sd, IPPROTO_TCP, TCP_NODELAY, (char *)&nodelay, &optlen) < 0) {
        opal_output(0, "tcp_peer_dump: TCP_NODELAY option: %s (%d)\n", 
                    strerror(opal_socket_errno),
                    opal_socket_errno);
    }
#else
    nodelay = 0;
#endif

    snprintf(buff, sizeof(buff), "%s-%s %s: %s - %s nodelay %d sndbuf %d rcvbuf %d flags %08x\n",
        ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
        ORTE_NAME_PRINT(&(peer->name)),
        msg, src, dst, nodelay, sndbuf, rcvbuf, flags);
    opal_output(0, "%s", buff);
}

/*
 * Accept incoming connection - if not already connected
 */

bool mca_oob_tcp_peer_accept(mca_oob_tcp_module_t *mod, mca_oob_tcp_peer_t* peer)
{
    opal_output_verbose(OOB_TCP_DEBUG_CONNECT, orte_oob_base_framework.framework_output,
                        "%s tcp:peer_accept called for peer %s in state %s on socket %d",
                        ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                        ORTE_NAME_PRINT(&peer->name),
                        mca_oob_tcp_state_print(peer->state), peer->sd);

    if (peer->state != MCA_OOB_TCP_CONNECTED) {

        tcp_peer_event_init(mod, peer);

        if (tcp_peer_send_connect_ack(mod, peer) != ORTE_SUCCESS) {
            opal_output(0, "%s-%s tcp_peer_accept: "
                        "tcp_peer_send_connect_ack failed\n",
                        ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                        ORTE_NAME_PRINT(&(peer->name)));
            peer->state = MCA_OOB_TCP_FAILED;
            mca_oob_tcp_peer_close(mod, peer);
            return false;
        }

        /* set the peer into the component and OOB-level peer tables to indicate
         * that we know this peer and we will be handling him
         */
        ORTE_ACTIVATE_TCP_CMP_OP(mod, &peer->name, mca_oob_tcp_component_set_module);

        tcp_peer_connected(peer);
        if (!peer->recv_ev_active) {
            opal_event_add(&peer->recv_event, 0);
            peer->recv_ev_active = true;
        }
        /* if a message is waiting to be sent, ensure the send event is active */
        if (NULL != peer->send_msg && !peer->send_ev_active) {
            opal_event_add(&peer->send_event, 0);
            peer->send_ev_active = true;
        }
        if (OOB_TCP_DEBUG_CONNECT <= opal_output_get_verbosity(orte_oob_base_framework.framework_output)) {
            mca_oob_tcp_peer_dump(peer, "accepted");
        }
        return true;
    }
    return false;
}
