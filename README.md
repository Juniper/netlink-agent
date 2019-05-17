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

# Netlink-agent module Interaction
![Module interaction](https://user-images.githubusercontent.com/20463666/57955144-e4815c80-78a9-11e9-88d3-2943b2c35966.png)


# Netlink-agent Modules
### KERNEL Module
Interacts with Kernel
- listens to route updates from Linux kernel over Netlink socket
- Can send route updates to Linux kernel over Netlink socket

### PRPD Client
Talks to JUNOS routing daemon (RPD) using GRPC +  Protobuff semantics*
- Add Routes to JUNOS routing daemon (RPD)
- Can be enhanced Listen to Route Flash from JUNOS routing daemon (RPD).

### FPM Client
Fib Push/Pull Manager client
- Establish connection with FPM server
- Send data to FPM server with FPM header
- Receive data from FPM server, and strip of FPM header

### FPM Server
Fib Push/Pull Manager Server
- Establish connection with FPM client
- Send data to FPM client with FPM header
- Receive data from FPM client, and strip of FPM header

### NLM Client
Netlink client
- Establish connection with Netlink server
- Send Netlink messages to Netlink server.
- Receive data from Netlink server

### NLM Server
Netlink Server
- Establish connection with Netlink client
- Send Netlink messages to client.
- Receive data from Netlink client



# Demo
## [yaml configuration file](utils/nlagent_e2e_test.yaml)
```
nlagent-modules :

    - module         : NLA_KNLM

    - module         : NLA_NLM_CLIENT
      server-address : 127.0.0.1
      server-port    : 11111
      notify-me :
          - notify-events-from : NLA_KNLM

    - module         : NLA_FPM_CLIENT
      server-address : 127.0.0.1
      server-port    : 22222
      notify-me :
          - notify-events-from : NLA_NLM_SERVER

    - module         : NLA_FPM_SERVER
      server-address : 127.0.0.1
      server-port    : 22222
      notify-me :
          - notify-events-from : NLA_FPM_SERVER

    - module         : NLA_NLM_SERVER
      server-address : 127.0.0.1
      server-port    : 11111
      notify-me :
          - notify-events-from : NLA_FPM_CLIENT


    - module         : NLA_PRPD_CLIENT
      server-address : 10.102.177.82
      server-port    : 40051
      notify-me :
          - notify-events-from : NLA_NLM_CLIENT
```


![Demo](https://user-images.githubusercontent.com/20463666/57955747-86557900-78ab-11e9-9d07-b15b034a49fc.png)


# Build & Run
## Build
Netlink agent is built when you run "make" in [Source directory](https://github.com/Juniper/netlink-agent).
## images
Once the build is finished, release images can be found under ship directory. The build process produces 
  * A Netlink agent daemon, which can be run directly
  * A Netlink agent docker container which can be used to run it on kubernetes using a [kubernetes Deployment yaml file](utils/kubectl.yaml)

* Sample build log:
```
netlink-agent> ls 
build
Makefile
nla_config.c
nla_defs.h
nla_externs.h
nla_fpm_client.c
nla_fpm.h
nla_fpm_server.c
nla_grpc.c
nla_knlm.c
nla_main.c
nla_nlm_client.c
nla_nlm_server.c
nla_policy.c
nla_prpd_client.c
nla_util.c
protos
ship
utils


netlink-agent> make
Creating directories
Beginning release build
Creating: protos/authentication_service.proto -> build/release/authentication_service.pb.cc
protoc -I protos --cpp_out=build/release protos/authentication_service.proto
         Create time: 00:00:00
Compiling: build/release/authentication_service.pb.cc -> build/release/authentication_service.pb.o
g++  -std=c++11 -Wall -Wextra -g -Wunused-parameter -Igrpc -O0 -I/usr/include/libnl3   -D NDEBUG -I . -I build/release -c build/release/authentication_service.pb.cc -o build/release/authentication_service.pb.o
         Compile time: 00:00:05
Creating: protos/jnx_addr.proto -> build/release/jnx_addr.pb.cc
protoc -I protos --cpp_out=build/release protos/jnx_addr.proto
         Create time: 00:00:00
Compiling: build/release/jnx_addr.pb.cc -> build/release/jnx_addr.pb.o
g++  -std=c++11 -Wall -Wextra -g -Wunused-parameter -Igrpc -O0 -I/usr/include/libnl3   -D NDEBUG -I . -I build/release -c build/release/jnx_addr.pb.cc -o build/release/jnx_addr.pb.o
         Compile time: 00:00:02
Creating: protos/prpd_common.proto -> build/release/prpd_common.pb.cc
protoc -I protos --cpp_out=build/release protos/prpd_common.proto
         Create time: 00:00:00
Compiling: build/release/prpd_common.pb.cc -> build/release/prpd_common.pb.o
g++  -std=c++11 -Wall -Wextra -g -Wunused-parameter -Igrpc -O0 -I/usr/include/libnl3   -D NDEBUG -I . -I build/release -c build/release/prpd_common.pb.cc -o build/release/prpd_common.pb.o
         Compile time: 00:00:04
Creating: protos/prpd_service.proto -> build/release/prpd_service.pb.cc
protoc -I protos --cpp_out=build/release protos/prpd_service.proto
         Create time: 00:00:00
Compiling: build/release/prpd_service.pb.cc -> build/release/prpd_service.pb.o
g++  -std=c++11 -Wall -Wextra -g -Wunused-parameter -Igrpc -O0 -I/usr/include/libnl3   -D NDEBUG -I . -I build/release -c build/release/prpd_service.pb.cc -o build/release/prpd_service.pb.o
         Compile time: 00:00:03
Creating: protos/rib_service.proto -> build/release/rib_service.pb.cc
protoc -I protos --cpp_out=build/release protos/rib_service.proto
         Create time: 00:00:00
Compiling: build/release/rib_service.pb.cc -> build/release/rib_service.pb.o
g++  -std=c++11 -Wall -Wextra -g -Wunused-parameter -Igrpc -O0 -I/usr/include/libnl3   -D NDEBUG -I . -I build/release -c build/release/rib_service.pb.cc -o build/release/rib_service.pb.o
         Compile time: 00:00:05
Creating: protos/authentication_service.proto -> build/release/authentication_service.grpc.pb.cc
protoc -I protos --grpc_out=build/release --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` protos/authentication_service.proto
         Create time: 00:00:00
Compiling: build/release/authentication_service.grpc.pb.cc -> build/release/authentication_service.grpc.pb.o
g++  -std=c++11 -Wall -Wextra -g -Wunused-parameter -Igrpc -O0 -I/usr/include/libnl3   -D NDEBUG -I . -I build/release -c build/release/authentication_service.grpc.pb.cc -o build/release/authentication_service.grpc.pb.o
         Compile time: 00:00:04
Creating: protos/jnx_addr.proto -> build/release/jnx_addr.grpc.pb.cc
protoc -I protos --grpc_out=build/release --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` protos/jnx_addr.proto
         Create time: 00:00:00
Compiling: build/release/jnx_addr.grpc.pb.cc -> build/release/jnx_addr.grpc.pb.o
g++  -std=c++11 -Wall -Wextra -g -Wunused-parameter -Igrpc -O0 -I/usr/include/libnl3   -D NDEBUG -I . -I build/release -c build/release/jnx_addr.grpc.pb.cc -o build/release/jnx_addr.grpc.pb.o
         Compile time: 00:00:02
Creating: protos/prpd_common.proto -> build/release/prpd_common.grpc.pb.cc
protoc -I protos --grpc_out=build/release --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` protos/prpd_common.proto
         Create time: 00:00:00
Compiling: build/release/prpd_common.grpc.pb.cc -> build/release/prpd_common.grpc.pb.o
g++  -std=c++11 -Wall -Wextra -g -Wunused-parameter -Igrpc -O0 -I/usr/include/libnl3   -D NDEBUG -I . -I build/release -c build/release/prpd_common.grpc.pb.cc -o build/release/prpd_common.grpc.pb.o
         Compile time: 00:00:03
Creating: protos/prpd_service.proto -> build/release/prpd_service.grpc.pb.cc
protoc -I protos --grpc_out=build/release --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` protos/prpd_service.proto
         Create time: 00:00:00
Compiling: build/release/prpd_service.grpc.pb.cc -> build/release/prpd_service.grpc.pb.o
g++  -std=c++11 -Wall -Wextra -g -Wunused-parameter -Igrpc -O0 -I/usr/include/libnl3   -D NDEBUG -I . -I build/release -c build/release/prpd_service.grpc.pb.cc -o build/release/prpd_service.grpc.pb.o
         Compile time: 00:00:03
Creating: protos/rib_service.proto -> build/release/rib_service.grpc.pb.cc
protoc -I protos --grpc_out=build/release --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` protos/rib_service.proto
         Create time: 00:00:00
Compiling: build/release/rib_service.grpc.pb.cc -> build/release/rib_service.grpc.pb.o
g++  -std=c++11 -Wall -Wextra -g -Wunused-parameter -Igrpc -O0 -I/usr/include/libnl3   -D NDEBUG -I . -I build/release -c build/release/rib_service.grpc.pb.cc -o build/release/rib_service.grpc.pb.o
         Compile time: 00:00:06
Compiling: nla_main.c -> build/release/nla_main.o
g++  -std=c++11 -Wall -Wextra -g -Wunused-parameter -Igrpc -O0 -I/usr/include/libnl3   -D NDEBUG -I . -I build/release -MP -MMD -c nla_main.c -o build/release/nla_main.o
         Compile time: 00:00:01
Compiling: nla_util.c -> build/release/nla_util.o
g++  -std=c++11 -Wall -Wextra -g -Wunused-parameter -Igrpc -O0 -I/usr/include/libnl3   -D NDEBUG -I . -I build/release -MP -MMD -c nla_util.c -o build/release/nla_util.o
         Compile time: 00:00:00
Compiling: nla_prpd_client.c -> build/release/nla_prpd_client.o
g++  -std=c++11 -Wall -Wextra -g -Wunused-parameter -Igrpc -O0 -I/usr/include/libnl3   -D NDEBUG -I . -I build/release -MP -MMD -c nla_prpd_client.c -o build/release/nla_prpd_client.o
         Compile time: 00:00:00
Compiling: nla_policy.c -> build/release/nla_policy.o
g++  -std=c++11 -Wall -Wextra -g -Wunused-parameter -Igrpc -O0 -I/usr/include/libnl3   -D NDEBUG -I . -I build/release -MP -MMD -c nla_policy.c -o build/release/nla_policy.o
         Compile time: 00:00:00
Compiling: nla_nlm_server.c -> build/release/nla_nlm_server.o
g++  -std=c++11 -Wall -Wextra -g -Wunused-parameter -Igrpc -O0 -I/usr/include/libnl3   -D NDEBUG -I . -I build/release -MP -MMD -c nla_nlm_server.c -o build/release/nla_nlm_server.o
         Compile time: 00:00:01
Compiling: nla_nlm_client.c -> build/release/nla_nlm_client.o
g++  -std=c++11 -Wall -Wextra -g -Wunused-parameter -Igrpc -O0 -I/usr/include/libnl3   -D NDEBUG -I . -I build/release -MP -MMD -c nla_nlm_client.c -o build/release/nla_nlm_client.o
         Compile time: 00:00:00
Compiling: nla_knlm.c -> build/release/nla_knlm.o
g++  -std=c++11 -Wall -Wextra -g -Wunused-parameter -Igrpc -O0 -I/usr/include/libnl3   -D NDEBUG -I . -I build/release -MP -MMD -c nla_knlm.c -o build/release/nla_knlm.o
         Compile time: 00:00:00
Compiling: nla_grpc.c -> build/release/nla_grpc.o
g++  -std=c++11 -Wall -Wextra -g -Wunused-parameter -Igrpc -O0 -I/usr/include/libnl3   -D NDEBUG -I . -I build/release -MP -MMD -c nla_grpc.c -o build/release/nla_grpc.o
         Compile time: 00:00:07
Compiling: nla_fpm_server.c -> build/release/nla_fpm_server.o
g++  -std=c++11 -Wall -Wextra -g -Wunused-parameter -Igrpc -O0 -I/usr/include/libnl3   -D NDEBUG -I . -I build/release -MP -MMD -c nla_fpm_server.c -o build/release/nla_fpm_server.o
         Compile time: 00:00:00
Compiling: nla_fpm_client.c -> build/release/nla_fpm_client.o
g++  -std=c++11 -Wall -Wextra -g -Wunused-parameter -Igrpc -O0 -I/usr/include/libnl3   -D NDEBUG -I . -I build/release -MP -MMD -c nla_fpm_client.c -o build/release/nla_fpm_client.o
         Compile time: 00:00:00
Compiling: nla_config.c -> build/release/nla_config.o
g++  -std=c++11 -Wall -Wextra -g -Wunused-parameter -Igrpc -O0 -I/usr/include/libnl3   -D NDEBUG -I . -I build/release -MP -MMD -c nla_config.c -o build/release/nla_config.o
         Compile time: 00:00:00
Linking: ship/release/nlagent
g++ build/release/authentication_service.pb.o build/release/jnx_addr.pb.o build/release/prpd_common.pb.o build/release/prpd_service.pb.o build/release/rib_service.pb.o build/release/authentication_service.grpc.pb.o build/release/jnx_addr.grpc.pb.o build/release/prpd_common.grpc.pb.o build/release/prpd_service.grpc.pb.o build/release/rib_service.grpc.pb.o build/release/nla_main.o build/release/nla_util.o build/release/nla_prpd_client.o build/release/nla_policy.o build/release/nla_nlm_server.o build/release/nla_nlm_client.o build/release/nla_knlm.o build/release/nla_grpc.o build/release/nla_fpm_server.o build/release/nla_fpm_client.o build/release/nla_config.o  -L/usr/local/lib -lpthread -lprotobuf -Wl,--no-as-needed -lgrpc++_reflection -lgrpc++ -lgrpc -Wl,--as-needed -ldl -lz -levent -lyaml -lnl-route-3 -lnl-3    -o ship/release/nlagent
         Link time: 00:00:01
Packaging: ship/release/nlagent-docker.tar
`/lib/x86_64-linux-gnu/libpthread.so.0' -> `build/release/nlagent-docker/lib/libpthread.so.0'
`/usr/local/lib/libprotobuf.so.17' -> `build/release/nlagent-docker/lib/libprotobuf.so.17'
`/usr/local/lib/libgrpc++_reflection.so.1' -> `build/release/nlagent-docker/lib/libgrpc++_reflection.so.1'
`/usr/local/lib/libgrpc++.so.1' -> `build/release/nlagent-docker/lib/libgrpc++.so.1'
`/usr/local/lib/libgrpc.so.6' -> `build/release/nlagent-docker/lib/libgrpc.so.6'
`/usr/lib/libevent-2.0.so.5' -> `build/release/nlagent-docker/lib/libevent-2.0.so.5'
`/usr/lib/x86_64-linux-gnu/libyaml-0.so.2' -> `build/release/nlagent-docker/lib/libyaml-0.so.2'
`/usr/lib/libnl-route-3.so.200' -> `build/release/nlagent-docker/lib/libnl-route-3.so.200'
`/usr/lib/libnl-3.so.200' -> `build/release/nlagent-docker/lib/libnl-3.so.200'
`/usr/lib/x86_64-linux-gnu/libstdc++.so.6' -> `build/release/nlagent-docker/lib/libstdc++.so.6'
`/lib/x86_64-linux-gnu/libgcc_s.so.1' -> `build/release/nlagent-docker/lib/libgcc_s.so.1'
`/lib/x86_64-linux-gnu/libc.so.6' -> `build/release/nlagent-docker/lib/libc.so.6'
`/usr/local/lib/libz.so.1' -> `build/release/nlagent-docker/lib/libz.so.1'
`/lib/x86_64-linux-gnu/librt.so.1' -> `build/release/nlagent-docker/lib/librt.so.1'
`/lib/x86_64-linux-gnu/libm.so.6' -> `build/release/nlagent-docker/lib/libm.so.6'
/lib64 -> build/release/nlagent-docker/lib64
`/lib64/ld-linux-x86-64.so.2' -> `build/release/nlagent-docker/lib64/ld-linux-x86-64.so.2'
nlagent-docker/
nlagent-docker/config/
nlagent-docker/config/nlagent.yaml
nlagent-docker/Dockerfile
nlagent-docker/lib/
nlagent-docker/lib/libgrpc++.so.1
nlagent-docker/lib/libgcc_s.so.1
nlagent-docker/lib/libpthread.so.0
nlagent-docker/lib/libnl-3.so.200
nlagent-docker/lib/libstdc++.so.6
nlagent-docker/lib/libgrpc++_reflection.so.1
nlagent-docker/lib/libc.so.6
nlagent-docker/lib/libgrpc.so.6
nlagent-docker/lib/libnl-route-3.so.200
nlagent-docker/lib/libm.so.6
nlagent-docker/lib/libevent-2.0.so.5
nlagent-docker/lib/libyaml-0.so.2
nlagent-docker/lib/libprotobuf.so.17
nlagent-docker/lib/libz.so.1
nlagent-docker/lib/librt.so.1
nlagent-docker/lib64/
nlagent-docker/lib64/ld-linux-x86-64.so.2
nlagent-docker/usr/
nlagent-docker/usr/sbin/
nlagent-docker/usr/sbin/nlagent
Making symlink: nlagent -> ship/release/nlagent
Making symlink: nlagent-docker.tar -> ship/release/nlagent-docker.tar
Total build time: 00:00:47

netlink-agent> cd ship/release/
netlink-agent/ship/release> ls
nlagent           
nlagent-docker.tar


```


* Filesystem view once build is done :
```
netlink-agent> tree
+-- build
¦   +-- release
¦       +-- authentication_service.grpc.pb.cc
¦       +-- authentication_service.grpc.pb.h
¦       +-- authentication_service.grpc.pb.o
¦       +-- authentication_service.pb.cc
¦       +-- authentication_service.pb.h
¦       +-- authentication_service.pb.o
¦       +-- jnx_addr.grpc.pb.cc
¦       +-- jnx_addr.grpc.pb.h
¦       +-- jnx_addr.grpc.pb.o
¦       +-- jnx_addr.pb.cc
¦       +-- jnx_addr.pb.h
¦       +-- jnx_addr.pb.o
¦       +-- nla_config.d
¦       +-- nla_config.o
¦       +-- nla_fpm_client.d
¦       +-- nla_fpm_client.o
¦       +-- nla_fpm_server.d
¦       +-- nla_fpm_server.o
¦       +-- nla_grpc.d
¦       +-- nla_grpc.o
¦       +-- nla_knlm.d
¦       +-- nla_knlm.o
¦       +-- nla_main.d
¦       +-- nla_main.o
¦       +-- nla_nlm_client.d
¦       +-- nla_nlm_client.o
¦       +-- nla_nlm_server.d
¦       +-- nla_nlm_server.o
¦       +-- nla_policy.d
¦       +-- nla_policy.o
¦       +-- nla_prpd_client.d
¦       +-- nla_prpd_client.o
¦       +-- nla_util.d
¦       +-- nla_util.o
¦       +-- prpd_common.grpc.pb.cc
¦       +-- prpd_common.grpc.pb.h
¦       +-- prpd_common.grpc.pb.o
¦       +-- prpd_common.pb.cc
¦       +-- prpd_common.pb.h
¦       +-- prpd_common.pb.o
¦       +-- prpd_service.grpc.pb.cc
¦       +-- prpd_service.grpc.pb.h
¦       +-- prpd_service.grpc.pb.o
¦       +-- prpd_service.pb.cc
¦       +-- prpd_service.pb.h
¦       +-- prpd_service.pb.o
¦       +-- rib_service.grpc.pb.cc
¦       +-- rib_service.grpc.pb.h
¦       +-- rib_service.grpc.pb.o
¦       +-- rib_service.pb.cc
¦       +-- rib_service.pb.h
¦       +-- rib_service.pb.o
¦       +-- nlagent-docker
¦           +-- config
¦           ¦   +-- nlagent.yaml
¦           +-- Dockerfile
¦           +-- lib
¦           ¦   +-- libc.so.6
¦           ¦   +-- libevent-2.0.so.5
¦           ¦   +-- libgcc_s.so.1
¦           ¦   +-- libgrpc++_reflection.so.1
¦           ¦   +-- libgrpc++.so.1
¦           ¦   +-- libgrpc.so.6
¦           ¦   +-- libm.so.6
¦           ¦   +-- libnl-3.so.200
¦           ¦   +-- libnl-route-3.so.200
¦           ¦   +-- libprotobuf.so.17
¦           ¦   +-- libpthread.so.0
¦           ¦   +-- librt.so.1
¦           ¦   +-- libstdc++.so.6
¦           ¦   +-- libyaml-0.so.2
¦           ¦   +-- libz.so.1
¦           +-- lib64
¦           ¦   +-- ld-linux-x86-64.so.2
¦           +-- usr
¦               +-- sbin
¦                   +-- nlagent
+-- Makefile
+-- nla_config.c
+-- nla_defs.h
+-- nla_externs.h
+-- nla_fpm_client.c
+-- nla_fpm.h
+-- nla_fpm_server.c
+-- nlagent -> ship/release/nlagent
+-- nlagent-docker.tar -> ship/release/nlagent-docker.tar
+-- nla_grpc.c
+-- nla_knlm.c
+-- nla_main.c
+-- nla_nlm_client.c
+-- nla_nlm_server.c
+-- nla_policy.c
+-- nla_prpd_client.c
+-- nla_util.c
+-- protos
¦   +-- authentication_service.proto
¦   +-- jnx_addr.proto
¦   +-- prpd_common.proto
¦   +-- prpd_service.proto
¦   +-- rib_service.proto
+-- utils
¦   +-- Dockerfile
¦   +-- kubectl.yaml
¦   +-- nlagent_e2e_test.yaml
¦   +-- nlagent.yaml
+-- ship
    +-- release
        +-- nlagent
        +-- nlagent-docker.tar
```
    
