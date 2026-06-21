#!/bin/bash
cd /home/vis/libdynamichardware/build && rm -f /tmp/gpio_run.log && (timeout 10 ./gpio_demo > /tmp/gpio_run.log 2>&1; echo "DONE_EXIT=$?" >> /tmp/gpio_run.log) &
