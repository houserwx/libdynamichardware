#!/bin/bash
# Deploy updated source + rebuild on vis@192.168.254.192 then run demos
set -e

REMOTE="vis@192.168.254.192"
PASS="vis"
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "=== Pushing updated sources to ${REMOTE} ==="
sshpass -p "$PASS" rsync -avz \
    src/dynamichardware/backends/gpio/GPIODiscovery.cpp \
    examples/gpio_correct_demo.cpp \
    include/ thirdparty/ CMakeLists.txt tests/ src/DynamicHardwareContextFactory.cpp \
    src/dynamichardware/dhdo/HardwareCatalog.cpp \
    src/tools/discover.cpp \
    src/dynamichardware/backends/gpio/GPIORTBackend.h \
    src/dynamichardware/backends/gpio/GPIORTBackend.cpp \
    "${REMOTE}:~/libdynamichardware/"

echo ""
echo "=== Building remotely (clean) ==="
sshpass -p "$PASS" ssh -o StrictHostKeyChecking=no "${REMOTE}" \
    'cd ~/libdynamichardware && rm -rf build && mkdir build && cd build && cmake .. >/dev/null 2>&1 && make -j$(nproc) 2>&1 | tail -15'

echo ""
echo "=== Copying config files ==="
sshpass -p "$PASS" scp "${PROJECT_DIR}/hardware.json" "${REMOTE}:~/libdynamichardware/build/hardware.json" || true

echo ""
echo "=== Running GPIO demo (30s timeout, fresh catalog) ==="
sshpass -p "$PASS" ssh -o StrictHostKeyChecking=no "${REMOTE}" \
    'cd ~/libdynamichardware/build && rm -f hardware.json && timeout 30 ./gpio_correct_demo 2>&1; echo "EXIT CODE: $?"'
