/**
 * Copyright(C) 2018, Juniper Networks, Inc.
 * All rights reserved
 *
 * shivakumar channalli
 *
 * This SOFTWARE is licensed to you under the Apache License 2.0 .
 * You may not use this code except in compliance with the License.
 * This code is not an official Juniper product.
 * You can obtain a copy of the License at http://spdx.org/licenses/Apache-2.0.html
 *
 * Third-Party Code: This SOFTWARE may depend on other components under
 * separate copyright notice and license terms.  Your use of the source
 * code for those components is subject to the term and conditions of
 * the respective license as noted in the Third-Party source code.
 */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <err.h>
#include <ctype.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/queue.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Libevent. */
#include <event.h>

/* Netlink */
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <netlink/msg.h>
#include <netlink/route/route.h>
#include <netlink/object.h>

/* nla header files. */
#include <nla_fpm.h>
#include <nla_defs.h>
#include <nla_externs.h>



nla_context_t       nla_knlm_ctx;
nla_module_vector_t nla_knlm_vector;


/* Netlink socket */
struct nl_sock *nlsock;


static void nla_knlm_connect_timer_start(void);


static void
nla_knlm_trigger_event (nla_event_t event, const void *msg, unsigned int msglen)
{
    nla_event_info_t evinfo;

    evinfo.nlaei_type = event;
    evinfo.nlaei_msglen  = msglen;
    evinfo.nlaei_msg = msg;

    nla_log(LOG_INFO, "from %s trigger event %s ", MODULE(NLA_KNLM), EVENT(event));
    nla_log(LOG_INFO, "msg %p, len %d", msg, msglen);
    nla_knlm_ctx.nlac_infravec->nlaiv_notify_cb(NLA_KNLM, &evinfo);
}


static void
nla_knlm_trigger_write (const void *msg, unsigned int msg_len)
{
    struct nlmsghdr *nlmsghdr = (struct nlmsghdr *)msg;

    nla_log(LOG_INFO, "clear nlmsg_flags");
    nlmsghdr->nlmsg_flags = 0;

    nla_knlm_trigger_event(NLA_WRITE, msg, msg_len);
}


/*
 * This function will be called for each valid
 * netlink message received in nl_recvmsgs_default()
 */
static int
nla_knlm_read_nl_msg (struct nl_msg *msg UNUSED, void *arg UNUSED)
{
    nla_log(LOG_INFO, "read bytes, msg %p len %u", nlmsg_hdr(msg), nlmsg_hdr(msg)->nlmsg_len);

    nla_nlmsg_walk(nlmsg_hdr(msg), nlmsg_hdr(msg)->nlmsg_len, nla_nlmsg_dump);
    nla_nlmsg_walk(nlmsg_hdr(msg), nlmsg_hdr(msg)->nlmsg_len, nla_knlm_trigger_write);

    return 0;
}


static void
nla_knlm_socket_read_msg (evutil_socket_t fd UNUSED, short what UNUSED, void *arg)
{
    nla_log(LOG_INFO, " ");
    nl_recvmsgs_default((struct nl_sock *)arg);
}


static void
nla_knlm_connect (evutil_socket_t fd UNUSED, short what UNUSED, void *arg UNUSED)
{
    int retval;

    nla_log(LOG_INFO, " ");

    /* allocate a new socket */
    nlsock = nl_socket_alloc();

    /* set socket to non-blocking state */
    nl_socket_set_nonblocking(nlsock);

    nl_socket_disable_auto_ack(nlsock);

    nl_socket_disable_seq_check(nlsock);

    /* register callback function, to receive notifications */
    nl_socket_modify_cb(nlsock, NL_CB_VALID, NL_CB_CUSTOM, nla_knlm_read_nl_msg, NULL);

    /* subscribe to route notifications group */
    nl_join_groups(nlsock, NLA_RTMGRP_ALL);
    nl_socket_add_memberships(nlsock, NLA_RTNLGRP_ALL, 0);

    /* Connect to routing netlink protocol */
    retval = nl_connect(nlsock, NETLINK_ROUTE);
    if (retval != NLE_SUCCESS) {
        goto retry;
    }

    nla_knlm_ctx.nlac_socket_read = event_new(nla_gl.nlag_base,
                                              nl_socket_get_fd(nlsock),
                                              EV_READ|EV_PERSIST,
                                              nla_knlm_socket_read_msg,
                                              nlsock);
    if (!nla_knlm_ctx.nlac_socket_read) {
        goto retry;
    }

    event_add(nla_knlm_ctx.nlac_socket_read, NULL);

    nla_knlm_trigger_event(NLA_CONNECTION_UP, NULL, 0);

    return;

retry:

    nla_knlm_trigger_event(NLA_CONNECTION_DOWN, NULL, 0);
    nla_knlm_connect_timer_start();

    return;
}


static void
nla_knlm_reset (void)
{
    nla_log(LOG_INFO, " ");

    nl_socket_free(nlsock);
    nlsock = NULL;

    nla_context_cleanup(&nla_knlm_ctx);
}


static void
nla_knlm_connect_timer_start ()
{
    struct timeval knlm_retry_timer = {2,0};

    nla_log(LOG_INFO, " ");

    nla_knlm_reset();

    if (!nla_gl.nlag_base) {
        nla_log(LOG_INFO, "error");
        return;
    }

    /* Create a dispatcher event */
    assert(!nla_knlm_ctx.nlac_start_timer);
    nla_knlm_ctx.nlac_start_timer = evtimer_new(nla_gl.nlag_base,
                                                nla_knlm_connect,
                                                NULL);

    evtimer_add(nla_knlm_ctx.nlac_start_timer, &knlm_retry_timer);
}


static void
nla_knlm_init ()
{
    nla_log(LOG_INFO, " ");

    nla_knlm_ctx.nlac_infravec = nla_infra_get_vec();

    nla_knlm_connect_timer_start();
}


static void
nla_knlm_init_flash (void)
{
    struct rtmsg rhdr;

    nla_log(LOG_INFO, "request route flash from knlm");

    /* Read all the state form kernel */
    memset(&rhdr, 0, sizeof(struct rtmsg));
    nl_send_simple(nlsock, RTM_GETROUTE, NLM_F_DUMP, &rhdr, sizeof(rhdr));
}


static void
nla_knlm_notify (nla_module_id_t from UNUSED, nla_event_info_t *evinfo)
{
    struct rtnl_route *route = NULL;
    int err;

    switch (evinfo->nlaei_type) {
    case NLA_WRITE:

        nla_log(LOG_INFO, "%s : write to kernel, msg %p len %d",
                EVENT(evinfo->nlaei_type), evinfo->nlaei_msg, evinfo->nlaei_msglen);

        err = rtnl_route_parse((struct nlmsghdr *)evinfo->nlaei_msg, &route);
        if (err < 0) {
            nla_log(LOG_INFO, "rtnl_route_parse error: %s", nl_geterror(err));
            return;
        }

        switch (nl_object_get_msgtype(OBJ_CAST(route))) {
        case RTM_NEWROUTE:
            err = rtnl_route_add(nlsock, route, NLM_F_CREATE);
            if (err < 0) {
                nla_log(LOG_INFO, "Unable to add route: %s", nl_geterror(err));
            }
            break;

        case RTM_DELROUTE:
            err = rtnl_route_delete(nlsock, route, 0);
            if (err < 0) {
                nla_log(LOG_INFO, "Unable to delete route: %s", nl_geterror(err));
            }
            break;
        }

        nl_object_free(OBJ_CAST(route));
        break;

    default:
        nla_log(LOG_INFO, "%s : ok", EVENT(evinfo->nlaei_type));
        break;
    }
}


nla_module_vector_t*
nla_knlm_get_vec (void)
{
    nla_log(LOG_INFO, " ");

    memset(&nla_knlm_ctx, 0, sizeof(nla_knlm_ctx));
    memset(&nla_knlm_vector, 0, sizeof(nla_knlm_vector));

    nla_knlm_vector.nlamv_module           = NLA_KNLM;
    nla_knlm_vector.nlamv_init_cb          = nla_knlm_init;
    nla_knlm_vector.nlamv_reset_cb         = nla_knlm_reset;
    nla_knlm_vector.nlamv_init_flash_cb    = nla_knlm_init_flash;
    nla_knlm_vector.nlamv_notify_cb        = nla_knlm_notify;

    return &nla_knlm_vector;
}

