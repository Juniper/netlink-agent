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

#ifndef _NLA_EXTERNS_H
#define _NLA_EXTERNS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * nla_config.c
 */
int nla_read_config();

void nla_cleanup_config();


/*
 * nla_fpm_server.c
 */
nla_module_vector_t* nla_fpm_server_get_vec();


/*
 * nla_fpm_client.c
 */
nla_module_vector_t* nla_fpm_client_get_vec();


/*
 * nla_grpc.cc
 */
int RibClientInit(char *ribServerAddr);

void RibClientReset();

int RibClientAddRoute(struct rtnl_route *route);

int RibClientRemoveRoute(struct rtnl_route *route);


/*
 * nla_kernel.c
 */
nla_module_vector_t* nla_knlm_get_vec();


/*
 * nla_main.c
 */
nla_infra_vector_t* nla_infra_get_vec(void);


/*
 * nla_nlm_server.c
 */
nla_module_vector_t* nla_nlm_server_get_vec();


/*
 * nla_nlm_client.c
 */
nla_module_vector_t* nla_nlm_client_get_vec();


/*
 * nla_policy.c
 */
nla_event_info_t* nla_policy_evaluate(int module, nla_event_info_t *in_evinfo);


/*
 * nla_prpdc.c
 */
nla_module_vector_t* nla_prpdc_get_vec();


/*
 * nla_util.c
 */
fpm_msg_hdr_t* nla_build_fpm_hdr(size_t data_len);

void nla_fpmmsg_dump(const void *msg, unsigned int msg_len);

void nla_nlmsg_dump(const void *msg, unsigned int msg_len);

void nla_nlmsg_walk(const void *msg, int msg_len,
                    void (*nlmsg_cb)(const void *msg, unsigned int msg_len));

void nla_fpm_msg_walk(const void *msg, int msg_len,
                     void (*fpmmsg_cb)(const void *msg, unsigned int msg_len),
                     void (*nlmsg_cb)(const void *msg, unsigned int msg_len));

const char *nla_trace_bits(const bits *bp, unsigned int bit);

const char *nla_trace_state(const bits *bp, unsigned int bit);

unsigned int nla_trace_bit(const bits * types, const char * name);

nla_event_info_t* nla_event_info_clone(nla_event_info_t *evinfo);

void nla_event_info_free(nla_event_info_t *evinfo);

void nla_context_cleanup(nla_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* _NLA_EXTERNS_H */
