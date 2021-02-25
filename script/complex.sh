#!/usr/bin/env bash

set -ex

# P12 <-----+ veth12                             veth22 +-----> P22
#    veth   |                                           |   veth
#  10.0.1.2 |       veth-brd1           veth-brd2       | 10.0.2.2
#           +---> BRD1 <--------> R <--------> BRD2 <---+
#           |               veth1   veth2               |
#           |            10.0.1.1   10.0.2.1            |
# P13 <-----+ veth13                             veth23 +-----> P23
#    veth                                                   veth
#  10.0.1.3                                               10.0.2.3

for NS in P12 P13 P22 P23 R BRD1 BRD2
do
    ip netns delete $NS 2>/dev/null || true
    ip netns add $NS
    ip netns exec $NS ip l set lo up
done

# P12 <-> BRD1
ip l add veth netns P12 type veth peer name veth12 netns BRD1
ip netns exec P12 ip a add 10.0.1.2/24 dev veth
ip netns exec P12 ip l set veth up
ip netns exec P12 ethtool -K veth tx off
ip netns exec P12 ip r add default via 10.0.1.1

ip netns exec BRD1 ip l set veth12 up
ip netns exec BRD1 ethtool -K veth12 tx off

# P13 <-> BRD1
ip l add veth netns P13 type veth peer name veth13 netns BRD1
ip netns exec P13 ip a add 10.0.1.3/24 dev veth
ip netns exec P13 ip l set veth up
ip netns exec P13 ethtool -K veth tx off
ip netns exec P13 ip r add default via 10.0.1.1

ip netns exec BRD1 ip l set veth13 up
ip netns exec BRD1 ethtool -K veth13 tx off

# R <-> BRD1
ip l add veth1 netns R type veth peer name veth-brd1 netns BRD1
# ip netns exec R ip a add 10.0.1.1/24 dev veth1
ip netns exec R ip l set veth1 up
ip netns exec R ethtool -K veth1 tx off

ip netns exec BRD1 ip l set veth-brd1 up
ip netns exec BRD1 ethtool -K veth-brd1 tx off

# bridge P12 & P13
# ip netns exec BRD1 ip l add brd1 type bridge
# ip netns exec BRD1 ip l set brd1 up
# ip netns exec BRD1 ip l set veth12 master brd1
# ip netns exec BRD1 ip l set veth13 master brd1
# ip netns exec BRD1 ip l set veth-brd1 master brd1

# P22 <-> BRD2
ip l add veth netns P22 type veth peer name veth22 netns BRD2
ip netns exec P22 ip a add 10.0.2.2/24 dev veth
ip netns exec P22 ip l set veth up
ip netns exec P22 ethtool -K veth tx off
ip netns exec P22 ip r add default via 10.0.2.1

ip netns exec BRD2 ip l set veth22 up
ip netns exec BRD2 ethtool -K veth22 tx off

# P23 <-> BRD2
ip l add veth netns P23 type veth peer name veth23 netns BRD2
ip netns exec P23 ip a add 10.0.2.3/24 dev veth
ip netns exec P23 ip l set veth up
ip netns exec P23 ethtool -K veth tx off
ip netns exec P23 ip r add default via 10.0.2.1

ip netns exec BRD2 ip l set veth23 up
ip netns exec BRD2 ethtool -K veth23 tx off

# R <-> BRD2
ip l add veth2 netns R type veth peer name veth-brd2 netns BRD2
# ip netns exec R ip a add 10.0.2.1/24 dev veth2
ip netns exec R ip l set veth2 up
ip netns exec R ethtool -K veth2 tx off

ip netns exec BRD2 ip l set veth-brd2 up
ip netns exec BRD2 ethtool -K veth-brd2 tx off

# bridge P12 & P13
# ip netns exec BRD2 ip l add brd2 type bridge
# ip netns exec BRD2 ip l set brd2 up
# ip netns exec BRD2 ip l set veth22 master brd2
# ip netns exec BRD2 ip l set veth23 master brd2
# ip netns exec BRD2 ip l set veth-brd2 master brd2

# forward
# ip netns exec R sh -c "echo 1 > /proc/sys/net/ipv4/conf/all/forwarding"
