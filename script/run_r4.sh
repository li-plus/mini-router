#!/usr/bin/env bash

cd $(dirname $0)
ip netns exec R4 bird -c bird_r4.conf -d -s bird_r4.ctl -P bird_r4.pid
