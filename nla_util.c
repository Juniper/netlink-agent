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
#include <event2/listener.h>

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


const bits nla_events[] = {
    {NLA_CONNECTION_DOWN, "CONNECTION_DOWN"},
    {NLA_CONNECTION_UP,   "CONNECTION_UP"},
    {NLA_WRITE,           "WRITE"},
    {NLA_GET_ALL,         "NLA_GET_ALL"},
    {NLA_EVENT_MAX,       "EVENT_MAX"},
    {0, NULL}
};


const bits nla_modules[] = {
    {NLA_KNLM,        "NLA_KNLM"},
    {NLA_PRPD_CLIENT, "NLA_PRPD_CLIENT"},
    {NLA_FPM_SERVER,  "NLA_FPM_SERVER"},
    {NLA_FPM_CLIENT,  "NLA_FPM_CLIENT"},
    {NLA_NLM_SERVER,  "NLA_NLM_SERVER"},
    {NLA_NLM_CLIENT,  "NLA_NLM_CLIENT"},
    {NLA_MODULE_ALL,  "NLA_MODULE_ALL"},
    {0, NULL}
};


const char *
nla_trace_state (const bits *types, unsigned int mask)
{
    const bits *p;

    for (p = types; p->b_name; p++) {
        if (p->b_bits == mask) {
            return p->b_name;
        }
    }
    return "UNKNOWN";
}


unsigned int
nla_trace_bit (const bits *types, const char *name)
{
    const bits *p;

    for (p = types; p->b_name; p++) {
        if (!strcmp(p->b_name, name)) {
            return p->b_bits;
        }
    }
    return -1;
}


fpm_msg_hdr_t*
nla_build_fpm_hdr (size_t data_len)
{
    static fpm_msg_hdr_t fpm_hdr;
    fpm_hdr.version  = FPM_PROTO_VERSION;
    fpm_hdr.msg_type = FPM_MSG_TYPE_NETLINK;
    fpm_hdr.msg_len  = htons(fpm_data_len_to_msg_len(data_len));
    return &fpm_hdr;
}


/*
 * nlmsg_len
 */
size_t
nlmsg_len (const struct nlmsghdr *hdr)
{
  return (hdr->nlmsg_len);
}


void
nla_fpmmsg_dump (const void *msg, unsigned int msglen UNUSED)
{
    fpm_msg_hdr_t *fpmmsg = (fpm_msg_hdr_t *)msg;

    nla_log0(LOG_INFO, "--<FPM MESSAGE>");
    nla_log0(LOG_INFO, "  [FPM HEADER] %zu octets", FPM_MSG_HDR_LEN);
    nla_log0(LOG_INFO, "    .version = %d", fpmmsg->version);
    nla_log0(LOG_INFO, "    .msg_type = %d", fpmmsg->msg_type);
    nla_log0(LOG_INFO, "    .msg_len = %zu", fpm_msg_len(fpmmsg));
    nla_log0(LOG_INFO, "  [Note] fpm_msg_data_len = %zu", fpm_msg_data_len(fpmmsg));
}


void
nla_nl_object_dump (void *obj)
{
    struct nl_dump_params dp;
    memset(&dp, 0, sizeof(struct nl_dump_params));
    dp.dp_fd = nla_gl.nlag_trace_fd;
    dp.dp_type = NL_DUMP_DETAILS;

    nla_log0(LOG_INFO, "--<OBJ DETAILS>");
    nl_object_dump(OBJ_CAST(obj), &dp);
}


static void
nla_nlmsg_dump_detail (const void *msg, unsigned int msglen UNUSED)
{
    struct nlmsghdr *nlmsghdr = (struct nlmsghdr *)msg;
    struct rtnl_route *route = NULL;
    int err;

    err = rtnl_route_parse(nlmsghdr, &route);
    if (err < 0) {
        nla_log(LOG_INFO, "rtnl_route_parse error: %s", nl_geterror(err));
        return;
    }

    nla_nl_object_dump(route);
}


static void
nla_nlmsg_dump_extensive (const void *msg, unsigned int msglen UNUSED)
{
    struct nlmsghdr *nlmsghdr = (struct nlmsghdr *)msg;
    struct nl_msg *nl_msg;

    nl_msg = nlmsg_convert(nlmsghdr);
    nl_msg_dump(nl_msg, nla_gl.nlag_trace_fd);
    nlmsg_free(nl_msg);
}


void
nla_nlmsg_dump (const void *msg, unsigned int msglen)
{
    if (nla_gl.nlag_trace_level >= LOG_DEBUG) {
       nla_nlmsg_dump_extensive(msg, msglen);
    }

    if (nla_gl.nlag_trace_level >= LOG_INFO) {
        nla_nlmsg_dump_detail(msg, msglen);
    }
}


void
nla_nlmsg_walk (const void *msg, int msg_len,
                void (*nlmsg_cb)(const void *msg, unsigned int msg_len))
{
    struct nlmsghdr *nlmsghdr;
    int received_msg_len;

    received_msg_len = msg_len;
    nlmsghdr = (struct nlmsghdr *)msg;
    while (nlmsg_ok(nlmsghdr, received_msg_len)) {
        if (nlmsg_cb) {
             nlmsg_cb(nlmsghdr, nlmsghdr->nlmsg_len);
        }
        nlmsghdr = nlmsg_next(nlmsghdr, &received_msg_len);
    }
}


void
nla_fpm_msg_walk (const void *msg, int msg_len,
                  void (*fpm_msg_cb)(const void *msg, unsigned int msg_len) ,
                  void (*nlmsg_cb)(const void *msg, unsigned int msg_len))
{
    fpm_msg_hdr_t *fpm_msg_hdr;
    size_t received_msg_len;

    fpm_msg_hdr = (fpm_msg_hdr_t *)msg;
    received_msg_len = msg_len;
    while (fpm_msg_hdr_ok(fpm_msg_hdr)) {
        if (fpm_msg_cb) {
            fpm_msg_cb(fpm_msg_hdr, fpm_msg_len(fpm_msg_hdr));
        }

        nla_nlmsg_walk(fpm_msg_data(fpm_msg_hdr),
                       fpm_msg_data_len(fpm_msg_hdr),
                       nlmsg_cb);

        fpm_msg_hdr = fpm_msg_next(fpm_msg_hdr, &received_msg_len);
    }
}


nla_event_info_t *
nla_event_info_clone (nla_event_info_t *evinfo)
{
    nla_event_info_t *dup;

    dup = (nla_event_info_t*)calloc(1, sizeof(nla_event_info_t));
    dup->nlaei_type = evinfo->nlaei_type;
    dup->nlaei_msglen = evinfo->nlaei_msglen;

    dup->nlaei_msg = calloc(1, evinfo->nlaei_msglen);
    memcpy((void *)dup->nlaei_msg, evinfo->nlaei_msg, evinfo->nlaei_msglen);

    return dup;
}


void
nla_event_info_free (nla_event_info_t *evinfo)
{
    free((void *)evinfo->nlaei_msg);
    free(evinfo);
    return;
}


void
nla_context_cleanup (nla_context_t  *ctx)
{
     /* connect timer */
    if (ctx->nlac_start_timer) {
        event_free(ctx->nlac_start_timer);
        ctx->nlac_start_timer = NULL;
    }

    /* TCP: connection listener */
    if (ctx->nlac_listener) {
        evconnlistener_free(ctx->nlac_listener);
        ctx->nlac_listener = NULL;
    }

    /* TCP: bufferevent */
    if (ctx->nlac_bev) {
        bufferevent_free(ctx->nlac_bev);
        ctx->nlac_bev = NULL;
    }

    /* socket read event */
    if (ctx->nlac_socket_read) {
        event_free(ctx->nlac_socket_read);
        ctx->nlac_socket_read = NULL;
    }
}


