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




nla_globals_t nla_gl;
nla_module_t nla_infa_modules[NLA_MODULE_ALL];
nla_infra_vector_t nla_infra_vector;


struct timeval start_immediately = {0,0};


static inline bool
nla_is_type_connection_status (nla_event_t event)
{
    if ((event == NLA_CONNECTION_DOWN) || (event == NLA_CONNECTION_UP)) {
        return true;
    }
    return false;
}


static bool
nla_is_module_enabled(int module)
{
    if (!nla_infa_modules[module].nlam_config.nlamc_enable) {
        /* The module is not enabled */
        return false;
    }

    if (!nla_infa_modules[module].nlam_vec) {
        /* The module is not initialised */
        return false;
    }

    return true;
}


static bool
nla_is_module_up (int module)
{
    assert(nla_is_module_enabled(module));

    if (nla_infa_modules[module].nlam_connection_state != NLA_CONNECTION_UP) {
        /* The module connection state is not up */
        return false;
    }

    return true;
}


static void
nla_infra_request_flash (int module)
{
    int i;
    bool init_flash = false;

    if (!nla_is_module_enabled(module)) {
        /* Module is not enabled */
        return;
    }

    if (!nla_is_module_up(module)) {
        return;
    }

    if (!nla_infa_modules[module].nlam_vec->nlamv_init_flash_cb) {
        /* module has't registered a init flash function */
        return;
    }

    /*
     * Initiate flash request only if all the modules which have registered
     * for this module's events are up.
     */
    for (i = 0; i < NLA_MODULE_ALL; i++) {

        if (!nla_is_module_enabled(i)) {
            /* Module is not enabled */
            continue;
        }

        if (nla_infa_modules[i].nlam_config.nlamc_notify_me[module]) {
            /* Found the module which has registered its call back */

            if (!nla_is_module_up(i)) {
                return;
            }
            init_flash = true;
        }
    }

    if (!init_flash) {
        return;
    }

    /* If we are here, then we can go ahead and initiate a flash from this module */
    nla_infa_modules[module].nlam_vec->nlamv_init_flash_cb();

    return;
}


static void
nla_infra_init_flash (int module)
{
    int i;

    /*
     * Initiate flash for all the modules which have registered
     * for this module's events
     */
    nla_infra_request_flash(module);

    /*
     * Request flash from all the modules, for which this module has
     * registered its callbacks
     */
    for (i = 0; i < NLA_MODULE_ALL; i++) {
        if (nla_infa_modules[module].nlam_config.nlamc_notify_me[i]) {
            nla_infra_request_flash(i);
        }
    }

    return;
}


static void
nla_infra_init_module (int module)
{
    if (!nla_infa_modules[module].nlam_vec) {
        return;
    }

    nla_infa_modules[module].nlam_vec->nlamv_init_cb();
}


static void
nla_infra_check_init_module (int module)
{
    int i;

    if (!nla_is_module_enabled(module)) {
        /* Module is not enabled */
        return;
    }

    if (nla_is_module_up(module)) {
        /* Module is already up nothing to do */
        return;
    }

    nla_log(LOG_INFO, "%s", MODULE(module));

    /*
     * Init this module only if all the modules which have registered
     * for this module's events are up.
     */
    for (i = 0; i < NLA_MODULE_ALL; i++) {

        if (!nla_is_module_enabled(i)) {
            /* Module is not enabled */
            continue;
        }

        /* check if module[i] is interested in events from this module */
        if (nla_infa_modules[i].nlam_config.nlamc_notify_me[module]) {
            if (nla_infa_modules[module].nlam_config.nlamc_notify_me[i]) {
                nla_log(LOG_INFO, "%s and %s are interdependent", MODULE(module), MODULE(i));
                continue;
            }

            /* Found the module which has registered its call back */
            if (!nla_is_module_up(i)) {
                nla_log(LOG_INFO, "%s : %s is not up; defer module init", MODULE(module), MODULE(i));
                return;
            }
        }
    }

    /* If we are here, then we can go ahead and init this module */
    nla_infra_init_module(module);

    return;
}


static void
nla_infra_init_all_modules ()
{
    int i;

    nla_log(LOG_INFO, " ");

    for (i = 0; i < NLA_MODULE_ALL; i++) {
        nla_infra_check_init_module(i);
    }
}


static void
nla_infra_reset_module (int module)
{
    if (!nla_infa_modules[module].nlam_vec) {
        return;
    }

    nla_infa_modules[module].nlam_vec->nlamv_reset_cb();
}


static void
nla_infra_modules_reset ()
{
    int i;

    nla_log(LOG_INFO, " ");

    for (i = 0; i < NLA_MODULE_ALL; i++) {
        nla_infra_reset_module(i);
    }

}


static int
nla_infra_process_connection_status_change (nla_module_id_t module,
                                            nla_event_info_t *evinfo)
{
    int i;

    if (!nla_is_type_connection_status((nla_event_t)evinfo->nlaei_type)) {
        return 0;
    }

    if (nla_infa_modules[module].nlam_connection_state == evinfo->nlaei_type) {
        /* No change in connection status */
        return 1;
    }

    nla_infa_modules[module].nlam_connection_state = evinfo->nlaei_type;

    nla_log(LOG_NOTICE, "module %s status %s", MODULE(module), EVENT(evinfo->nlaei_type));

    switch (evinfo->nlaei_type) {
    case NLA_CONNECTION_DOWN:
        nla_log(LOG_WARN, "Something went wrong. lets do a fresh start ");
        evtimer_add(nla_gl.nlag_reinit, &start_immediately);
        break;

    case NLA_CONNECTION_UP:
        /*
         * call Init for all modules from which this module is interested in
         * receiving the events.
         */
        nla_log(LOG_INFO, "module %s : Now Init all the depedent modules", MODULE(module));
        for (i = 0; i < NLA_MODULE_ALL; i++) {
            if (nla_infa_modules[module].nlam_config.nlamc_notify_me[i]) {
                nla_infra_check_init_module(i);
            }
        }

        nla_infra_init_flash(module);
        break;
    default:
        break;
    }

    return 1;
}


static void
nla_infra_event_dispatcher (nla_module_id_t from, nla_event_info_t *evinfo)
{
    int i;
    nla_event_info_t *module_specific_evinfo;

    if (nla_infra_process_connection_status_change(from, evinfo)) {
        /* connection status change event, no need to propogate to other modules */
        return;
    }

    for (i = 0; i < NLA_MODULE_ALL; i++) {

        if (!nla_is_module_enabled(i)) {
            continue;
        }

        if (!nla_infa_modules[i].nlam_config.nlamc_notify_me[from]) {
            /* This module[i] is not configured to receive event from [form] module */
            continue;
        }

        if (!nla_infa_modules[i].nlam_vec->nlamv_notify_cb) {
            /* module has't registered a init flash function */
            continue;
        }

        if (!nla_is_module_up(i)) {
            /* module still not up*/
            continue;
        }

        /* evaluate module specific policies to format the message */
        module_specific_evinfo = nla_policy_evaluate(i, evinfo);
        if (!module_specific_evinfo) {
            nla_log(LOG_INFO, "policy evaluation failed: skip notifying this msg to %s", MODULE(i));
            return;
        }

        nla_log(LOG_INFO, "from %s to %s -> event %s ",
                MODULE(from), MODULE(i), EVENT(module_specific_evinfo->nlaei_type));

        nla_infa_modules[i].nlam_vec->nlamv_notify_cb(from, module_specific_evinfo);

        nla_event_info_free(module_specific_evinfo);
    }
}


static void
nla_infra_register_module (nla_module_id_t module,
                           nla_module_vector_t * (*get_vec_pf) (void))
{
    if (!nla_infa_modules[module].nlam_config.nlamc_enable) {
        return;
    }

    nla_infa_modules[module].nlam_connection_state = NLA_CONNECTION_DOWN;
    nla_infa_modules[module].nlam_vec = get_vec_pf();
}


static void
nla_infra_modules_init ()
{
    nla_log(LOG_INFO, " ");

    nla_infra_register_module(NLA_KNLM,        nla_knlm_get_vec);
    nla_infra_register_module(NLA_PRPD_CLIENT, nla_prpdc_get_vec);
    nla_infra_register_module(NLA_FPM_SERVER,  nla_fpm_server_get_vec);
    nla_infra_register_module(NLA_FPM_CLIENT,  nla_fpm_client_get_vec);
    nla_infra_register_module(NLA_NLM_SERVER,  nla_nlm_server_get_vec);
    nla_infra_register_module(NLA_NLM_CLIENT,  nla_nlm_client_get_vec);

    /* Call init routines of the modules */
    nla_infra_init_all_modules();
}


static void
nla_infra_get_sockaddr (nla_module_id_t module, struct sockaddr_un *un_addr)
{
    struct sockaddr_in in_addr;

    memset(un_addr, 0, sizeof(struct sockaddr_un));

    memset(&in_addr, 0, sizeof(in_addr));

    in_addr.sin_family = AF_INET;
    inet_pton(AF_INET,
              nla_infa_modules[module].nlam_config.nlamc_addr,
              &(in_addr.sin_addr.s_addr));
    in_addr.sin_port = htons(nla_infa_modules[module].nlam_config.nlamc_port);

    memcpy(un_addr, &in_addr, sizeof(struct sockaddr_in));
}


static char *
nla_infra_get_server_addr_str (nla_module_id_t module)
{
    return nla_infa_modules[module].nlam_config.nlamc_addr;
}


static int
nla_infra_get_server_port (nla_module_id_t module)
{
    return nla_infa_modules[module].nlam_config.nlamc_port;
}


static void
nla_infra_vec_init (void)
{
    nla_log(LOG_INFO, " ");

    nla_infra_vector.nlaiv_notify_cb    = nla_infra_event_dispatcher;
    nla_infra_vector.nlaiv_get_sockaddr = nla_infra_get_sockaddr;
    nla_infra_vector.nlaiv_get_addr_str = nla_infra_get_server_addr_str;
    nla_infra_vector.nlaiv_get_port     = nla_infra_get_server_port;
}


nla_infra_vector_t*
nla_infra_get_vec (void)
{
    nla_log(LOG_INFO, " ");
    return &nla_infra_vector;
}


static void
nla_infra_modules_reinit_event (evutil_socket_t fd UNUSED, short what UNUSED, void *arg UNUSED)
{
    /*
     * Alwasy cleanup and do a fresh start.
     */
    nla_log(LOG_INFO, "start cleaning up the modules");
    nla_infra_modules_reset();

    nla_log(LOG_INFO, "start bring up the modules");
    nla_infra_modules_init();
}


static void
nla_infra_libevent_init ()
{
    nla_log(LOG_INFO, " ");

    /* Create an event base */
    nla_gl.nlag_base = event_base_new();
    if (!nla_gl.nlag_base) {
        nla_log(LOG_INFO, "Failed to init libevent");
        exit(0);
    }

    /* Create a reinit event */
    nla_gl.nlag_reinit = evtimer_new(nla_gl.nlag_base, nla_infra_modules_reinit_event, NULL);
    evtimer_add(nla_gl.nlag_reinit, &start_immediately);
}


static void
nla_infra_init ()
{
    nla_log(LOG_INFO, " ");

    /* Module initialisation */
    nla_infra_vec_init();

    /* perform one-time initialization of the libevent library */
    nla_infra_libevent_init();
}


static void
nla_global_init()
{

    /* set version */
    nla_gl.nlag_version = NLA_VERSION;

    /* default config file */
    if (!nla_gl.nlag_config_file) {
        nla_gl.nlag_config_file = strdup("nlagent.yaml");
    }

    /*
     * By default send the traces to stdout, unless a trace ile is
     * explicitly specified
     */
    nla_gl.nlag_trace_fd = stdout;
#if 0
    FILE *fd;
    /* trace file */
    if (nla_gl.nlag_trace_file) {
        fd = fopen(nla_gl.nlag_trace_file, "w");
        if (fd) {
            nla_gl.nlag_trace_fd = fd;
        } else {
            nla_log0(LOG_ERR, "Failed to create trace file %s!", nla_gl.nlag_trace_file);
            return;
        }
    }
#endif
}


static void
nla_global_cleanup()
{
    /* default config file */
    if (nla_gl.nlag_config_file) {
        free(nla_gl.nlag_config_file);
        nla_gl.nlag_config_file = NULL;
    }

    /* trace file */
    if (nla_gl.nlag_trace_file) {
        free(nla_gl.nlag_trace_file);
        nla_gl.nlag_trace_file = NULL;
    }

    if (nla_gl.nlag_trace_fd) {
        fclose(nla_gl.nlag_trace_fd);
        nla_gl.nlag_trace_fd = NULL;
    }

    event_free(nla_gl.nlag_reinit);

    event_base_free(nla_gl.nlag_base);
}


static void
nal_show_version (const int more)
{
    nla_log0(LOG_DEFAULT, "version %d\n", nla_gl.nlag_version);

    if (more) {
        /* display more info*/
    }
}


/*
 * Main entry point
 */
int
main (int argc, char **argv)
{
    int opt;
    int vflag;

    /*
     * Read command line arguments
     */
    while ((opt = getopt(argc, argv, "c:f:t:Nv")) != EOF) {
        switch(opt) {
            case 'c':                                   /* config file */
                nla_gl.nlag_config_file = strdup(optarg);
                break;

            case 'f':                                   /* trace file */
                nla_gl.nlag_trace_file = strdup(optarg);
                break;

            case 'N':                                   /* don't fork a child */
                nla_gl.nlag_dont_daemonize = true;
                break;

            case 't':                                   /* trace level */
                nla_gl.nlag_trace_level = atoi(optarg);
                break;

            case 'v':                                   /* show version */
                vflag += 1;
                break;

            case '?':
            default:                              /* unsupported */
                fprintf(stdout, "usage: nlagent [-v]  [-c config-filename] [-t trace-level -f -trace-filename]\n");
                exit(1);
        }
    }

    nla_global_init();

    /* Process version flag. */
    if (vflag) {
        nal_show_version(vflag);
        exit(0);
    }

    nla_log(LOG_INFO, " ");

    /* Read configuration */
    if (nla_read_config() < 0) {
        nla_log(LOG_INFO, "failed to read config");
        exit(1);
    }

    nla_infra_init();

    /* Wait for events */
    event_base_dispatch(nla_gl.nlag_base);

    /* Cleanup */
    nla_infra_modules_reset();

    nla_cleanup_config();

    nla_global_cleanup();

    fprintf(stdout, "done\n");

    return 0;
}


