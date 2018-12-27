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

/* libyaml */
#include <yaml.h>

/* nla header files. */
#include <nla_fpm.h>
#include <nla_defs.h>
#include <nla_externs.h>


#define NODE_VAL(node) ((const char *)(node)->data.scalar.value)


static int
nla_yaml_get_module_id (yaml_document_t *document, int i)
{
    yaml_node_t *node;

    node = yaml_document_get_node(document, i);
    if (!node) {
        nla_log(LOG_INFO, "Failed to get node [%d]", i);
        return -1;
    }

    return nla_trace_bit(nla_modules, NODE_VAL(node));
}


static int
nla_yaml_module_init (int module)
{
    int i;

    nla_infa_modules[module].nlam_config.nlamc_enable = true;
    nla_infa_modules[module].nlam_config.nlamc_addr   = NULL;
    nla_infa_modules[module].nlam_config.nlamc_port   = NLA_INVALID;

    for (i = 0; i < NLA_MODULE_ALL; i++) {
        nla_infa_modules[module].nlam_config.nlamc_notify_me[i] = false;
    }

    /* Init policy */
    for (i = 0; i < NLAP_MAX; i++) {
        nla_infa_modules[module].nlam_config.nlamc_policy[i].nlap_entries = 0;
    }

    return 0;
}


static int
nla_yaml_set_policy (yaml_document_t *document,
                     int i,
                     int module,
                     nla_policy_type_t policy_type)
{
    yaml_node_t *node;
    nla_policy_t *policy;

    node = yaml_document_get_node(document, i);
    if (!node) {
        nla_log(LOG_INFO, "Failed to get node [%d]", i);
        return -1;
    }

    policy = &nla_infa_modules[module].nlam_config.nlamc_policy[policy_type];

    if (policy->nlap_entries < NLA_POLICY_ENTRIES_MAX) {
        policy->nlap_value[policy->nlap_entries++] = strtol(NODE_VAL(node), NULL, 10);
    }

    return 0;
}


static int
nla_yaml_set_addr_str (yaml_document_t *document, int i, int module)
{
    yaml_node_t *node;

    node = yaml_document_get_node(document, i);
    if (!node) {
        nla_log(LOG_INFO, "Failed to get node [%d]", i);
        return -1;
    }

    nla_infa_modules[module].nlam_config.nlamc_addr = strdup(NODE_VAL(node));

    return 0;
}


static int
nla_yaml_set_port (yaml_document_t *document, int i, int module)
{
    yaml_node_t *node;

    node = yaml_document_get_node(document, i);
    if (!node) {
        nla_log(LOG_INFO, "Failed to get node [%d]", i);
        return -1;
    }

    nla_infa_modules[module].nlam_config.nlamc_port =
        strtol(NODE_VAL(node), NULL, 10);

    return 0;
}


static int
nla_yaml_set_notify_events_from (yaml_document_t *document, int i, int module)
{
    yaml_node_t *node;
    int from;

    node = yaml_document_get_node(document, i);
    if (!node) {
        nla_log(LOG_INFO, "Failed to get node [%d]", i);
        return -1;
    }

    from = nla_yaml_get_module_id(document, i);
    if (from  < 0) {
        nla_log(LOG_INFO, "Failed to get module id for : %s", NODE_VAL(node));
        return -1;
    }

    nla_infa_modules[module].nlam_config.nlamc_notify_me[from] = true;

    return 0;
}


void
nla_cleanup_config ()
{
    int i;

    nla_log(LOG_INFO, " ");

    for (i = 0; i < NLA_MODULE_ALL; i++) {

        if (nla_infa_modules[i].nlam_config.nlamc_addr) {
            free(nla_infa_modules[i].nlam_config.nlamc_addr);
            nla_infa_modules[i].nlam_config.nlamc_addr = NULL;
        }

        memset(&nla_infa_modules[i].nlam_config, 0, sizeof(nla_module_config_t));
    }
}


static void
nla_dump_config ()
{
    int i, j;
    nla_policy_t *policy;

    nla_log0(LOG_NOTICE, "\n---- MODULE CONFIGURATION");

    for (i = 0; i < NLA_MODULE_ALL; i++) {

        if (!nla_infa_modules[i].nlam_config.nlamc_enable) {
            continue;
        }

        nla_log0(LOG_NOTICE, "---> module         : %s", MODULE(i));

        if (nla_infa_modules[i].nlam_config.nlamc_addr) {
            nla_log0(LOG_NOTICE, "     server-address : %s ",
                    nla_infa_modules[i].nlam_config.nlamc_addr);
        }

        if (nla_infa_modules[i].nlam_config.nlamc_port != NLA_INVALID) {
            nla_log0(LOG_NOTICE, "     server-port    : %d",
                    nla_infa_modules[i].nlam_config.nlamc_port);
        }

        policy = nla_infa_modules[i].nlam_config.nlamc_policy;
        nla_log0(LOG_NOTICE, "     policy :");
        /* filter */
        for (j = 0; j < policy[NLAP_FILTER_FAMILY].nlap_entries; j++) {
            nla_log0(LOG_NOTICE, "         filter-family      : %d",
                    policy[NLAP_FILTER_FAMILY].nlap_value[j]);
        }

        for (j = 0; j < policy[NLAP_FILTER_TABLE].nlap_entries; j++) {
            nla_log0(LOG_NOTICE, "         filter-table       : %d",
                    policy[NLAP_FILTER_TABLE].nlap_value[j]);
        }

        for (j = 0; j < policy[NLAP_FILTER_PROTOCOL].nlap_entries; j++) {
            nla_log0(LOG_NOTICE, "         filter-protocol    : %d",
                    policy[NLAP_FILTER_PROTOCOL].nlap_value[j]);
        }

        /* set */
        for (j = 0; j < policy[NLAP_SET_TABLE].nlap_entries; j++) {
            nla_log0(LOG_NOTICE, "         set-table          : %d",
                    policy[NLAP_SET_TABLE].nlap_value[j]);
        }

        for (j = 0; j < policy[NLAP_SET_PROTOCOL].nlap_entries; j++) {
            nla_log0(LOG_NOTICE, "         set-protocol       : %d",
                    policy[NLAP_SET_PROTOCOL].nlap_value[j]);
        }

        /* strip attributes */
        for (j = 0; j < policy[NLAP_STRIP_RTATTR].nlap_entries; j++) {
            nla_log0(LOG_NOTICE, "         strip-rtattr       : %d",
                    policy[NLAP_STRIP_RTATTR].nlap_value[j]);
        }

        nla_log0(LOG_NOTICE, "     notify-me :");
        for (j = 0; j < NLA_MODULE_ALL; j++) {

            if (!nla_infa_modules[i].nlam_config.nlamc_notify_me[j]) {
                continue;
            }

            nla_log0(LOG_NOTICE, "         notify-events-from : %s", MODULE(j));
        }
        nla_log0(LOG_NOTICE, " ");
    }
}


static void
nla_yaml_default_config ()
{
    FILE *cf;

    cf = fopen(nla_gl.nlag_config_file, "r");
    if (cf) {
        fclose(cf);
        return;
    }

    nla_log(LOG_INFO, "creating default config.");

    cf = fopen(nla_gl.nlag_config_file, "w");
    if (!cf) {
        nla_log0(LOG_ERR, "Failed to create default config file %s!", nla_gl.nlag_config_file);
        return;
    }

    fprintf(cf, "nlagent-modules :"
                "\n    - module         : NLA_KNLM"
                "\n"
                "\n    - module         : NLA_PRPD_CLIENT"
                "\n      server-address : 127.0.0.1"
                "\n      server-port    : 40051"
                "\n      notify-me :"
                "\n          - notify-events-from : NLA_FPM_CLIENT"
                "\n"
                "\n    - module         : NLA_FPM_CLIENT"
                "\n      server-address : 127.0.0.1"
                "\n      server-port    : 2620"
                "\n      policy :"
                "\n          - filter-protocol : 22"
                "\n          - set-protocol    : 0"
                "\n          - strip-rtattr    : 7"
                "\n          - strip-rtattr    : 12"
                "\n          - strip-rtattr    : 15"
                "\n          - strip-rtattr    : 20"
                "\n      notify-me :"
                "\n          - notify-events-from : NLA_KNLM"
                );
    fclose(cf);
}


int
nla_read_config ()
{
    FILE *cf;
    yaml_parser_t parser;
    yaml_document_t document;
    yaml_node_t *node;
    int i;
    int ret = -1;
    int module_id = 0;

    nla_log(LOG_INFO, " ");

    nla_yaml_default_config();

    cf = fopen(nla_gl.nlag_config_file, "r");
    if (!cf) {
        nla_log0(LOG_ERR, "Failed to open config file %s !", nla_gl.nlag_config_file);
        return -1;
    }

    /* Initialize parser */
    if (!yaml_parser_initialize(&parser)) {
        nla_log0(LOG_ERR, "Failed to initialize yaml parser !");
        return -1;
    }

    /* Set input file */
    yaml_parser_set_input_file(&parser, cf);

    if (!yaml_parser_load(&parser, &document)) {
        nla_log0(LOG_ERR, "Failed to load yaml parser !");
        goto done;
    }

    i = 1;
    while(true) {
        node = yaml_document_get_node(&document, i);
        if (!node) {
            break;
        }

        /* Get the data node index */
        i++;
        switch (node->type) {
        case YAML_SCALAR_NODE:

             /* printf(nla_gl.nlag_trace_fd, "SCALAR [%d]: %s\n", i, NODE_VAL(node)); */

             if (!strcmp("module", NODE_VAL(node))) {
                 module_id = nla_yaml_get_module_id(&document, i);
                 if (module_id < 0 ) {
                     nla_log(LOG_INFO, "Failed to get module id for : %s\n", NODE_VAL(node));
                     goto failed;
                 }
                 nla_yaml_module_init(module_id);
             }

             if (!strcmp("server-address", NODE_VAL(node))) {
                 nla_yaml_set_addr_str(&document, i, module_id);
             }

             if (!strcmp("server-port", NODE_VAL(node))) {
                 nla_yaml_set_port(&document, i, module_id);
             }

             if (!strcmp("notify-events-from", NODE_VAL(node))) {
                 if (nla_yaml_set_notify_events_from(&document, i, module_id) < 0) {
                     nla_log(LOG_INFO, "Failed to set %s", NODE_VAL(node));
                     goto failed;
                 }
             }

             /* Process policy configs */

             /* filter */
             if (!strcmp("filter-family", NODE_VAL(node))) {
                 nla_yaml_set_policy(&document, i, module_id, NLAP_FILTER_FAMILY);
             }

             if (!strcmp("filter-table", NODE_VAL(node))) {
                 nla_yaml_set_policy(&document, i, module_id, NLAP_FILTER_TABLE);
             }

             if (!strcmp("filter-protocol", NODE_VAL(node))) {
                 nla_yaml_set_policy(&document, i, module_id, NLAP_FILTER_PROTOCOL);
             }

             /* set */
             if (!strcmp("set-table", NODE_VAL(node))) {
                 nla_yaml_set_policy(&document, i, module_id, NLAP_SET_TABLE);
             }

             if (!strcmp("set-protocol", NODE_VAL(node))) {
                 nla_yaml_set_policy(&document, i, module_id, NLAP_SET_PROTOCOL);
             }

             /* strip attributes */
             if (!strcmp("strip-rtattr", NODE_VAL(node))) {
                 nla_yaml_set_policy(&document, i, module_id, NLAP_STRIP_RTATTR);
             }

            break;

        default:
            break;
        }
    }

    ret = 0;

    nla_dump_config();

failed:
    yaml_document_delete(&document);

done:
    yaml_parser_delete(&parser);

    fclose(cf);

    return ret;
}

