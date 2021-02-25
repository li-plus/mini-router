#!/usr/bin/env bash

set -e

# R1 <--------------> R2 <--------------> R3 <--------------> R4 <--------------> R5
#    r1r2        r2r1    r2r3        r3r2     r3r4       r4r3    r4r5        r5r4
#   10.0.1.9  10.0.1.1  10.0.2.9  10.0.2.1  10.0.3.1  10.0.3.9  10.0.4.1  10.0.4.9

for NS in R1 R2 R3 R4 R5
do
    ip netns delete $NS 2>/dev/null || true
    ip netns add $NS
    ip netns exec $NS ip l set lo up
done

# enable ip forward for R2 & R4
ip netns exec R2 sh -c "echo 1 > /proc/sys/net/ipv4/conf/all/forwarding"
ip netns exec R4 sh -c "echo 1 > /proc/sys/net/ipv4/conf/all/forwarding"

# R1 <-> R2
ip l add r1r2 netns R1 type veth peer name r2r1 netns R2

ip netns exec R1 ip a add 10.0.1.9/24 dev r1r2
ip netns exec R1 ip l set r1r2 up
ip netns exec R1 ethtool -K r1r2 tx off     # Disable linux checksum verfication
ip netns exec R1 ip r add default via 10.0.1.1

ip netns exec R2 ip a add 10.0.1.1/24 dev r2r1
ip netns exec R2 ip l set r2r1 up
ip netns exec R2 ethtool -K r2r1 tx off

# R2 <-> R3
ip l add r2r3 netns R2 type veth peer name r3r2 netns R3

ip netns exec R2 ip a add 10.0.2.9/24 dev r2r3
ip netns exec R2 ip l set r2r3 up
ip netns exec R2 ethtool -K r2r3 tx off

# ip netns exec R3 ip a add 10.0.2.1/24 dev r3r2
ip netns exec R3 ip l set r3r2 up
ip netns exec R3 ethtool -K r3r2 tx off

# R3 <-> R4
ip l add r3r4 netns R3 type veth peer name r4r3 netns R4

# ip netns exec R3 ip a add 10.0.3.1/24 dev r3r4
ip netns exec R3 ip l set r3r4 up
ip netns exec R3 ethtool -K r3r4 tx off

ip netns exec R4 ip a add 10.0.3.9/24 dev r4r3
ip netns exec R4 ip l set r4r3 up
ip netns exec R4 ethtool -K r4r3 tx off

# R4 <-> R5
ip l add r4r5 netns R4 type veth peer name r5r4 netns R5

ip netns exec R4 ip a add 10.0.4.1/24 dev r4r5
ip netns exec R4 ip l set r4r5 up
ip netns exec R4 ethtool -K r4r5 tx off

ip netns exec R5 ip a add 10.0.4.9/24 dev r5r4
ip netns exec R5 ip l set r5r4 up
ip netns exec R5 ethtool -K r5r4 tx off
ip netns exec R5 ip r add default via 10.0.4.1
