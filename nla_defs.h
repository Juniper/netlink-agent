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

#ifndef _NLA_DEFS_H
#define _NLA_DEFS_H

#ifdef __cplusplus
extern "C" {
#endif


#ifndef UNUSED
#define UNUSED __attribute__ ((__unused__))
#endif


#define NLA_VERSION 1


typedef struct bits_s {
    unsigned int b_bits;
    const char  *b_name;
} bits;


typedef enum nla_event_e {
    NLA_CONNECTION_DOWN = 1,
    NLA_CONNECTION_UP,
    NLA_WRITE,
    NLA_GET_ALL,
    NLA_EVENT_MAX,
} nla_event_t;


typedef enum nla_module_e {
    NLA_KNLM,        /* Kernel Netlink Manager  : Interacts with kernel to exchange Netlink msgs */
    NLA_PRPD_CLIENT, /* Programmable RPD client : Interacts with RPD daemon through GRPC */
    NLA_FPM_SERVER,  /* TCP Server for FIB Push/pull Manager : send/receive FPM msgs */
    NLA_FPM_CLIENT,  /* TCP Client for FIB Push/pull Manager : send/receive FPM msgs */
    NLA_NLM_SERVER,  /* TCP Server for Netlink Manager : send/receive Netlink msgs */
    NLA_NLM_CLIENT,  /* TCP Client for Netlink Manager : send/receive Netlink msgs */
    NLA_MODULE_ALL,
} nla_module_id_t;


typedef struct nla_event_info_s {
    int         nlaei_type;
    int         nlaei_msglen;
    const void *nlaei_msg;
} nla_event_info_t;


typedef enum nla_policy_e {
    NLAP_FILTER_FAMILY,
    NLAP_FILTER_TABLE,
    NLAP_FILTER_PROTOCOL,
    NLAP_SET_TABLE,
    NLAP_SET_PROTOCOL,
    NLAP_STRIP_RTATTR,
    NLAP_MAX,
} nla_policy_type_t;


#define NLA_POLICY_ENTRIES_MAX 30


typedef struct nla_policy_s {
    int nlap_entries;
    int nlap_value[NLA_POLICY_ENTRIES_MAX];
} nla_policy_t;


typedef struct nla_infra_vector_s {
    void  (*nlaiv_notify_cb)(nla_module_id_t, nla_event_info_t *);
    void  (*nlaiv_get_sockaddr)(nla_module_id_t module, struct sockaddr_un *un_addr);
    char *(*nlaiv_get_addr_str)(nla_module_id_t);
    int   (*nlaiv_get_port)(nla_module_id_t);
} nla_infra_vector_t;


typedef struct nla_module_vector_s {
    nla_module_id_t nlamv_module;
    void (*nlamv_init_cb)();
    void (*nlamv_reset_cb)(void);
    void (*nlamv_init_flash_cb)(void);
    void (*nlamv_notify_cb)(nla_module_id_t, nla_event_info_t *);
} nla_module_vector_t;


typedef struct nla_module_config_s {
    bool         nlamc_enable;
    char        *nlamc_addr;
    int          nlamc_port;
    nla_policy_t nlamc_policy[NLAP_MAX];
    bool         nlamc_notify_me[NLA_MODULE_ALL];
} nla_module_config_t;


typedef struct nla_module_s {
    nla_module_vector_t *nlam_vec;
    int                  nlam_connection_state;
    nla_module_config_t  nlam_config;
} nla_module_t;


/* A client or server connection. */
typedef struct nla_context_s {
    struct event          *nlac_start_timer;     /* connect timer */
    struct evconnlistener *nlac_listener;        /* TCP: connection listener */
    struct bufferevent    *nlac_bev;             /* TCP: bufferevent */
    struct event          *nlac_socket_read;     /* socket read event */
    nla_infra_vector_t    *nlac_infravec;
} nla_context_t;


typedef struct nla_globals_s {
    struct event_base *nlag_base;   /* Event handler context */
    struct event      *nlag_reinit; /* Timer context for modules reinit */
    char              *nlag_config_file;
    int                nlag_trace_level;
    char              *nlag_trace_file;
    FILE              *nlag_trace_fd;
    int                nlag_version;
    bool               nlag_dont_daemonize;
} nla_globals_t;


/*
 *  Make globals visible.
 */
extern nla_globals_t nla_gl;
extern const bits nla_modules[];
extern const bits nla_events[];
extern nla_module_t nla_infa_modules[];


/* Port to connect on. */
#define NLA_INVALID -1


#define NL_MSG_HDR_LEN (sizeof(struct nlmsghdr))


#define NLA_RTMGRP_ALL  (RTMGRP_IPV4_ROUTE  | RTMGRP_IPV6_ROUTE)
#define NLA_RTNLGRP_ALL (RTNLGRP_IPV4_ROUTE | RTNLGRP_IPV6_ROUTE)


#define MODULE(x) nla_trace_state(nla_modules, (x))
#define EVENT(x)  nla_trace_state(nla_events, (x))


/*
 * Loging related code.
 */

typedef enum nla_log_e {
    LOG_DEFAULT = 0,
    LOG_ERR     = LOG_DEFAULT,
    LOG_NOTICE  = 1,
    LOG_WARN    = 2,
    LOG_INFO    = 3,
    LOG_DEBUG   = 4,
} nla_log_t;

#define nla_log_enabled(trace_level) (nla_gl.nlag_trace_fd && (trace_level <= nla_gl.nlag_trace_level))

#define nla_log0(trace_level, ...) nla_log_(false, trace_level, __VA_ARGS__)
#define nla_log(trace_level,  ...) nla_log_(true,  trace_level, __VA_ARGS__)

#define nla_log_(more_info, trace_level, ...)\
{\
    if (nla_log_enabled(trace_level)) {\
        if (more_info) {\
            fprintf(nla_gl.nlag_trace_fd, "%-50s-%3d-  ", __FUNCTION__, __LINE__);\
        }\
        fprintf(nla_gl.nlag_trace_fd, __VA_ARGS__);\
        fprintf(nla_gl.nlag_trace_fd, "\n");\
    }\
}


#ifdef __cplusplus
}
#endif

#endif /* _NLA_DEFS_H */
