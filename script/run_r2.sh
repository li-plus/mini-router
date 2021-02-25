#!/usr/bin/env bash

cd $(dirname $0)
ip netns exec R2 bird -c bird_r2.conf -d -s bird_r2.ctl -P bird_r2.pid
