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



static nla_policy_t *
nla_policy_get_cfg (nla_module_id_t module)
{
    return nla_infa_modules[module].nlam_config.nlamc_policy;
}


/**
 * Strip off an attribute from a message.
 * [Note] currently only first level attributes can be removed,
          the case of attributes inside a nested attribute is not handled.
 *
 * Initial message format:
 *
 *  <------------------------------------- hdrlen ----------------------------------------->
 *  +-------------------+- - -+-----  ------------+- - -+--------+---+--------+---+--------+---+
 *  |  struct nlmsghdr  | Pad |  Protocol Header  | Pad | attr 1 |Pad| attr 2 |Pad| attr 3 |Pad|
 *  +-------------------+- - -+-------------------+- - -+--------+---+--------+---+--------+---+
 *                                                                    <strip this>
 * attr:
 *  <------------ nla_total_size(payload) ------------>
 *  +------------------+- - -+- - - - - - - - - +- - -+
 *  | Attribute Header | Pad |     Payload      | Pad |
 *  +------------------+- - -+- - - - - - - - - +- - -+
 *
 *
 *
 * Lets say we want to strip of "attr 2" from the message, then, then final message
 * needs to be formatted as below and hdrlen needs to be set accordingly.
 *
 *  <------------------------------------- hdrlen ---------------------------->
 *  +-------------------+- - -+-----  ------------+- - -+--------+---+--------+---+
 *  |  struct nlmsghdr  | Pad |  Protocol Header  | Pad | attr 1 |Pad| attr 3 |Pad|
 *  +-------------------+- - -+-------------------+- - -+--------+---+--------+---+
 */
static void
nla_policy_strip_attr (nla_event_info_t *evinfo, int attr_type)
{
    struct nlmsghdr *nlh;
    struct nlattr *attr;
    char *attr_start;
    char *attr_end;
    int attr_size;
    int memcpy_len;

    nlh = (struct nlmsghdr *)evinfo->nlaei_msg;

    while (true) {
        attr = nlmsg_find_attr(nlh, sizeof(struct rtmsg), attr_type);
        if (!attr) {
            return;
        }

        attr_size = nla_total_size(nla_len(attr));
        attr_start = (char *)attr;
        attr_end = attr_start + attr_size;
        memcpy_len = (char *)nlmsg_tail(nlh) - attr_end;

        memcpy(attr_start, attr_end, memcpy_len);

        nlh->nlmsg_len -= attr_size;
        evinfo->nlaei_msglen -= attr_size;

        nla_log(LOG_INFO, "stripped attr_type [%d] from msg", attr_type);
    }

    return;
}


static bool
nla_policy_match_filter (int module,
                         nla_policy_type_t policy_type,
                         const char *match_str,
                         int   match_value)
{
    int i;
    nla_policy_t *policy;


    policy = nla_policy_get_cfg((nla_module_id_t)module);

    if (!policy[policy_type].nlap_entries) {
        /* No policy configured */
        return true;
    }

    for (i = 0; i < policy[policy_type].nlap_entries; i++) {
        if (match_value == policy[policy_type].nlap_value[i]) {
            nla_log(LOG_INFO, "[%s %d] matched a filter policy",
                              match_str, match_value);
            return true;
        }
    }

    nla_log(LOG_INFO, "[%s %d] didn't match any  filter policies",
                      match_str, match_value);

    return false;
}


/**
 * Evaluate policy on nla_event_info_t to determine if it is acceptable or
 * need to be discarded for the module
 *
 * @return TRUE if the evinfo from module is acceptable
 */
nla_event_info_t *
nla_policy_evaluate (int module, nla_event_info_t *in_evinfo)
{
    nla_event_info_t *out_evinfo;
    nla_policy_t *policy;
    struct nlmsghdr *nlh;
    struct rtmsg *rtm;
    int i;

    out_evinfo = nla_event_info_clone(in_evinfo);
    nlh = (struct nlmsghdr *)out_evinfo->nlaei_msg;
    rtm = (struct rtmsg*)nlmsg_data(nlh);

    policy = nla_policy_get_cfg((nla_module_id_t)module);

    /*
     * handle filter policies
     */
    if (!nla_policy_match_filter(module, NLAP_FILTER_FAMILY, "rtm_family", rtm->rtm_family)) {
        goto fail;
    }

    if (!nla_policy_match_filter(module, NLAP_FILTER_TABLE, "rtm_table", rtm->rtm_table)) {
        goto fail;
    }

    if (!nla_policy_match_filter(module, NLAP_FILTER_PROTOCOL, "rtm_protocol", rtm->rtm_protocol)) {
        goto fail;
    }

    /*
     * handle set policies
     * TODO: Make multiple copies of the packet for each set
     */
    if (policy[NLAP_SET_TABLE].nlap_entries) {
        for (i = 0; i < policy[NLAP_SET_TABLE].nlap_entries; i++) {
            rtm->rtm_protocol = policy[NLAP_SET_TABLE].nlap_value[i];
            nla_log(LOG_INFO, "set rtm_table field to [%d]", policy[NLAP_SET_TABLE].nlap_value[i]);
        }
    }

    if (policy[NLAP_SET_PROTOCOL].nlap_entries) {
        for (i = 0; i < policy[NLAP_SET_PROTOCOL].nlap_entries; i++) {
            rtm->rtm_protocol = policy[NLAP_SET_PROTOCOL].nlap_value[i];
            nla_log(LOG_INFO, "set rtm_protocol field to [%d]", policy[NLAP_SET_PROTOCOL].nlap_value[i]);
        }
    }

    /*
     * remove some attibutes from the msg
     */
    if (policy[NLAP_STRIP_RTATTR].nlap_entries) {
        for (i = 0; i < policy[NLAP_STRIP_RTATTR].nlap_entries; i++) {
            nla_policy_strip_attr(out_evinfo, policy[NLAP_STRIP_RTATTR].nlap_value[i]);
        }
    }

    nla_log(LOG_INFO, "success");

    return out_evinfo;

fail:
    nla_event_info_free(out_evinfo);
    return NULL;

}



