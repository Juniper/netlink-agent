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

#include <iostream>
#include <memory>
#include <thread>
#include <assert.h>
#include <chrono>
#include <future>
#include <unistd.h>
#include <fcntl.h>

#include <grpc++/grpc++.h>
#include "authentication_service.grpc.pb.h"
#include "jnx_addr.grpc.pb.h"
#include "prpd_common.grpc.pb.h"
#include "rib_service.grpc.pb.h"
#include "prpd_service.grpc.pb.h"

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
#include <netlink/addr.h>

/* nla header files. */
#include <nla_fpm.h>
#include <nla_defs.h>
#include <nla_externs.h>


using grpc::Channel;
using grpc::ClientAsyncResponseReader;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Status;
using routing::Rib;
using routing::Base;


typedef struct grpc_context_s {
    std::shared_ptr<grpc::Channel> *gc_channel;
    std::thread        *gc_connectionManagerThread;
    std::promise<void> *gc_exitSignal; /* Create a std::promise object */
    std::future<void>  *gc_futureObj;  /* Fetch std::future object associated with promise */
    struct event       *gc_pipeReadEvent;
    int                 gc_pipeFd[2];
} grpc_context_t;


typedef struct nh_arg_s {
    routing::RouteNexthop  *na_rtNh;
    uint32_t                na_nhIndex;
} nh_arg_t;


grpc_context_t grpc_ctx;

extern void nla_prpdc_event_cb(nla_event_t event);


const char *
GetClientId ()
{
    return "nlagent";
}


uint64_t
GetCookie ()
{
    return 1234;
}


uint32_t
GetPurgeTime ()
{
    return 1;
}


int
ConfigRoutePurgeTime ()
{
    routing::Base::Stub           baseChannel(*grpc_ctx.gc_channel);
    routing::RtPurgeConfigRequest rtPurgeCfgReq;
    routing::RtOperReply          rtOperReply;
    grpc::ClientContext           context;
    grpc::Status                  status;

    /*
     * Build ClientContext object
     */
    context.AddMetadata("client-id", GetClientId());

    rtPurgeCfgReq.set_time(GetPurgeTime());

    status = baseChannel.RoutePurgeTimeConfig(&context, rtPurgeCfgReq, &rtOperReply);
    if (!status.ok()) {
        nla_log(LOG_INFO, "Config RPC failed");
        return -1;
    }

    if (rtOperReply.ret_code() != routing::RET_SUCCESS) {
        nla_log(LOG_INFO, "Config failed with status %d", rtOperReply.ret_code());
        return -1;
    }

    nla_log(LOG_INFO, "Config successful");

    return 0;
}


const char *
GetTableName (struct rtnl_route *route)
{
    switch (rtnl_route_get_family(route)) {
    case AF_INET:
        return "inet.0";
    case AF_INET6:
        return "inet6.0";
    }
    return "unknown";
}


void
CreateNetworkAddressFromNladdr (struct nl_addr *nlAddr, routing::NetworkAddress *NetworkAddr)
{
    jnxBase::IpAddress       IpAddr;

    IpAddr.set_addr_bytes((const char*)nl_addr_get_binary_addr(nlAddr), nl_addr_get_len(nlAddr));
    switch (nl_addr_get_family(nlAddr)) {
    case AF_INET:
        NetworkAddr->mutable_inet()->CopyFrom(IpAddr);
        break;
    case AF_INET6:
        NetworkAddr->mutable_inet6()->CopyFrom(IpAddr);
        break;
    default:
        break;
    }
}


void
AddNexthop (struct rtnl_nexthop *rtnh, void *arg)
{
    routing::RouteGateway    rtGw;
    routing::NetworkAddress  rtGwAddr;
    nh_arg_t                *nhArg;
    struct nl_addr          *nlGwaddr;
    struct nl_addr          *nlViaAddr = NULL;
    uint32_t                 ifIndex;

    nhArg = (nh_arg_t *)arg;

    /*
     * Build the Gateway
     */
    nlGwaddr  = rtnl_route_nh_get_gateway(rtnh);
    //nlViaAddr = rtnl_route_nh_get_via(rtnh);
    ifIndex   = rtnl_route_nh_get_ifindex(rtnh);

    if (!nlGwaddr && !nlViaAddr) {
        /* Skip these nexthops, Nothing to do here */
        return;
    }

    if (nlGwaddr) {
        CreateNetworkAddressFromNladdr(nlGwaddr, &rtGwAddr);
        rtGw.mutable_gateway_address()->CopyFrom(rtGwAddr);
    }

    if (nlViaAddr) {
        CreateNetworkAddressFromNladdr(nlViaAddr, &rtGwAddr);
        rtGw.mutable_gateway_address()->CopyFrom(rtGwAddr);
    }

    if (ifIndex) {
        //rtGw.set_interface_name("ens3f0.0");
        //rtnl_link_i2name(link_cache, nh->rtnh_ifindex, buf, sizeof(buf)));
    }

    /*
     * Build the nexthop
     */
    nhArg->na_rtNh->add_gateways();
    nhArg->na_rtNh->mutable_gateways(nhArg->na_nhIndex++)->CopyFrom(rtGw);
}


int
RibClientAddRoute (struct rtnl_route *route)
{
    routing::Rib::Stub            ribChannel(*grpc_ctx.gc_channel);
    routing::RouteMatchFields     rtKey;
    routing::RouteTable           rtTable;
    routing::RouteEntry           rtEntry;
    routing::NetworkAddress       rtPrefix;
    routing::RouteNexthop         rtNh;
    routing::RouteAttributes      rtAttrs;
    routing::RouteAttributeUint32 rtAttrColorVal;
    routing::RouteUpdateRequest   rtUpdateReq;
    routing::RouteOperReply       rtOperReply;
    grpc::ClientContext           context;
    grpc::Status                  status;
    struct nl_addr                *dstAddr;
    uint32_t                      rtPrefixLen;


    /*
     * Build ClientContext object
     */
    context.AddMetadata("client-id", GetClientId());

    /*
     * Build RouteTable object
     */
    rtTable.mutable_rtt_name()->set_name(GetTableName(route));

    /*
     * Build the Prefix object
     */
    dstAddr = rtnl_route_get_dst(route);
    CreateNetworkAddressFromNladdr(dstAddr, &rtPrefix);
    rtPrefixLen = nl_addr_get_prefixlen(dstAddr);


    /*
     * Build the nexthop
     */
    nh_arg_t nhArg;
    nhArg.na_rtNh = &rtNh;
    nhArg.na_nhIndex = 0;
    rtnl_route_foreach_nexthop(route, AddNexthop, &nhArg);


    /*
     * Build RouteMatchFields object
     */
    rtKey.set_cookie(GetCookie());
    rtKey.mutable_table()->CopyFrom(rtTable);
    rtKey.mutable_dest_prefix()->CopyFrom(rtPrefix);
    rtKey.set_dest_prefix_len(rtPrefixLen);


    /*
     * Add color Attribute
     */
    rtAttrColorVal.set_value(100);
    auto& rtAttrColor = *rtAttrs.mutable_colors();
    rtAttrColor[0] = rtAttrColorVal;

    /*
     * Build RouteEntry object
     */
    rtEntry.mutable_key()->CopyFrom(rtKey);
    rtEntry.mutable_nexthop()->CopyFrom(rtNh);
    rtEntry.mutable_attributes()->CopyFrom(rtAttrs);

    /*
     * Build RouteUpdateRequest
     */
    rtUpdateReq.add_routes();
    rtUpdateReq.mutable_routes(0)->CopyFrom(rtEntry);

    status = ribChannel.RouteAdd(&context, rtUpdateReq, &rtOperReply);
    if (!status.ok()) {
        nla_log(LOG_INFO, "RouteAdd RPC failed");
        return -1;
    }

    if (rtOperReply.status() != routing::SUCCESS) {
        nla_log(LOG_INFO, "RouteAdd failed with status %d", rtOperReply.status());
        return 0;
    }

    nla_log(LOG_INFO, "RouteAdd successful");

    return 0;
}


int
RibClientRemoveRoute (struct rtnl_route *route)
{
    routing::Rib::Stub          ribChannel(*grpc_ctx.gc_channel);
    routing::NetworkAddress     rtPrefix;
    routing::RouteTable         rtTable;
    routing::RouteMatchFields   rtKey;
    routing::RouteRemoveRequest rtRemoveReq;
    routing::RouteOperReply     rtOperReply;
    grpc::ClientContext         context;
    struct nl_addr             *dstAddr;
    uint32_t                    rtPrefixLen;

    /*
     * Build ClientContext object
     */
    context.AddMetadata("client-id", GetClientId());

    /*
     * Build RouteTable object
     */
    rtTable.mutable_rtt_name()->set_name(GetTableName(route));

    /*
     * Build the Prefix object
     */
    dstAddr = rtnl_route_get_dst(route);
    CreateNetworkAddressFromNladdr(dstAddr, &rtPrefix);
    rtPrefixLen = nl_addr_get_prefixlen(dstAddr);


    /*
     * Build RouteMatchFields object
     */
    rtKey.set_cookie(GetCookie());
    rtKey.mutable_table()->CopyFrom(rtTable);
    rtKey.mutable_dest_prefix()->CopyFrom(rtPrefix);
    rtKey.set_dest_prefix_len(rtPrefixLen);

    rtRemoveReq.add_keys();
    rtRemoveReq.mutable_keys(0)->CopyFrom(rtKey);

    auto status = ribChannel.RouteRemove(&context, rtRemoveReq, &rtOperReply);
    if (!status.ok()) {
        nla_log(LOG_INFO, "RouteRemove RPC failed");
        return -1;
    }

    if (rtOperReply.status() != routing::SUCCESS) {
        nla_log(LOG_INFO, "RouteRemove failed with status %d", rtOperReply.status());
        return 0;
    }

    nla_log(LOG_INFO, "RouteRemove successful");

    return 0;
}


/**
 * We can retry if the errno is transient.
 */
bool
PipeErrnoRetry (const int err)
{
    switch (err) {
    case EINTR:
    case EAGAIN:
        return true;

    default:
        return false;
    }
}


void
PipeWrite (int pipeWrtiteFd, nla_event_t connectionState)
{
    int len;

    do {
        nla_log(LOG_INFO, "PipeWriteConnectionState");
        errno = 0;
        len = write(pipeWrtiteFd, &connectionState, sizeof(connectionState));
    } while (len < 0 && PipeErrnoRetry(errno));
}


void
PipeRead (evutil_socket_t pipeWrtiteFd, short event UNUSED, void *arg UNUSED)
{
    int len;
    nla_event_t state;

    do {
        nla_log(LOG_INFO, "PipeReadConnectionState");
        errno = 0;
        len = read(pipeWrtiteFd, &state, sizeof(state));
        if (len > 0) {
            nla_prpdc_event_cb(state);
        }
    } while (len < 0 && (errno == EINTR));
}


void
ConnectionManager (int pipeWrtiteFd)
{
    nla_event_t connectionState = NLA_CONNECTION_DOWN;
    nla_event_t newConnectionState = NLA_CONNECTION_DOWN;

    std::chrono::system_clock::time_point deadline;

    nla_log(LOG_INFO, "ConnectionManager init");

    while (grpc_ctx.gc_futureObj->wait_for(std::chrono::milliseconds(1)) == std::future_status::timeout) {

        auto state = (*grpc_ctx.gc_channel)->GetState(false);

        switch (state) {
        case GRPC_CHANNEL_READY:
            nla_log(LOG_INFO, "GRPC_CHANNEL_READY");
            while (ConfigRoutePurgeTime() < 0) {
                nla_log(LOG_INFO, "Login failed");
            }
            nla_log(LOG_INFO, "Login successful");
            newConnectionState = NLA_CONNECTION_UP;
            break;
        default:
            switch (state) {
            case GRPC_CHANNEL_IDLE:
                nla_log(LOG_INFO, "GRPC_CHANNEL_IDLE");
                break;

            case GRPC_CHANNEL_CONNECTING:
                nla_log(LOG_INFO, "GRPC_CHANNEL_CONNECTING");
                break;

            case GRPC_CHANNEL_TRANSIENT_FAILURE:
                nla_log(LOG_INFO, "GRPC_CHANNEL_TRANSIENT_FAILURE");
                break;

            case GRPC_CHANNEL_SHUTDOWN:
                nla_log(LOG_INFO, "GRPC_CHANNEL_SHUTDOWN");
                break;

            default:
                nla_log(LOG_INFO, "default");
                break;
            }
            newConnectionState = NLA_CONNECTION_DOWN;
            /* Try connecting */
            (*grpc_ctx.gc_channel)->GetState(true);
        }

        if (newConnectionState != connectionState) {
            connectionState = newConnectionState;
            PipeWrite(pipeWrtiteFd, connectionState);
        }

        /* Wait for state to change */
        while (grpc_ctx.gc_futureObj->wait_for(std::chrono::milliseconds(1)) == std::future_status::timeout) {
            deadline = std::chrono::system_clock::now() + std::chrono::seconds(1);
            if (!(*grpc_ctx.gc_channel)->WaitForStateChange(state, deadline)) {
                /* nla_log(LOG_INFO, "Timeout while WaitForStateChange"); */
                continue;
            }
            break;
        }
    }

    nla_log(LOG_INFO, "ConnectionManager terminate");
}


int
StartConnectionManager ()
{

    grpc_ctx.gc_pipeFd[0] = -1;
    grpc_ctx.gc_pipeFd[1] = -1;

    /*
     * Create pipe for inter-thread communication
     */
    if (pipe2(grpc_ctx.gc_pipeFd, O_NONBLOCK|O_CLOEXEC) < 0) {
        nla_log(LOG_INFO, "failed to create pipe");
        return -1;
    }

    grpc_ctx.gc_pipeReadEvent = event_new(nla_gl.nlag_base,
                                          grpc_ctx.gc_pipeFd[0],
                                          EV_READ|EV_PERSIST,
                                          PipeRead,
                                          NULL);

    if (event_add(grpc_ctx.gc_pipeReadEvent, NULL) < 0) {
        nla_log(LOG_INFO, "failed to add pipe read event");
        return -1;
    }

    /* Spawn thread that loops indefinitely to keep track of the connection */
    grpc_ctx.gc_connectionManagerThread =
        new std::thread(ConnectionManager, grpc_ctx.gc_pipeFd[1]);

    return 0;
}


void
RibClientReset ()
{
    nla_log(LOG_INFO, "RibClientReset");

    if (grpc_ctx.gc_connectionManagerThread) {
        /* Set the value in promise */
        assert(grpc_ctx.gc_exitSignal);
        grpc_ctx.gc_exitSignal->set_value();

        /* Wait for thread to join */
        grpc_ctx.gc_connectionManagerThread->join();
        delete grpc_ctx.gc_connectionManagerThread;
        grpc_ctx.gc_connectionManagerThread = NULL;
    }

    if (grpc_ctx.gc_exitSignal) {
        delete grpc_ctx.gc_exitSignal;
        grpc_ctx.gc_exitSignal = NULL;
    }

    if (grpc_ctx.gc_futureObj) {
        delete grpc_ctx.gc_futureObj;
        grpc_ctx.gc_futureObj = NULL;
    }

    if (grpc_ctx.gc_channel) {
        delete grpc_ctx.gc_channel;
        grpc_ctx.gc_channel = NULL;
    }

    if (grpc_ctx.gc_pipeReadEvent) {
        event_free(grpc_ctx.gc_pipeReadEvent);
        grpc_ctx.gc_pipeReadEvent = NULL;
    }

    close(grpc_ctx.gc_pipeFd[0]);
    grpc_ctx.gc_pipeFd[0] = -1;

    close(grpc_ctx.gc_pipeFd[1]);
    grpc_ctx.gc_pipeFd[1] = -1;
}


int
RibClientInit (char *ribServerAddr)
{
    /*
     * Instantiate the client. It requires a channel, out of which the actual RPCs
     * are created. This channel models a connection to an endpoint (in this case,
     * localhost at port 50051). We indicate that the channel isn't authenticated
     * (use of InsecureChannelCredentials()).
     */
    grpc_ctx.gc_channel =
        new std::shared_ptr<grpc::Channel>(grpc::CreateChannel(ribServerAddr, grpc::InsecureChannelCredentials()));

    /* Create a std::promise object */
    grpc_ctx.gc_exitSignal = new std::promise<void>;

    /* Fetch std::future object associated with promise */
    grpc_ctx.gc_futureObj =
        new std::future<void>(grpc_ctx.gc_exitSignal->get_future());

    if (StartConnectionManager() < 0) {
        return -1;
    }

    return 0;
}
