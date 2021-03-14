#!/usr/bin/env bash

function check() {
    CMD=$1
    EXPECTED=$2
    OUTPUT=$($CMD)
    if (( $(echo "$OUTPUT" | grep "$EXPECTED" | wc -l) > 0 ))
    then
        echo "\e[0;32mPASSED: $CMD\e[m"
    else
        echo "\e[0;31mFAILED: $CMD\e[m"
        echo "\e[0;31mOUTPUT: $OUTPUT\e[m"
        echo "\e[0;31mEXPECTED: $EXPECTED\e[m"
    fi
}

function test_switch() {
    bash switch.sh >/dev/null 2>&1

    ip netns exec BRD1 ../build/bin/switch ../conf/switch/s.json >/dev/null 2>&1 &
    PID=$!

    echo "Switch starting at PID $PID"

    CMD="ip netns exec P12 ping 10.0.1.3 -c 4"
    EXPECTED="ttl=64"
    check "$CMD" "$EXPECTED"

    CMD="ip netns exec P12 ping 10.0.1.1 -c 4"
    EXPECTED="ttl=64"
    check "$CMD" "$EXPECTED"

    CMD="ip netns exec R ping 10.0.1.3 -c 4"
    EXPECTED="ttl=64"
    check "$CMD" "$EXPECTED"

    echo "killing all subprocesses"
    kill -9 $PID
}

function test_router() {
    bash router.sh >/dev/null 2>&1

    ip netns exec R2 bird -c bird_r2.conf -s bird_r2.ctl -P bird_r2.pid
    ip netns exec R4 bird -c bird_r4.conf -s bird_r4.ctl -P bird_r4.pid
    ip netns exec R3 ../build/bin/router ../conf/router/r3.json >/dev/null 2>&1 &
    PID=$!

    echo "Router starting at PID $PID"

    sleep 15  # wait for RIP update

    CMD="ip netns exec R2 ping 10.0.2.1 -c 4"
    EXPECTED="ttl=64"
    check "$CMD" "$EXPECTED"

    CMD="ip netns exec R4 ping 10.0.3.1 -c 4"
    EXPECTED="ttl=64"
    check "$CMD" "$EXPECTED"

    CMD="ip netns exec R1 ping 10.0.4.9 -c 4"
    EXPECTED="ttl=61"
    check "$CMD" "$EXPECTED"

    CMD="ip netns exec R5 ping 10.0.1.9 -c 4"
    EXPECTED="ttl=61"
    check "$CMD" "$EXPECTED"

    CMD="ip netns exec R5 ping 10.0.1.9 -c 4"
    EXPECTED="ttl=61"
    check "$CMD" "$EXPECTED"

    CMD="ip netns exec R1 ping 10.0.4.9 -c 4 -t 2"
    EXPECTED="Time to live exceeded"
    check "$CMD" "$EXPECTED"

    echo "killing all subprocesses"
    killall bird
    kill -9 $PID
}

echo "===== BEGIN SWITCH TEST ====="
test_switch
echo "===== END SWITCH TEST ====="

echo "===== BEGIN ROUTER TEST ====="
test_router
echo "===== END ROUTER TEST ====="

