#!/usr/bin/env bash

set -e

# P12 <-----+ veth12
#    veth   |
#  10.0.1.2 |       veth-brd1
#           +---> BRD1 <--------> R
#           |               veth1
#           |             10.0.1.1
# P13 <-----+ veth13
#    veth
#  10.0.1.3

for NS in P12 P13 R BRD1
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
ip netns exec R ip a add 10.0.1.1/24 dev veth1
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
