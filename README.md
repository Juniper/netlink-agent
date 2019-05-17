# netlink-agent
Netlink agent in a nutshell deals with messages in netlink format.
It can interact with multiple external entities (Linux kernel, Junos routing daemon, FPM based server such as ONOS, Microsoft SONiC) to exchanges the messages.

Netlink agent modules are designed like Lego blocks, and how they are arranged/configured defines the functionality of the Netlink agent.
User can specify the behaviour of Netlink agent daemon through a yaml based configuration file, The same can be passed to the 
deamon during Netlink agent daemon bring up time


# Netlink-agent high level design.
1) All Modules talk to each other In Netlink message format
2) Each Module can register to listen write Events from other modules
3) Underlying infra takes care of 
   a) dispatching write events to all other modules which have registered for event from the module.
   b) Connection tracking and 
   c) Requesting flash from modules based on Connection state 

