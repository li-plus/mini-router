#!/usr/bin/env bash

cd $(dirname $0)
ip netns exec R3 bird -c bird_r3.conf -d -s bird_r3.ctl -P bird_r3.pid
