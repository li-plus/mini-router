# Mini-Router

A mini-router supporting basic ARP protocol, IP forwarding, ICMP echo / reply, and RIP routing.

## Build

Install project dependencies.

```shell
sudo apt install -y cmake libpcap-dev libjson-c-dev bird iperf3 wireshark
```

Build the project.

```shell
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j
```

## Run Router

Create a network topology using ip namespace.

```sh
# R1 <--------------> R2 <--------------> R3 <--------------> R4 <--------------> R5
#    r1r2        r2r1    r2r3        r3r2     r3r4       r4r3    r4r5        r5r4
#   10.0.1.9  10.0.1.1  10.0.2.9  10.0.2.1  10.0.3.1  10.0.3.9  10.0.4.1  10.0.4.9

cd ../script/
sudo bash router.sh
```

Run & test router.

```sh
# Start router in R3
sudo ip netns exec R3 ../build/bin/router ../conf/router/r3.json
# Ping router
sudo ip netns exec R2 ping 10.0.2.1 -c 4
sudo ip netns exec R4 ping 10.0.3.1 -c 4
# Test Host Unreachable
sudo ip netns exec R2 ip r add 10.0.233.1/32 via 10.0.2.1
sudo ip netns exec R2 ping 10.0.233.1 -c 4
# Start BIRD (a standard RIP router) in R2 & R4
sudo bash run_r2.sh
sudo bash run_r4.sh
# Ping R1 <-> R5
sudo ip netns exec R1 ping 10.0.4.9 -c 4
sudo ip netns exec R5 ping 10.0.1.9 -c 4
# Test TTL Exceeded
sudo ip netns exec R1 ping 10.0.4.9 -c 4 -t 2
```

Run speed test with iperf3.

```sh
# Start server
sudo ip netns exec R5 iperf3 -s
# Start client
sudo ip netns exec R1 iperf3 -c 10.0.4.9 -O 5 -P 10     # TCP multi-client
sudo ip netns exec R1 iperf3 -c 10.0.4.9 -O 5 -u -l 16 -b 1G    # UDP small packets
```

## Run Switch

Create a network topology

```sh
# P12 <-----+ veth12
#    veth   |
#  10.0.1.2 |       veth-brd1
#           +---> BRD1 <--------> R
#           |               veth1
#           |             10.0.1.1
# P13 <-----+ veth13
#    veth
#  10.0.1.3

sudo bash switch.sh
```

Run switch.

```sh
sudo ip netns exec BRD1 ../build/bin/switch ../conf/switch/s.json
```

Make some tests.

```sh
# connectivity test
sudo ip netns exec P12 ping 10.0.1.3 -c 4
sudo ip netns exec P12 ping 10.0.1.1 -c 4
sudo ip netns exec R ping 10.0.1.3 -c 4
# speed test
sudo ip netns exec R iperf3 -s
sudo ip netns exec P12 iperf3 -c 10.0.1.1 -O 5 -P 10
```

## Run Switch & Router

Create a network topology

```sh
# P12 <-----+ veth12                             veth22 +-----> P22
#    veth   |                                           |   veth
#  10.0.1.2 |       veth-brd1           veth-brd2       | 10.0.2.2
#           +---> BRD1 <--------> R <--------> BRD2 <---+
#           |               veth1   veth2               |
#           |            10.0.1.1   10.0.2.1            |
# P13 <-----+ veth13                             veth23 +-----> P23
#    veth                                                   veth
#  10.0.1.3                                               10.0.2.3

sudo bash complex.sh
```

Run router & switch.

```sh
# Run switch on 10.0.1.0/24 (bridge P12 & P13)
sudo ip netns exec BRD1 ../build/bin/switch ../conf/complex/s1.json
sudo ip netns exec P12 ping 10.0.1.3 -c 4
# Run switch on 10.0.2.0/24 (bridge P22 & P23)
sudo ip netns exec BRD2 ../build/bin/switch ../conf/complex/s2.json
sudo ip netns exec P23 ping 10.0.2.2 -c 4
# Run router on R
sudo ip netns exec R ../build/bin/router ../conf/complex/r.json
sudo ip netns exec P12 ping 10.0.2.3 -c 4
sudo ip netns exec P22 ping 10.0.1.2 -c 4
sudo ip netns exec P13 ping 10.0.2.1 -c 4   # ping router
```
