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




nla_context_t       nla_prpdc_ctx;
nla_module_vector_t nla_prpdc_vector;

static void nla_prpdc_server_connect_timer_start ();


static void
nla_prpdc_trigger_event (nla_event_t event, const void *msg, unsigned int msglen)
{
    nla_event_info_t evinfo;

    evinfo.nlaei_type = event;
    evinfo.nlaei_msglen = msglen;
    evinfo.nlaei_msg = msg;

    nla_log(LOG_INFO, "from %s trigger event %s ", MODULE(NLA_PRPD_CLIENT), EVENT(event));
    nla_log(LOG_INFO, "msg %p, len %d", msg, msglen);
    nla_prpdc_ctx.nlac_infravec->nlaiv_notify_cb(NLA_PRPD_CLIENT, &evinfo);
}


void
nla_prpdc_event_cb (nla_event_t event)
{
    switch(event) {
    case NLA_CONNECTION_DOWN:
        nla_log(LOG_INFO, "Connection down");
        break;
    case NLA_CONNECTION_UP:
        nla_log(LOG_INFO, "connection with server established");
        break;
    default:
        nla_log(LOG_INFO, "%s : ok", EVENT(event));
        break;
    }

    nla_prpdc_trigger_event(event, NULL, 0);
}


static void
nla_prpdc_server_connect (evutil_socket_t fd UNUSED,
                        short what UNUSED,
                        void *arg UNUSED)
{
    char ribServerAddr[100];

    snprintf(ribServerAddr, sizeof(ribServerAddr), "%s:%d",
             nla_prpdc_ctx.nlac_infravec->nlaiv_get_addr_str(NLA_PRPD_CLIENT),
             nla_prpdc_ctx.nlac_infravec->nlaiv_get_port(NLA_PRPD_CLIENT));

    if (RibClientInit(ribServerAddr) < 0) {
        nla_log(LOG_INFO, "nla_prpdc_server_connect failure");
        goto retry;
    }

    return;

retry:

    nla_prpdc_server_connect_timer_start();
}


static void
nla_prpdc_reset (void)
{
    nla_log(LOG_INFO, " ");

    RibClientReset();
    nla_context_cleanup(&nla_prpdc_ctx);
}


static void
nla_prpdc_server_connect_timer_start ()
{
    struct timeval retry_timer = {2,0};

    nla_log(LOG_INFO, " ");

    /* Always start from fresh */
    nla_prpdc_reset();

    if (!nla_gl.nlag_base) {
        nla_log(LOG_INFO, "nla_prpdc_server_connect_timer_start failure");
        return;
    }

    /* Create a dispatcher event */
    assert(!nla_prpdc_ctx.nlac_start_timer);
    nla_prpdc_ctx.nlac_start_timer = evtimer_new(nla_gl.nlag_base,
                                                 nla_prpdc_server_connect,
                                                 NULL);

    evtimer_add(nla_prpdc_ctx.nlac_start_timer, &retry_timer);
}


static void
nla_prpdc_init ()
{
    nla_log(LOG_INFO, " ");

    nla_prpdc_ctx.nlac_infravec = nla_infra_get_vec();

    nla_prpdc_server_connect_timer_start();
}


static void
nla_prpdc_init_flash (void)
{
    nla_log(LOG_INFO, " ");
}


static void
nla_prpdc_notify (nla_module_id_t from UNUSED, nla_event_info_t *evinfo)
{
    struct rtnl_route *route = NULL;
    int err;
    int retVal = 0;

    switch (evinfo->nlaei_type) {
    case NLA_WRITE:
        nla_log(LOG_INFO, "%s : write to prpd server, msg %p len %d",
                EVENT(evinfo->nlaei_type), evinfo->nlaei_msg, evinfo->nlaei_msglen);

        err = rtnl_route_parse((struct nlmsghdr *)evinfo->nlaei_msg, &route);
        if (err < 0) {
            nla_log(LOG_INFO, "rtnl_route_parse error: %s", nl_geterror(err));
            return;
        }

        switch (nl_object_get_msgtype(OBJ_CAST(route))) {
        case RTM_NEWROUTE:
            retVal = RibClientAddRoute(route);
            break;

        case RTM_DELROUTE:
            retVal = RibClientRemoveRoute(route);
            break;
        }

        if (retVal < 0) {
            nla_log(LOG_INFO, "RibClient write operation failed ");
        }

        nl_object_free(OBJ_CAST(route));

        break;

    default:
        nla_log(LOG_INFO, "%s : ok", EVENT(evinfo->nlaei_type));
        break;
    }
}


nla_module_vector_t*
nla_prpdc_get_vec (void)
{
    nla_log(LOG_INFO, " ");

    memset(&nla_prpdc_ctx, 0, sizeof(nla_prpdc_ctx));
    memset(&nla_prpdc_vector, 0, sizeof(nla_prpdc_vector));

    nla_prpdc_vector.nlamv_module           = NLA_PRPD_CLIENT;
    nla_prpdc_vector.nlamv_init_cb          = nla_prpdc_init;
    nla_prpdc_vector.nlamv_reset_cb         = nla_prpdc_reset;
    nla_prpdc_vector.nlamv_init_flash_cb    = nla_prpdc_init_flash;
    nla_prpdc_vector.nlamv_notify_cb        = nla_prpdc_notify;

    return &nla_prpdc_vector;
}


