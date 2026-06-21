#!/bin/bash
cd /home/vis/libdynamichardware/build
timeout 8 ./gpio_demo > /tmp/gpio_demo.log 2>&1
echo "EXIT=$?" >> /tmp/gpio_demo.log
