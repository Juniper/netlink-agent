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
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Libevent. */
#include <event.h>
#include <event2/listener.h>

/* Netlink */
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <netlink/msg.h>

/* nla header files. */
#include <nla_fpm.h>
#include <nla_defs.h>
#include <nla_externs.h>



nla_context_t       nla_fpm_server_ctx;
nla_module_vector_t nla_fpm_server_vector;


static void nla_fpm_server_listener_timer_start ();


static void
nla_fpm_server_trigger_event (nla_event_t event, const void *msg, unsigned int msglen)
{
    nla_event_info_t evinfo;

    evinfo.nlaei_type = event;
    evinfo.nlaei_msglen  = msglen;
    evinfo.nlaei_msg = msg;

    nla_log(LOG_INFO, "from %s trigger event %s ", MODULE(NLA_FPM_SERVER), EVENT(event));
    nla_log(LOG_INFO, "msg %p, len %d", msg, msglen);
    nla_fpm_server_ctx.nlac_infravec->nlaiv_notify_cb(NLA_FPM_SERVER, &evinfo);
}


static void
nla_fpm_server_trigger_write (const void *msg, unsigned int msg_len)
{
    nla_fpm_server_trigger_event(NLA_WRITE, msg, msg_len);
}


static void
nla_fpm_server_write_cb (struct bufferevent *bev UNUSED, void *ctx UNUSED)
{
    nla_log(LOG_INFO, "sent msg");
}


static void
nla_fpm_server_read_cb (struct bufferevent *bev, void *ctx UNUSED)
{
    struct evbuffer *inevb;
    fpm_msg_hdr_t fpm_msg_hdr;
    uint8_t data[8192];
    size_t n;

    for (;;) {
        /* Read one fpm message at a time. */
        inevb = bufferevent_get_input(bev);
        n = evbuffer_get_length(inevb);
        if (n <= 0) {
            /* Done. */
            break;
        }

        if (n < FPM_MSG_HDR_LEN) {
            nla_log(LOG_INFO, "[read bytes %zu] We should never come here", n);
            break;
        }

        /* Take a look at the fpm header to find out the msg length */
        evbuffer_copyout(inevb, &fpm_msg_hdr, FPM_MSG_HDR_LEN);

        if (!fpm_msg_hdr_ok(&fpm_msg_hdr)) {
            nla_log(LOG_INFO, "fpm_msg_hdr_ok check failed");
            assert(false);
        }

        if (n < fpm_msg_len(&fpm_msg_hdr)) {
            nla_log(LOG_INFO, "[read bytes %zu, fpm msg len %zu] Not enough data to proceed",
                    n, fpm_msg_len(&fpm_msg_hdr));
            break;
        }

        n = bufferevent_read(bev, data, fpm_msg_len(&fpm_msg_hdr));

        nla_log(LOG_INFO, "read bytes, msg %p len %zu", data, n);

        nla_fpm_msg_walk(data, n, nla_fpmmsg_dump, nla_nlmsg_dump);
        nla_fpm_msg_walk(data, n, NULL, nla_fpm_server_trigger_write);
    }
}


static void
nla_fpm_server_event_cb (struct bufferevent *bev UNUSED, short what, void *ctx UNUSED)
{

    /* Errors */
    nla_log(LOG_INFO, "0x%x", what);

    if (what & BEV_EVENT_EOF) {
        nla_log(LOG_INFO, "Connection closed.\n");
    } else if (what & BEV_EVENT_ERROR) {
        nla_log(LOG_INFO, "Got an error on the connection: %s\n",
                evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
    } else {
        return;
    }

    nla_fpm_server_trigger_event(NLA_CONNECTION_DOWN, NULL, 0);
    nla_log(LOG_INFO, "[event 0x%x] retry connection", what);
    nla_fpm_server_listener_timer_start();
}


static void
nla_fpm_server_accept_connections (struct evconnlistener *listener UNUSED,
                                   evutil_socket_t fd,
                                   struct sockaddr *sa UNUSED,
                                   int socklen UNUSED,
                                   void *user_data UNUSED)
{
    nla_log(LOG_INFO, " ");

    if (nla_fpm_server_ctx.nlac_bev) {
        nla_log(LOG_INFO, "only 1 connection allowed");
        return;
    }

    nla_fpm_server_ctx.nlac_bev = bufferevent_socket_new(nla_gl.nlag_base,
                                                         fd,
                                                         BEV_OPT_CLOSE_ON_FREE);
    if (!nla_fpm_server_ctx.nlac_bev) {
        nla_log(LOG_INFO, "bufferevent_socket_new failure");
        return;
    }

    bufferevent_setcb(nla_fpm_server_ctx.nlac_bev,
                      nla_fpm_server_read_cb,
                      nla_fpm_server_write_cb,
                      nla_fpm_server_event_cb,
                      NULL);

    bufferevent_enable(nla_fpm_server_ctx.nlac_bev, EV_READ | EV_WRITE);

    bufferevent_setwatermark(nla_fpm_server_ctx.nlac_bev, EV_READ, FPM_MSG_HDR_LEN, 0);

    nla_log(LOG_INFO, "connection with fpm client established");

    nla_fpm_server_trigger_event(NLA_CONNECTION_UP, NULL, 0);
}


static void
nla_accept_error_cb (struct evconnlistener *listener UNUSED, void *ctx UNUSED)
{
    nla_log(LOG_INFO, "Got an error on the listener: %s\n, retry connection",
            evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
    nla_fpm_server_listener_timer_start();
}


static void
nla_fpm_server_listener (evutil_socket_t fd UNUSED,
                     short what UNUSED,
                     void *arg UNUSED)
{
    struct sockaddr_un addr;

    nla_log(LOG_INFO, " ");

    nla_fpm_server_ctx.nlac_infravec->nlaiv_get_sockaddr(NLA_FPM_SERVER, &addr);

    nla_fpm_server_ctx.nlac_listener = evconnlistener_new_bind(nla_gl.nlag_base,
                                           nla_fpm_server_accept_connections,
                                           NULL,
                                           LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_FREE,
                                           -1,
                                           (struct sockaddr*)&addr,
                                           sizeof(addr));

    if (!nla_fpm_server_ctx.nlac_listener) {
        nla_log(LOG_INFO, "failed to create a listener!\n");
        return;
    }

    evconnlistener_set_error_cb(nla_fpm_server_ctx.nlac_listener, nla_accept_error_cb);

}


static void
nla_fpm_server_reset (void)
{
    nla_log(LOG_INFO, " ");
    nla_context_cleanup(&nla_fpm_server_ctx);
}


static void
nla_fpm_server_listener_timer_start ()
{
    struct timeval retry_timer = {2,0};

    nla_log(LOG_INFO, " ");

    /* Always start from fresh */
    nla_fpm_server_reset();

    if (!nla_gl.nlag_base) {
        nla_log(LOG_INFO, "nla_fpm_server_listener_timer_start failure");
        return;
    }

    /* Create a dispatcher event */
    assert(!nla_fpm_server_ctx.nlac_start_timer);
    nla_fpm_server_ctx.nlac_start_timer = evtimer_new(nla_gl.nlag_base,
                                                      nla_fpm_server_listener,
                                                      NULL);

    evtimer_add(nla_fpm_server_ctx.nlac_start_timer, &retry_timer);
}


static void
nla_fpm_server_init ()
{
    nla_log(LOG_INFO, " ");

    nla_fpm_server_ctx.nlac_infravec = nla_infra_get_vec();

    nla_fpm_server_listener_timer_start();
}


static void
nla_fpm_server_init_flash (void)
{
    nla_log(LOG_INFO, " ");
}


static void
nla_fpm_server_notify (nla_module_id_t from UNUSED, nla_event_info_t *evinfo)
{
    struct evbuffer *outevb;

    switch(evinfo->nlaei_type) {
    case NLA_WRITE:
        outevb = evbuffer_new();

        nla_log(LOG_INFO, "%s : write to fpm client, msg %p len %d",
                EVENT(evinfo->nlaei_type), evinfo->nlaei_msg, evinfo->nlaei_msglen);

        evbuffer_add(outevb, nla_build_fpm_hdr(evinfo->nlaei_msglen), FPM_MSG_HDR_LEN);

        evbuffer_add(outevb, evinfo->nlaei_msg, evinfo->nlaei_msglen);

        if (bufferevent_write_buffer(nla_fpm_server_ctx.nlac_bev, outevb) < 0) {
            nla_log(LOG_INFO, "bufferevent_write_buffer failed");
            /*
             * If we hit this core, we might need to create evbuffer queue
             * which gets processed in the background.
             */
            assert(false);
        }

        evbuffer_free(outevb);

        break;

    default:
        nla_log(LOG_INFO, "%s : ok", EVENT(evinfo->nlaei_type));
        break;
    }
}


nla_module_vector_t*
nla_fpm_server_get_vec (void)
{
    nla_log(LOG_INFO, " ");

    memset(&nla_fpm_server_ctx, 0, sizeof(nla_fpm_server_ctx));
    memset(&nla_fpm_server_vector, 0, sizeof(nla_fpm_server_vector));

    nla_fpm_server_vector.nlamv_module           = NLA_FPM_SERVER;
    nla_fpm_server_vector.nlamv_init_cb          = nla_fpm_server_init;
    nla_fpm_server_vector.nlamv_reset_cb         = nla_fpm_server_reset;
    nla_fpm_server_vector.nlamv_init_flash_cb    = nla_fpm_server_init_flash;
    nla_fpm_server_vector.nlamv_notify_cb        = nla_fpm_server_notify;

    return &nla_fpm_server_vector;
}


