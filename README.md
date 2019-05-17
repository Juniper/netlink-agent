# Netlink-agent
Netlink agent in a nutshell deals with Netlink messages.
It can interact with multiple external entities such as Linux kernel, JUNOS routing daemon, TCP based clients/servers, FPM  clients/servers (example  ONOS and Microsoft SONiC) to enable exchange of Netlink messages between them.
It acts an agent to these external entities to aid them in converting Netlink messages to their native format and vice versa.



# Netlink-agent high level design.
* Netlink agent modules are designed like Lego blocks, and how they are arranged/configured defines the functionality of the Netlink agent.
   User can specify the behavior of Netlink agent daemon through a [yaml based configuration file](utils/nlagent.yaml), during Netlink agent daemon bring up time
* All Modules talk to each other In Netlink message format
* Each Module can register to listen write Events from other modules
* Underlying infra takes care of 
   - dispatching write events to all other modules which have registered for event from the module.
   - Connection tracking and 
   - Requesting flash from modules based on Connection state



# Netlink-agent Modules
## KERNEL Module
Interacts with Kernel
- listens to route updates from Linux kernel over Netlink socket
- Can send route updates to Linux kernel over Netlink socket

## PRPD Client
Talks to JUNOS routing daemon (RPD) using GRPC +  Protobuff semantics*
- Add Routes to JUNOS routing daemon (RPD)
- Can be enhanced Listen to Route Flash from JUNOS routing daemon (RPD).

## FPM Client
Fib Push/Pull Manager client
- Establish connection with FPM server
- Send data to FPM server with FPM header
- Receive data from FPM server, and strip of FPM header

## FPM Server
Fib Push/Pull Manager Server
- Establish connection with FPM client
- Send data to FPM client with FPM header
- Receive data from FPM client, and strip of FPM header

## NLM Client
Netlink client
- Establish connection with Netlink server
- Send Netlink messages to Netlink server.
- Receive data from Netlink server

## NLM Server
Netlink Server
- Establish connection with Netlink client
- Send Netlink messages to client.
- Receive data from Netlink client



